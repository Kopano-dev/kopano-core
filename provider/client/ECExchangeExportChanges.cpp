/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <kopano/platform.h>
#include <kopano/MAPIErrors.h>
#include <kopano/memory.hpp>
#include <kopano/mapi_ptr.h>
#include "ECExchangeExportChanges.h"
#include "WSMessageStreamExporter.h"
#include "WSSerializedMessage.h"
#include <set>
#include <kopano/Util.h>
#include <kopano/ECGuid.h>
#include <edkguid.h>
#include <mapiguid.h>
#include <kopano/mapiext.h>
#include <mapiutil.h>
#include "ics.h"
#include "Mem.h"
#include "ECMessage.h"
#include <kopano/stringutil.h>
#include "EntryPoint.h"
#include <kopano/CommonUtil.h>
#include <arpa/inet.h> /* ntohl */
#include <kopano/charset/convert.h>
#define ec_log_ics(...)   ec_log(EC_LOGLEVEL_DEBUG | EC_LOGLEVEL_SYNC, __VA_ARGS__)

using namespace KC;

class PropTagCompare final {
	public:
	bool operator()(unsigned int lhs, unsigned int rhs) const
	{
		if (PROP_TYPE(lhs) == PT_UNSPECIFIED || PROP_TYPE(rhs) == PT_UNSPECIFIED)
			return PROP_ID(lhs) < PROP_ID(rhs); 
		return lhs < rhs;
	}
};

/** 
 * Removes properties from lpDestMsg, which do are not listed in @validprops.
 *
 * @dst_msg:	The message to delete properties from, which are found "invalid".
 * @src_msg:	The message for which named properties may be lookupped, listed in @validprops.
 * @validprops:	Properties which are valid in @dst_msg. All others should be removed.
 *
 * Named properties listed in @validprops map to names in @src_msg. The
 * corresponding property tags are checked in @dst_msg.
 */
static HRESULT delete_residual_props(IMessage *dst_msg, IMessage *src_msg,
    SPropTagArray *validprops)
{
	memory_ptr<SPropTagArray> stdprops, namedprops, mappedprops;
	memory_ptr<MAPINAMEID *> propnames;
	unsigned int nnames = 0;
	std::set<unsigned int, PropTagCompare> proptags;

	if (dst_msg == nullptr || src_msg == nullptr || validprops == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto ret = dst_msg->GetPropList(0, &~stdprops);
	if (ret != hrSuccess || stdprops->cValues == 0)
		return ret;
	ret = MAPIAllocateBuffer(CbNewSPropTagArray(validprops->cValues), &~namedprops);
	if (ret != hrSuccess)
		return ret;

	memset(namedprops, 0, CbNewSPropTagArray(validprops->cValues));
	for (size_t i = 0; i < validprops->cValues; ++i)
		if (PROP_ID(validprops->aulPropTag[i]) >= 0x8000)
			namedprops->aulPropTag[namedprops->cValues++] = validprops->aulPropTag[i];
	if (namedprops->cValues > 0) {
		ret = src_msg->GetNamesFromIDs(&+namedprops, nullptr, 0, &nnames, &~propnames);
		if (FAILED(ret))
			return ret;
		ret = dst_msg->GetIDsFromNames(nnames, propnames, MAPI_CREATE, &~mappedprops);
		if (FAILED(ret))
			return ret;
	}

	/* Add the PropTags the message currently has */
	for (size_t i = 0; i < stdprops->cValues; ++i)
		proptags.emplace(stdprops->aulPropTag[i]);
	/* Remove the regular properties we want to keep */
	for (size_t i = 0; i < validprops->cValues; ++i)
		if (PROP_ID(validprops->aulPropTag[i]) < 0x8000)
			proptags.erase(validprops->aulPropTag[i]);
	/*
	 * Remove the mapped named properties we want to keep. Filter failed
	 * named properties, so they will be removed.
	 */
	for (size_t i = 0; mappedprops != nullptr && i < mappedprops->cValues; ++i)
		if (PROP_TYPE(mappedprops->aulPropTag[i]) != PT_ERROR)
			proptags.erase(mappedprops->aulPropTag[i]);
	if (proptags.empty())
		return hrSuccess;

	/* Reuse stdprops to hold the properties we are going to delete. */
	assert(stdprops->cValues >= proptags.size());
	memset(stdprops->aulPropTag, 0, stdprops->cValues * sizeof *stdprops->aulPropTag);
	stdprops->cValues = 0;
	for (const auto &i : proptags)
		stdprops->aulPropTag[stdprops->cValues++] = i;
	ret = dst_msg->DeleteProps(stdprops, nullptr);
	if (ret != hrSuccess)
		return ret;
	return dst_msg->SaveChanges(KEEP_OPEN_READWRITE);
}

ECExchangeExportChanges::ECExchangeExportChanges(ECMsgStore *lpStore,
    const std::string &sk, const wchar_t *szDisplay, unsigned int ulSyncType) :
	m_ulSyncType(ulSyncType), m_sourcekey(sk),
	m_strDisplay(szDisplay != nullptr ? szDisplay : L"<Unknown>"),
	/* In server-side sync, only use a batch size of 1. */
	m_ulBatchSize(sk.empty() ? 1 : 256), m_lpStore(lpStore)
{
	memset(&m_tmsStart, 0, sizeof(m_tmsStart));
}

HRESULT ECExchangeExportChanges::Create(ECMsgStore *lpStore, REFIID iid, const std::string& sourcekey, const wchar_t *szDisplay, unsigned int ulSyncType, LPEXCHANGEEXPORTCHANGES* lppExchangeExportChanges){
	if (lpStore == NULL || (ulSyncType != ICS_SYNC_CONTENTS && ulSyncType != ICS_SYNC_HIERARCHY))
		return MAPI_E_INVALID_PARAMETER;
	return alloc_wrap<ECExchangeExportChanges>(lpStore, sourcekey,
	       szDisplay, ulSyncType).as(iid, lppExchangeExportChanges);
}

HRESULT	ECExchangeExportChanges::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECExchangeExportChanges, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IExchangeExportChanges, this);
	REGISTER_INTERFACE2(IUnknown, this);
	REGISTER_INTERFACE2(IECExportChanges, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECExchangeExportChanges::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError){
	HRESULT		hr = hrSuccess;
	ecmem_ptr<MAPIERROR> lpMapiError;
	memory_ptr<TCHAR> lpszErrorMsg;

	//FIXME: give synchronization errors messages
	hr = Util::HrMAPIErrorToText((hResult == hrSuccess)?MAPI_E_NO_ACCESS : hResult, &~lpszErrorMsg);
	if (hr != hrSuccess)
		return hr;
	hr = ECAllocateBuffer(sizeof(MAPIERROR), &~lpMapiError);
	if(hr != hrSuccess)
		return hr;

	if (ulFlags & MAPI_UNICODE) {
		std::wstring wstrErrorMsg = convert_to<std::wstring>(lpszErrorMsg.get());
		std::wstring wstrCompName = convert_to<std::wstring>(g_strProductName.c_str());

		hr = MAPIAllocateMore(sizeof(std::wstring::value_type) * (wstrErrorMsg.size() + 1),
		     lpMapiError, reinterpret_cast<void **>(&lpMapiError->lpszError));
		if (hr != hrSuccess)
			return hr;
		wcscpy((wchar_t*)lpMapiError->lpszError, wstrErrorMsg.c_str());

		hr = MAPIAllocateMore(sizeof(std::wstring::value_type) * (wstrCompName.size() + 1),
		     lpMapiError, reinterpret_cast<void **>(&lpMapiError->lpszComponent));
		if (hr != hrSuccess)
			return hr;
		wcscpy((wchar_t*)lpMapiError->lpszComponent, wstrCompName.c_str());
	} else {
		std::string strErrorMsg = convert_to<std::string>(lpszErrorMsg.get());
		std::string strCompName = convert_to<std::string>(g_strProductName.c_str());

		hr = MAPIAllocateMore(strErrorMsg.size() + 1, lpMapiError,
		     reinterpret_cast<void **>(&lpMapiError->lpszError));
		if (hr != hrSuccess)
			return hr;
		strcpy((char*)lpMapiError->lpszError, strErrorMsg.c_str());
		hr = MAPIAllocateMore(strCompName.size() + 1, lpMapiError,
		     reinterpret_cast<void **>(&lpMapiError->lpszComponent));
		if (hr != hrSuccess)
			return hr;
		strcpy((char*)lpMapiError->lpszComponent, strCompName.c_str());
	}

	lpMapiError->ulContext		= 0;
	lpMapiError->ulLowLevelError= 0;
	lpMapiError->ulVersion		= 0;
	*lppMAPIError = lpMapiError.release();
	return hrSuccess;
}

HRESULT ECExchangeExportChanges::Config(LPSTREAM lpStream, ULONG ulFlags, LPUNKNOWN lpCollector, LPSRestriction lpRestriction, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize){
	struct sbcmp {
		bool operator()(const SBinary &l, const SBinary &r) const { return Util::CompareSBinary(l, r) < 0; }
	};
	HRESULT hr;
	unsigned int ulSyncId = 0, ulChangeId = 0, ulStep = 0;
	BOOL		bCanStream = FALSE;
	bool		bForceImplicitStateUpdate = false;
	std::map<SBinary, ChangeListIter, sbcmp> mapChanges;
	ChangeList		lstChange;
	std::string	sourcekey;

	if(m_bConfiged){
		zlog("Config() called twice");
		return MAPI_E_UNCONFIGURED;
	}
	if(lpRestriction) {
		hr = Util::HrCopySRestriction(&~m_lpRestrict, lpRestriction);
		if (hr != hrSuccess)
			return zlog("Invalid restriction", hr);
	} else {
		m_lpRestrict.reset();
	}

	m_ulFlags = ulFlags;

	if(! (ulFlags & SYNC_CATCHUP)) {
		if(lpCollector == NULL) {
			zlog("No importer to export to");
			return MAPI_E_INVALID_PARAMETER;
		}

		// We don't need the importer when doing SYNC_CATCHUP
		if(m_ulSyncType == ICS_SYNC_CONTENTS){
			hr = lpCollector->QueryInterface(IID_IExchangeImportContentsChanges, &~m_lpImportContents);
			if (hr == hrSuccess) {
				m_lpStore->lpTransport->HrCheckCapabilityFlags(KOPANO_CAP_ENHANCED_ICS, &bCanStream);
				if (bCanStream == TRUE) {
					zlog("Exporter supports enhanced ICS, checking importer...");
					hr = lpCollector->QueryInterface(IID_IECImportContentsChanges, &~m_lpImportStreamedContents);
					if (hr == MAPI_E_INTERFACE_NOT_SUPPORTED) {
						assert(m_lpImportStreamedContents == NULL);
						hr = hrSuccess;
						zlog("Importer does not support enhanced ICS");
					} else
						zlog("Importer supports enhanced ICS");
				} else
					zlog("Exporter does not support enhanced ICS");
			}
		}else if(m_ulSyncType == ICS_SYNC_HIERARCHY){
			hr = lpCollector->QueryInterface(IID_IExchangeImportHierarchyChanges, &~m_lpImportHierarchy);
		}else{
			hr = MAPI_E_INVALID_PARAMETER;
		}
		if(hr != hrSuccess)
			return hr;
	}

	if (lpStream == NULL){
		LARGE_INTEGER lint = {{ 0, 0 }};
		unsigned int tmp[2] = {0, 0}, ulSize = 0;

		zlog("Creating new exporter stream");
		hr = CreateStreamOnHGlobal(GlobalAlloc(GPTR, sizeof(tmp)), true, &~m_lpStream);
		if (hr != hrSuccess)
			return zlog("Unable to create new exporter stream", hr);
		m_lpStream->Seek(lint, STREAM_SEEK_SET, NULL);
		m_lpStream->Write(tmp, sizeof(tmp), &ulSize);
	} else {
		hr = lpStream->QueryInterface(IID_IStream, &~m_lpStream);
		if (hr != hrSuccess)
			return zlog("Passed state stream does not support IStream interface", hr);
	}

	hr = HrDecodeSyncStateStream(m_lpStream, &ulSyncId, &ulChangeId);
	if (hr != hrSuccess)
		return zlog("Unable to decode sync state stream", hr);
	ec_log_ics("Decoded state stream: syncid=%u, changeid=%u, processed changes=%lu",
		ulSyncId, ulChangeId, static_cast<unsigned long>(m_setProcessedChanges.size()));

	if(ulSyncId == 0) {
		ec_log_ics("Getting new sync id for folder \"%ls\"...", m_strDisplay.c_str());

		// Ignore any trailing garbage in the stream
		if (m_sourcekey.size() < 24 && (m_sourcekey[m_sourcekey.size()-1] & 0x80)) {
			// If we're getting a truncated sourcekey, untruncate it now so we can pass it to GetChanges()
			sourcekey = m_sourcekey;
			sourcekey[m_sourcekey.size()-1] &= ~0x80;
			sourcekey.append(1, '\x00');
			sourcekey.append(1, '\x00');
		} else {
			sourcekey = m_sourcekey;
		}

		// Register our sync with the server, get a sync ID
		hr = m_lpStore->lpTransport->HrSetSyncStatus(sourcekey, ulSyncId, ulChangeId, m_ulSyncType, 0, &ulSyncId);
		if (hr != hrSuccess)
			return zlog("Unable to update sync status on server", hr);
		ec_log_ics("New sync id for folder \"%ls\": %u", m_strDisplay.c_str(), ulSyncId);
		bForceImplicitStateUpdate = true;
	}

	MAPIFreeBuffer(m_lpChanges);
	hr = m_lpStore->lpTransport->HrGetChanges(sourcekey, ulSyncId, ulChangeId, m_ulSyncType, ulFlags, m_lpRestrict, &m_ulMaxChangeId, &m_ulChanges, &~m_lpChanges);
	if (hr != hrSuccess)
		return zlog("Unable to get changes from server", hr);
	m_ulSyncId = ulSyncId;
	m_ulChangeId = ulChangeId;
	ec_log(EC_LOGLEVEL_INFO | EC_LOGLEVEL_SYNC, "folder=\"%ls\" changes=%u syncid=%u changeid=%u",
		m_strDisplay.c_str(), m_ulChanges, m_ulSyncId, m_ulChangeId);
	/**
	 * Filter the changes.
	 * How this works:
	 * If no previous change has been seen for a particular sourcekey, the change will just be added to
	 * the correct list. The iterator into that list is stored in a map, keyed by the sourcekey.
	 * If a previous change is found, action is taken based on the type of that change. The action can
	 * involve removing the change from a change list and/or adding it to another list.
	 *
	 * Note that lstChange is a local list, which is copied into m_lstChange after filtering. This is
	 * done because m_lstChange is not a list but a vector, which is not well suited for removing
	 * items in the middle of the data.
	 */
	for (ulStep = 0; ulStep < m_ulChanges; ++ulStep) {
		// First check if this change hasn't been processed yet
		if (m_setProcessedChanges.find({m_lpChanges[ulStep].ulChangeId, std::string(reinterpret_cast<const char *>(m_lpChanges[ulStep].sSourceKey.lpb), m_lpChanges[ulStep].sSourceKey.cb)}) != m_setProcessedChanges.end())
			continue;

		auto iterLastChange = mapChanges.find(m_lpChanges[ulStep].sSourceKey);
		if (iterLastChange == mapChanges.end()) {
			switch (ICS_ACTION(m_lpChanges[ulStep].ulChangeType)) {
			case ICS_NEW:
			case ICS_CHANGE:
				mapChanges.emplace(m_lpChanges[ulStep].sSourceKey, lstChange.emplace(lstChange.end(), m_lpChanges[ulStep]));
				break;
			case ICS_FLAG:
				mapChanges.emplace(m_lpChanges[ulStep].sSourceKey, m_lstFlag.emplace(m_lstFlag.end(), m_lpChanges[ulStep]));
				break;
			case ICS_SOFT_DELETE:
				mapChanges.emplace(m_lpChanges[ulStep].sSourceKey, m_lstSoftDelete.emplace(m_lstSoftDelete.end(), m_lpChanges[ulStep]));
				break;
			case ICS_HARD_DELETE:
				mapChanges.emplace(m_lpChanges[ulStep].sSourceKey, m_lstHardDelete.emplace(m_lstHardDelete.end(), m_lpChanges[ulStep]));
				break;
			default:
				break;
			}
			continue;
		}

		switch (ICS_ACTION(m_lpChanges[ulStep].ulChangeType)) {
		case ICS_NEW:
			// This shouldn't happen since apparently we have another change for the same object.
			// However, if an object gets moved to another folder and back, we'll get a delete followed by an add. If that happens
			// we skip the delete and morph the current add to a change.
			if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_SOFT_DELETE || ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_HARD_DELETE) {
				auto iterNewChange = lstChange.emplace(lstChange.end(), *iterLastChange->second);
				iterNewChange->ulChangeType = (iterNewChange->ulChangeType & ~ICS_ACTION_MASK) | ICS_CHANGE;
				if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_SOFT_DELETE)
					m_lstSoftDelete.erase(iterLastChange->second);
				else
					m_lstHardDelete.erase(iterLastChange->second);
				iterLastChange->second = iterNewChange;
				ec_log_ics("Got an ICS_NEW change for a previously deleted object. I converted it to a change. sourcekey=%s",
					bin2hex(m_lpChanges[ulStep].sSourceKey).c_str());
			} else {
				ec_log_ics("Got an ICS_NEW change for an object we have seen before. prev_change=%04x, sourcekey=%s",
					iterLastChange->second->ulChangeType, bin2hex(m_lpChanges[ulStep].sSourceKey).c_str());
			}
			break;

		case ICS_CHANGE:
			// Any change is allowed as the previous change except a change.
			if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_CHANGE)
				ec_log_ics("Got an ICS_CHANGE on an object for which we just saw an ICS_CHANGE. sourcekey=%s",
					bin2hex(m_lpChanges[ulStep].sSourceKey).c_str());
			// A previous delete is allowed for the same reason as in ICS_NEW.
			if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_SOFT_DELETE || ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_HARD_DELETE) {
				if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_SOFT_DELETE)
					m_lstSoftDelete.erase(iterLastChange->second);
				else
					m_lstHardDelete.erase(iterLastChange->second);
				iterLastChange->second = lstChange.emplace(lstChange.end(), m_lpChanges[ulStep]);
				ec_log_ics("Got an ICS_CHANGE change for a previously deleted object. sourcekey=%s",
					bin2hex(m_lpChanges[ulStep].sSourceKey).c_str());
			} else if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_FLAG) {
				m_lstFlag.erase(iterLastChange->second);
				iterLastChange->second = lstChange.emplace(lstChange.end(), m_lpChanges[ulStep]);
				ec_log_ics("Upgraded a previous ICS_FLAG to ICS_CHANGED. sourcekey=%s",
					bin2hex(m_lpChanges[ulStep].sSourceKey).c_str());
			} else
				ec_log_ics("Ignoring ICS_CHANGE due to a previous change. prev_change=%04x, sourcekey=%s",
					iterLastChange->second->ulChangeType, bin2hex(m_lpChanges[ulStep].sSourceKey).c_str());
			break;

		case ICS_FLAG:
			// This is only allowed after an ICS_NEW and ICS_CHANGE. It will be ignored in any case.
			if (ICS_ACTION(iterLastChange->second->ulChangeType) != ICS_NEW && ICS_ACTION(iterLastChange->second->ulChangeType) != ICS_CHANGE)
				ec_log_ics("Got an ICS_FLAG with something else than a ICS_NEW or ICS_CHANGE as the previous changes. prev_change=%04x, sourcekey=%s",
					iterLastChange->second->ulChangeType, bin2hex(m_lpChanges[ulStep].sSourceKey).c_str());
			ec_log_ics("Ignoring ICS_FLAG due to previous ICS_NEW or ICS_CHANGE. prev_change=%04x, sourcekey=%s",
				iterLastChange->second->ulChangeType, bin2hex(m_lpChanges[ulStep].sSourceKey).c_str());
			break;

		case ICS_SOFT_DELETE:
		case ICS_HARD_DELETE:
			// We'll ignore the previous change and replace it with this delete. We won't write it now as
			// we could get an add for the same object. But because of the reordering (deletes after all adds/changes) we would delete
			// the new object. Therefore we'll make a change out of a delete - add/change.
			ec_log_ics("Replacing previous change with current ICS_xxxx_DELETE. prev_change=%04x, sourcekey=%s",
				iterLastChange->second->ulChangeType, bin2hex(m_lpChanges[ulStep].sSourceKey).c_str());
			if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_NEW || ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_CHANGE)
				lstChange.erase(iterLastChange->second);
			else if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_FLAG)
				m_lstFlag.erase(iterLastChange->second);
			else if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_SOFT_DELETE)
				m_lstSoftDelete.erase(iterLastChange->second);
			else if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_HARD_DELETE)
				m_lstHardDelete.erase(iterLastChange->second);

			if (ICS_ACTION(m_lpChanges[ulStep].ulChangeType) == ICS_SOFT_DELETE)
				iterLastChange->second = m_lstSoftDelete.emplace(m_lstSoftDelete.end(), m_lpChanges[ulStep]);
			else
				iterLastChange->second = m_lstHardDelete.emplace(m_lstHardDelete.end(), m_lpChanges[ulStep]);
			break;
		default:
			ec_log_ics("Got an unknown change. change=%04x, sourcekey=%s",
				m_lpChanges[ulStep].ulChangeType, bin2hex(m_lpChanges[ulStep].sSourceKey).c_str());
			break;
		}
	}

	m_lstChange.assign(lstChange.begin(), lstChange.end());
	m_ulBufferSize = (ulBufferSize != 0) ? ulBufferSize : 10;
	m_bConfiged = true;

	if (bForceImplicitStateUpdate) {
		ec_log_ics("Forcing state update for folder \"%ls\"", m_strDisplay.c_str());
		if (m_ulChangeId == 0)
			UpdateState(NULL);
	}
	return hrSuccess;
}

HRESULT ECExchangeExportChanges::Synchronize(ULONG *lpulSteps, ULONG *lpulProgress)
{
	HRESULT			hr = hrSuccess;

	if(!m_bConfiged){
		zlog("Config() not called before Synchronize()");
		return MAPI_E_UNCONFIGURED;
	}
	if(m_ulFlags & SYNC_CATCHUP){
		m_ulChangeId = std::max(m_ulMaxChangeId, m_ulChangeId);
		hr = UpdateStream(m_lpStream);
		if (hr == hrSuccess)
			*lpulProgress = *lpulSteps = 0;
		return hr;
	}
	if (*lpulProgress == 0 && ec_log_get()->Log(EC_LOGLEVEL_DEBUG))
		m_clkStart = times(&m_tmsStart);

	if(m_ulSyncType == ICS_SYNC_CONTENTS){
		hr = ExportMessageChanges();
		if(hr == SYNC_W_PROGRESS)
			goto progress;
		if(hr != hrSuccess)
			return hr;
		hr = ExportMessageDeletes();
		if(hr != hrSuccess)
			return hr;
		hr = ExportMessageFlags();
		if(hr != hrSuccess)
			return hr;
	}else if(m_ulSyncType == ICS_SYNC_HIERARCHY){
		hr = ExportFolderChanges();
		if(hr == SYNC_W_PROGRESS)
			goto progress;
		if(hr != hrSuccess)
			return hr;
		hr = ExportFolderDeletes();
		if(hr != hrSuccess)
			return hr;
	}else{
		return MAPI_E_INVALID_PARAMETER;
	}

	hr = UpdateStream(m_lpStream);
	if(hr != hrSuccess)
		return hr;

	if(! (m_ulFlags & SYNC_CATCHUP)) {
		if (m_ulSyncType == ICS_SYNC_CONTENTS)
			hr = m_lpImportContents->UpdateState(NULL);
		else
			hr = m_lpImportHierarchy->UpdateState(NULL);
		if (hr != hrSuccess)
			return zlog("Importer state update failed", hr);
	}

progress:
	if(hr == hrSuccess) {
		// The sync has completed. This means we can move to the next change ID on the server, and clear our
		// processed change list. If we cannot save the new state to the server, we just keep the previous
		// change ID and the (large) change list. This allows us always to have a state, even when we can't
		// communicate with the server.
		if(m_lpStore->lpTransport->HrSetSyncStatus(m_sourcekey, m_ulSyncId, m_ulMaxChangeId, m_ulSyncType, 0, &m_ulSyncId) == hrSuccess) {
			ec_log_ics("Done: syncid=%u, changeid=%u/%u", m_ulSyncId, m_ulChangeId, m_ulMaxChangeId);
			m_ulChangeId = m_ulMaxChangeId;
			m_setProcessedChanges.clear();

			if(m_ulChanges) {
				if (ec_log_get()->Log(EC_LOGLEVEL_DEBUG)) {
					struct tms	tmsEnd = {0};
					clock_t		clkEnd = times(&tmsEnd);
					double		dblDuration = 0;
					char		szDuration[64] = {0};

					// Calculate diff
					dblDuration = (double)(clkEnd - m_clkStart) / TICKS_PER_SEC;
					if (dblDuration >= 60)
						snprintf(szDuration, sizeof(szDuration), "%u:%02u.%03u min.", (unsigned)(dblDuration / 60), (unsigned)dblDuration % 60, (unsigned)(dblDuration * 1000 + .5) % 1000);
					else
						snprintf(szDuration, sizeof(szDuration), "%u.%03u s.", (unsigned)dblDuration % 60, (unsigned)(dblDuration * 1000 + .5) % 1000);
					ec_log_ics("folder changes synchronized in %s", szDuration);
				} else
					ec_log(EC_LOGLEVEL_INFO | EC_LOGLEVEL_SYNC, "folder changes synchronized");
			}
		}
	}

	*lpulSteps = m_lstChange.size();
	*lpulProgress = m_ulStep;
	return hr;
}

HRESULT ECExchangeExportChanges::UpdateState(LPSTREAM lpStream){
	if(!m_bConfiged){
		zlog("Config() not called before UpdateState()");
		return MAPI_E_UNCONFIGURED;
	}
	if (lpStream == NULL)
		lpStream = m_lpStream;
	return UpdateStream(lpStream);
}

HRESULT ECExchangeExportChanges::GetChangeCount(ULONG *lpcChanges) {
	ULONG cChanges = 0;

	if(!m_bConfiged){
		zlog("Config() not called before GetChangeCount()");
		return MAPI_E_UNCONFIGURED;
	}

	// Changes in flags or deletions are only one step all together
	if(!m_lstHardDelete.empty() || !m_lstSoftDelete.empty() || !m_lstFlag.empty())
		++cChanges;
	cChanges += m_lstChange.size();
	*lpcChanges = cChanges;
	return hrSuccess;
}

HRESULT ECExchangeExportChanges::ExportMessageChanges() {
	assert(m_lpImportContents != NULL);
	if (m_lpImportStreamedContents != NULL)
		return ExportMessageChangesFast();
	else
		return ExportMessageChangesSlow();
}

HRESULT ECExchangeExportChanges::ExportMessageChangesSlow() {
	HRESULT			hr = hrSuccess;
	memory_ptr<SPropValue> lpPropArray;
	memory_ptr<SPropTagArray> lpPropTagArray;
	unsigned int ulObjType, ulCount, ulSteps = 0, cbEntryID = 0;
	memory_ptr<ENTRYID> lpEntryID;
	static constexpr const SizedSPropTagArray(5, sptMessageExcludes) =
		{5, {PR_MESSAGE_SIZE, PR_MESSAGE_RECIPIENTS,
		PR_MESSAGE_ATTACHMENTS, PR_ATTACH_SIZE, PR_PARENT_SOURCE_KEY}};
	static constexpr const SizedSPropTagArray(7, sptImportProps) =
		{7, {PR_SOURCE_KEY, PR_LAST_MODIFICATION_TIME, PR_CHANGE_KEY,
		PR_PARENT_SOURCE_KEY, PR_PREDECESSOR_CHANGE_LIST, PR_ENTRYID,
		PR_ASSOCIATED}};
	static constexpr const SizedSPropTagArray(1, sptAttach) = {1, {PR_ATTACH_NUM}};

	while(m_ulStep < m_lstChange.size() && (m_ulBufferSize == 0 || ulSteps < m_ulBufferSize)){
		unsigned int ulFlags = 0;
		if ((m_lstChange.at(m_ulStep).ulChangeType & ICS_ACTION_MASK) == ICS_NEW)
			ulFlags |= SYNC_NEW_MESSAGE;

		object_ptr<IMessage> lpSourceMessage, lpDestMessage;
		object_ptr<IMAPITable> lpTable;
		rowset_ptr lpRows;
		if(!m_sourcekey.empty()) {
			// Normal exporter, get the import properties we need by opening the source message
			hr = m_lpStore->EntryIDFromSourceKey(
				m_lstChange.at(m_ulStep).sParentSourceKey.cb,
				m_lstChange.at(m_ulStep).sParentSourceKey.lpb,
				m_lstChange.at(m_ulStep).sSourceKey.cb,
				m_lstChange.at(m_ulStep).sSourceKey.lpb,
				&cbEntryID, &~lpEntryID);
			if(hr == MAPI_E_NOT_FOUND){
				hr = hrSuccess;
				goto next;
			}
			ec_log(EC_LOGLEVEL_INFO | EC_LOGLEVEL_SYNC, "change sourcekey: %s", bin2hex(m_lstChange.at(m_ulStep).sSourceKey).c_str());
			if(hr != hrSuccess) {
				ec_log_ics("Error while getting entryid from sourcekey %s: %s (%x)",
					bin2hex(m_lstChange.at(m_ulStep).sSourceKey).c_str(),
					GetMAPIErrorMessage(hr), hr);
				goto exit;
			}
			hr = m_lpStore->OpenEntry(cbEntryID, lpEntryID, &IID_IMessage, MAPI_MODIFY, &ulObjType, &~lpSourceMessage);
			if(hr == MAPI_E_NOT_FOUND){
				hr = hrSuccess;
				goto next;
			}
			if(hr != hrSuccess) {
				ec_log_ics("Unable to open message with entryid %s: %s (%x)",
					bin2hex(cbEntryID, lpEntryID.get()).c_str(), GetMAPIErrorMessage(hr), hr);
				goto exit;
			}
			/* Check if requested interface exists */
			object_ptr<IMessage> throwaway;
			hr = lpSourceMessage->QueryInterface(IID_IMessage, &~throwaway);
			if (hr != hrSuccess) {
				ec_log_ics("Unable to open message with entryid %s: %s (%x)",
					bin2hex(cbEntryID, lpEntryID.get()).c_str(), GetMAPIErrorMessage(hr), hr);
				goto exit;
			}
			throwaway.reset();

			hr = lpSourceMessage->GetProps(sptImportProps, 0, &ulCount, &~lpPropArray);
			if(FAILED(hr)) {
				zlog("Unable to get properties from source message", hr);
				goto exit;
			}
			hr = m_lpImportContents->ImportMessageChange(ulCount, lpPropArray, ulFlags, &~lpDestMessage);
		} else {
			// Server-wide ICS exporter, only export source key and parent source key
			SPropValue sProps[2];

			sProps[0].ulPropTag = PR_SOURCE_KEY;
			sProps[0].Value.bin = m_lstChange.at(m_ulStep).sSourceKey; // cheap copy is ok since pointer is valid during ImportMessageChange call
			sProps[1].ulPropTag = PR_PARENT_SOURCE_KEY;
			sProps[1].Value.bin = m_lstChange.at(m_ulStep).sParentSourceKey;
			hr = m_lpImportContents->ImportMessageChange(2, sProps, ulFlags, &~lpDestMessage);
		}

		if (hr == SYNC_E_IGNORE) {
			ec_log(EC_LOGLEVEL_INFO | EC_LOGLEVEL_SYNC, "ignored change");
			// Mark this change as processed
			hr = hrSuccess;
			goto next;
		}else if(hr == SYNC_E_OBJECT_DELETED){
			ec_log(EC_LOGLEVEL_INFO | EC_LOGLEVEL_SYNC, "ignored change for deleted item");
			// Mark this change as processed
			hr = hrSuccess;
			goto next;
		}else if(hr == SYNC_E_INVALID_PARAMETER){
			//exchange doesn't like our input sometimes
			ec_log(EC_LOGLEVEL_INFO | EC_LOGLEVEL_SYNC, "ignored change parameter");
			hr = hrSuccess;
			goto next;
		// TODO handle SYNC_E_OBJECT_DELETED, SYNC_E_CONFLICT, SYNC_E_NO_PARENT, SYNC_E_INCEST, SYNC_E_UNSYNCHRONIZED
		}else if(hr != hrSuccess){
			zlog("Error during message import", hr);
			goto exit;
		}
		if (lpDestMessage == NULL)
			// Import succeeded, but we have no message to export to. Treat this the same as
			// SYNC_E_IGNORE. This is required for the BES ICS exporter
			goto next;
		if (lpSourceMessage == nullptr)
			/*
			 * Well this is getting silly. If we do not have a
			 * source message, where the heck should we copy from?
			 */
			goto next;
		hr = lpSourceMessage->CopyTo(0, NULL, sptMessageExcludes, 0,
		     NULL, &IID_IMessage, lpDestMessage, 0, NULL);
		if(hr != hrSuccess) {
			zlog("Unable to copy to imported message", hr);
			goto exit;
		}
		hr = lpSourceMessage->GetRecipientTable(0, &~lpTable);
		if(hr !=  hrSuccess) {
			zlog("Unable to read source message's recipient table", hr);
			goto exit;
		}
		hr = lpTable->QueryColumns(TBL_ALL_COLUMNS, &~lpPropTagArray);
		if (hr != hrSuccess) {
			zlog("Unable to get column set from source message's recipient table", hr);
			goto exit;
		}
		hr = lpTable->SetColumns(lpPropTagArray, 0);
		if (hr != hrSuccess) {
			zlog("Unable to set column set for source message's recipient table", hr);
			goto exit;
		}
		hr = lpTable->QueryRows(0xFFFF, 0, &~lpRows);
		if(hr !=  hrSuccess) {
			zlog("Unable to read recipients from source message", hr);
			goto exit;
		}
		//FIXME: named property in the recipienttable ?
		hr = lpDestMessage->ModifyRecipients(0, reinterpret_cast<ADRLIST *>(lpRows.get()));
		if(hr !=  hrSuccess)
			hr = hrSuccess;
			//goto exit;

		//delete every attachment
		hr = lpDestMessage->GetAttachmentTable(0, &~lpTable);
		if(hr !=  hrSuccess) {
			zlog("Unable to get destination's attachment table", hr);
			goto exit;
		}
		hr = lpTable->SetColumns(sptAttach, 0);
		if(hr !=  hrSuccess) {
			zlog("Unable to set destination's attachment table's column set", hr);
			goto exit;
		}
		hr = lpTable->QueryRows(0xFFFF, 0, &~lpRows);
		if(hr !=  hrSuccess) {
			zlog("Unable to read destination's attachment list", hr);
			goto exit;
		}

		for (ulCount = 0; ulCount < lpRows->cRows; ++ulCount) {
			hr = lpDestMessage->DeleteAttach(lpRows[ulCount].lpProps[0].Value.ul, 0, nullptr, 0);
			if(hr != hrSuccess) {
				ec_log_ics("Unable to delete destination's attachment number %d: %s (%x)",
					lpRows[ulCount].lpProps[0].Value.ul, GetMAPIErrorMessage(hr), hr);
				goto exit;
			}
		}

		//add every attachment
		hr = lpSourceMessage->GetAttachmentTable(0, &~lpTable);
		if(hr !=  hrSuccess)
			goto exit;
		hr = lpTable->SetColumns(sptAttach, 0);
		if(hr !=  hrSuccess)
			goto exit;
		hr = lpTable->QueryRows(0xFFFF, 0, &~lpRows);
		if(hr !=  hrSuccess)
			goto exit;

		for (ulCount = 0; ulCount < lpRows->cRows; ++ulCount) {
			object_ptr<IAttach> lpSourceAttach, lpDestAttach;
			hr = lpSourceMessage->OpenAttach(lpRows[ulCount].lpProps[0].Value.ul, &IID_IAttachment, 0, &~lpSourceAttach);
			if(hr !=  hrSuccess) {
				ec_log_ics("Unable to open attachment %d in source message: %s (%x)",
					lpRows[ulCount].lpProps[0].Value.ul, GetMAPIErrorMessage(hr), hr);
				goto exit;
			}
			hr = lpDestMessage->CreateAttach(&IID_IAttachment, 0, &ulObjType, &~lpDestAttach);
			if(hr !=  hrSuccess) {
				zlog("Unable to create attachment", hr);
				goto exit;
			}
			hr = lpSourceAttach->CopyTo(0, NULL, sptAttach, 0,
			     NULL, &IID_IAttachment, lpDestAttach, 0, NULL);
			if(hr !=  hrSuccess) {
				zlog("Unable to copy attachment", hr);
				goto exit;
			}
			hr = lpDestAttach->SaveChanges(0);
			if(hr !=  hrSuccess) {
				zlog("SaveChanges() failed for destination attachment", hr);
				goto exit;
			}
		}
		lpTable.release();

		hr = lpSourceMessage->GetPropList(0, &~lpPropTagArray);
		if (hr != hrSuccess) {
			zlog("Unable to get property list of source message", hr);
			goto exit;
		}
		hr = delete_residual_props(lpDestMessage, lpSourceMessage, lpPropTagArray);
		if (hr != hrSuccess) {
			zlog("Unable to remove old properties from destination message", hr);
			goto exit;
		}
		hr = lpDestMessage->SaveChanges(0);
		if(hr != hrSuccess) {
			zlog("SaveChanges failed for destination message", hr);
			goto exit;
		}

next:
		// Mark this change as processed, even if we skipped it due to SYNC_E_IGNORE or because the item was deleted on the source server
		const auto &sk = m_lstChange.at(m_ulStep).sSourceKey;
		m_setProcessedChanges.emplace(m_lstChange.at(m_ulStep).ulChangeId, std::string(reinterpret_cast<const char *>(sk.lpb), sk.cb));
		++m_ulStep;
		++ulSteps;
	}

	if(m_ulStep < m_lstChange.size())
		hr = SYNC_W_PROGRESS;
exit:
	if(hr != hrSuccess && hr != SYNC_W_PROGRESS)
		ec_log(EC_LOGLEVEL_ERROR | EC_LOGLEVEL_SYNC, "change error: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
	return hr;
}

HRESULT ECExchangeExportChanges::ExportMessageChangesFast()
{
	HRESULT hr = hrSuccess;
	object_ptr<WSSerializedMessage> ptrSerializedMessage;
	unsigned int cbProps = 0, ulFlags = 0;
	SPropValuePtr ptrProps;
	const SPropValue *lpPropVal = NULL;
	StreamPtr ptrDestStream;
	static constexpr const SizedSPropTagArray(11, sptImportProps) = { 11, {
		PR_SOURCE_KEY,
		PR_LAST_MODIFICATION_TIME,
		PR_CHANGE_KEY,
		PR_PARENT_SOURCE_KEY,
		PR_PREDECESSOR_CHANGE_LIST,
		PR_ENTRYID,
		PR_ASSOCIATED,
		PR_MESSAGE_FLAGS, /* needed for backward compat since PR_ASSOCIATED is not supported on earlier systems */
		PR_STORE_RECORD_KEY,
		PR_EC_HIERARCHYID,
		PR_EC_PARENT_HIERARCHYID
	} };
	static constexpr const SizedSPropTagArray(7, sptImportPropsServerWide) = { 7, {
		PR_SOURCE_KEY,
		PR_PARENT_SOURCE_KEY,
		PR_STORE_RECORD_KEY,
		PR_STORE_ENTRYID,
		PR_EC_HIERARCHYID,
		PR_EC_PARENT_HIERARCHYID,
		PR_ENTRYID
	} };
	const auto lpImportProps = m_sourcekey.empty() ? sptImportPropsServerWide : sptImportProps;

	// No more changes (add/modify).
	ec_log_ics("ExportFast: At step %u, changeset contains %zu items)",
		m_ulStep, m_lstChange.size());
	if (m_ulStep >= m_lstChange.size())
		goto exit;

	if (!m_ptrStreamExporter || m_ptrStreamExporter->IsDone()) {
		ec_log_ics("ExportFast: Requesting new batch, batch size = %u", m_ulBatchSize);
		hr = m_lpStore->ExportMessageChangesAsStream(m_ulFlags & (SYNC_BEST_BODY | SYNC_LIMITED_IMESSAGE), m_ulEntryPropTag, m_lstChange, m_ulStep, m_ulBatchSize, lpImportProps, &~m_ptrStreamExporter);
		if (hr == MAPI_E_UNABLE_TO_COMPLETE) {
			// There was nothing to export (see ExportMessageChangesAsStream documentation)
			assert(m_ulStep >= m_lstChange.size());	// @todo: Is this a correct assumption?
			hr = hrSuccess;
			goto exit;
		} else if (hr != hrSuccess) {
			zlog("ExportFast: Stream export failed", hr);
			goto exit;
		}
		zlog("ExportFast: Got new batch");
	}

	ec_log_ics("ExportFast: Requesting serialized message, step = %u", m_ulStep);
	hr = m_ptrStreamExporter->GetSerializedMessage(m_ulStep, &~ptrSerializedMessage);
	if (hr == SYNC_E_OBJECT_DELETED) {
		zlog("ExportFast: Source message is deleted");
		hr = hrSuccess;
		goto skip;
	} else if (hr != hrSuccess) {
		zlog("ExportFast: Unable to get serialized message", hr);
		goto exit;
	}
	hr = ptrSerializedMessage->GetProps(&cbProps, &~ptrProps);
	if (hr != hrSuccess) {
		zlog("ExportFast: Unable to get required properties from serialized message", hr);
		goto exit;
	}
	lpPropVal = PCpropFindProp(ptrProps, cbProps, PR_MESSAGE_FLAGS);
	if (lpPropVal != NULL && (lpPropVal->Value.ul & MSGFLAG_ASSOCIATED))
		ulFlags |= SYNC_ASSOCIATED;
	if ((m_lstChange.at(m_ulStep).ulChangeType & ICS_ACTION_MASK) == ICS_NEW)
		ulFlags |= SYNC_NEW_MESSAGE;

	zlog("ExportFast: Importing message change");
	hr = m_lpImportStreamedContents->ImportMessageChangeAsAStream(cbProps, ptrProps, ulFlags, &~ptrDestStream);
	if (hr == hrSuccess) {
		zlog("ExportFast: Copying data");
		hr = ptrSerializedMessage->CopyData(ptrDestStream);
		if (hr != hrSuccess) {
			zlog("ExportFast: Failed to copy data", hr);
			LogMessageProps(EC_LOGLEVEL_DEBUG, cbProps, ptrProps);
			goto exit;
		}
		zlog("ExportFast: Copied data");
	} else if (hr == SYNC_E_IGNORE || hr == SYNC_E_OBJECT_DELETED) {
		zlog("ExportFast: Change ignored", hr);
		hr = ptrSerializedMessage->DiscardData();
		if (hr != hrSuccess) {
			zlog("ExportFast: Failed to discard data", hr);
			LogMessageProps(EC_LOGLEVEL_DEBUG, cbProps, ptrProps);
			goto exit;
		}
	} else {
		zlog("ExportFast: Import failed", hr);
		LogMessageProps(EC_LOGLEVEL_DEBUG, cbProps, ptrProps);
		goto exit;
	}

skip:
	m_setProcessedChanges.emplace(m_lstChange.at(m_ulStep).ulChangeId, std::string(reinterpret_cast<const char *>(m_lstChange.at(m_ulStep).sSourceKey.lpb), m_lstChange.at(m_ulStep).sSourceKey.cb));
	if (++m_ulStep < m_lstChange.size())
		hr = SYNC_W_PROGRESS;
exit:
	if (FAILED(hr))
		m_ptrStreamExporter.reset();
	return zlog("ExportFast: Done", hr);
}

HRESULT ECExchangeExportChanges::ExportMessageFlags(){
	HRESULT			hr = hrSuccess;
	memory_ptr<READSTATE> lpReadState;
	ULONG			ulCount;

	if(m_lstFlag.empty())
		goto exit;
	hr = MAPIAllocateBuffer(sizeof(READSTATE) * m_lstFlag.size(), &~lpReadState);
	if (hr != hrSuccess)
		goto exit;

	ulCount = 0;
	for (const auto &change : m_lstFlag) {
		lpReadState[ulCount].cbSourceKey = change.sSourceKey.cb;
		hr = KAllocCopy(change.sSourceKey.lpb, change.sSourceKey.cb, reinterpret_cast<void **>(&lpReadState[ulCount].pbSourceKey), lpReadState);
		if (hr != hrSuccess)
			goto exit;
		lpReadState[ulCount].ulFlags = change.ulFlags;
		++ulCount;
	}

	if(ulCount > 0){
		hr = m_lpImportContents->ImportPerUserReadStateChange(ulCount, lpReadState);
		if (hr == SYNC_E_IGNORE)
			hr = hrSuccess;
		if(hr != hrSuccess) {
			zlog("Read state change failed", hr);
			goto exit;
		}
		// Mark the flag changes as processed
		for (const auto &change : m_lstFlag)
			m_setProcessedChanges.emplace(change.ulChangeId, std::string(reinterpret_cast<const char *>(change.sSourceKey.lpb), change.sSourceKey.cb));
	}

exit:
	if (hr != hrSuccess)
		ec_log(EC_LOGLEVEL_ERROR | EC_LOGLEVEL_SYNC, "Failed to sync message flags: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
	return hr;
}

HRESULT ECExchangeExportChanges::ExportMessageDeletes(){
	memory_ptr<ENTRYLIST> lpEntryList;

	if(!m_lstSoftDelete.empty()){
		auto hr = ChangesToEntrylist(&m_lstSoftDelete, &~lpEntryList);
		if(hr != hrSuccess)
			return hr;
		hr = m_lpImportContents->ImportMessageDeletion(SYNC_SOFT_DELETE, lpEntryList);
		if (hr == SYNC_E_IGNORE)
			hr = hrSuccess;
		if (hr != hrSuccess)
			return zlog("Message deletion import failed", hr);
		hr = AddProcessedChanges(m_lstSoftDelete);
		if (hr != hrSuccess)
			return zlog("Unable to add processed soft deletion changes", hr);
	}

	if(!m_lstHardDelete.empty()){
		auto hr = ChangesToEntrylist(&m_lstHardDelete, &~lpEntryList);
		if (hr != hrSuccess)
			return zlog("Unable to create entry list", hr);
		hr = m_lpImportContents->ImportMessageDeletion(0, lpEntryList);
		if (hr == SYNC_E_IGNORE)
			hr = hrSuccess;
		if (hr != hrSuccess)
			return zlog("Message hard deletion failed", hr);
		hr = AddProcessedChanges(m_lstHardDelete);
		if (hr != hrSuccess)
			return zlog("Unable to add processed hard deletion changes", hr);
	}
	return hrSuccess;
}

HRESULT ECExchangeExportChanges::ExportFolderChanges(){
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpPropVal = NULL;
	unsigned int ulObjType = 0, ulCount = 0, ulSteps = 0, cbEntryID = 0;

	while(m_ulStep < m_lstChange.size() && (m_ulBufferSize == 0 || ulSteps < m_ulBufferSize)){
		object_ptr<IMAPIFolder> lpFolder;
		memory_ptr<SPropValue> lpPropArray;
		memory_ptr<ENTRYID> lpEntryID;

		if(!m_sourcekey.empty()) {
			// Normal export, need all properties
			hr = m_lpStore->EntryIDFromSourceKey(
				m_lstChange.at(m_ulStep).sSourceKey.cb,
				m_lstChange.at(m_ulStep).sSourceKey.lpb,
				0, NULL,
				&cbEntryID, &~lpEntryID);
			if(hr != hrSuccess){
				ec_log(EC_LOGLEVEL_INFO | EC_LOGLEVEL_SYNC, "change sourcekey not found");
				hr = hrSuccess;
				goto next;
			}
			ec_log(EC_LOGLEVEL_INFO | EC_LOGLEVEL_SYNC, "change sourcekey: %s", bin2hex(m_lstChange.at(m_ulStep).sSourceKey).c_str());
			hr = m_lpStore->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpFolder);
			if(hr != hrSuccess){
				hr = hrSuccess;
				goto next;
			}
			hr = HrGetAllProps(lpFolder, (m_ulFlags & SYNC_UNICODE ? MAPI_UNICODE : 0), &ulCount, &~lpPropArray);
			if (FAILED(hr))
				return zlog("Unable to get source folder properties", hr);

			//for folders directly under m_lpFolder PR_PARENT_SOURCE_KEY must be NULL
			//this protects against recursive problems during syncing when the PR_PARENT_SOURCE_KEY
			//equals the sourcekey of the folder which we are already syncing.
			//If the exporter sends an empty PR_PARENT_SOURCE_KEY to the importer the importer can
			//assume the parent is the folder which it is syncing.

			lpPropVal = PpropFindProp(lpPropArray, ulCount, PR_PARENT_SOURCE_KEY);
			if (lpPropVal != nullptr && m_sourcekey.size() == lpPropVal->Value.bin.cb &&
			    memcmp(lpPropVal->Value.bin.lpb, m_sourcekey.c_str(), m_sourcekey.size()) == 0)
				lpPropVal->Value.bin.cb = 0;

			hr = m_lpImportHierarchy->ImportFolderChange(ulCount, lpPropArray);
		} else {
			// Server-wide ICS
			SPropValue sProps;
			sProps.ulPropTag = PR_SOURCE_KEY;
			sProps.Value.bin = m_lstChange.at(m_ulStep).sSourceKey;
			hr = m_lpImportHierarchy->ImportFolderChange(1, &sProps);
		}

		if (hr == SYNC_E_IGNORE){
			ec_log(EC_LOGLEVEL_INFO | EC_LOGLEVEL_SYNC, "change ignored");
			hr = hrSuccess;
			goto next;
		}else if (hr == MAPI_E_INVALID_PARAMETER){
			ec_log(EC_LOGLEVEL_INFO | EC_LOGLEVEL_SYNC, "change invalid parameter");
			hr = hrSuccess;
			goto next;
		}else if(hr == MAPI_E_NOT_FOUND){
			ec_log(EC_LOGLEVEL_INFO | EC_LOGLEVEL_SYNC, "change not found");
			hr = hrSuccess;
			goto next;
		}else if(FAILED(hr)) {
			ec_log(EC_LOGLEVEL_ERROR | EC_LOGLEVEL_SYNC, "change error: %s (%x)",
				GetMAPIErrorMessage(hr), hr);
			return hr;
		}else if(hr != hrSuccess){
			ec_log(EC_LOGLEVEL_WARNING | EC_LOGLEVEL_SYNC, "change warning: %s (%x)",
				GetMAPIErrorMessage(hr), hr);
		}
next:
		// Mark this change as processed
		const auto &sk = m_lstChange.at(m_ulStep).sSourceKey;
		m_setProcessedChanges.emplace(m_lstChange.at(m_ulStep).ulChangeId, std::string(reinterpret_cast<const char *>(sk.lpb), sk.cb));
		++ulSteps;
		++m_ulStep;
	}

	if(m_ulStep < m_lstChange.size())
		hr = SYNC_W_PROGRESS;
	return hr;
}

HRESULT ECExchangeExportChanges::ExportFolderDeletes(){
	HRESULT			hr = hrSuccess;
	memory_ptr<ENTRYLIST> lpEntryList;

	if(!m_lstSoftDelete.empty()){
		hr = ChangesToEntrylist(&m_lstSoftDelete, &~lpEntryList);
		if (hr != hrSuccess)
			return zlog("Unable to create folder deletion entry list", hr);
		hr = m_lpImportHierarchy->ImportFolderDeletion(SYNC_SOFT_DELETE, lpEntryList);
		if (hr == SYNC_E_IGNORE)
			hr = hrSuccess;
		if (hr != hrSuccess)
			return zlog("Unable to import folder deletions", hr);
		hr = AddProcessedChanges(m_lstSoftDelete);
		if (hr != hrSuccess)
			return zlog("Unable to add processed folder soft deletions", hr);
	}

	if(!m_lstHardDelete.empty()){
		hr = ChangesToEntrylist(&m_lstHardDelete, &~lpEntryList);
		if (hr != hrSuccess)
			return zlog("Unable to create folder hard delete entry list", hr);
		hr = m_lpImportHierarchy->ImportFolderDeletion(0, lpEntryList);
		if (hr == SYNC_E_IGNORE)
			hr = hrSuccess;
		if (hr != hrSuccess)
			return zlog("Hard delete folder import failed", hr);
		hr = AddProcessedChanges(m_lstHardDelete);
		if (hr != hrSuccess)
			return zlog("Unable to add processed folder hard deletions", hr);
	}
	return hrSuccess;
}

//write in the stream 4 bytes syncid, 4 bytes changeid, 4 bytes changecount, {4 bytes changeid, 4 bytes sourcekeysize, sourcekey}
HRESULT ECExchangeExportChanges::UpdateStream(LPSTREAM lpStream){
	HRESULT hr = hrSuccess;
	LARGE_INTEGER liPos = {{0, 0}};
	ULARGE_INTEGER liZero = {{0, 0}};
	unsigned int ulSize, ulChangeCount = 0, ulChangeId = 0, ulSourceKeySize = 0;

	if(lpStream == NULL)
		goto exit;
	hr = lpStream->SetSize(liZero);
	if(hr != hrSuccess)
		goto exit;
	hr = lpStream->Seek(liPos, STREAM_SEEK_SET, NULL);
	if(hr != hrSuccess)
		goto exit;
	hr = lpStream->Write(&m_ulSyncId, 4, &ulSize);
	if(hr != hrSuccess)
		goto exit;
	if (m_ulSyncId == 0)
		m_ulChangeId = 0;
	hr = lpStream->Write(&m_ulChangeId, 4, &ulSize);
	if(hr != hrSuccess)
		goto exit;

	if(!m_setProcessedChanges.empty()) {
		ulChangeCount = m_setProcessedChanges.size();
		hr = lpStream->Write(&ulChangeCount, 4, &ulSize);
		if(hr != hrSuccess)
			goto exit;

		for (const auto &pc : m_setProcessedChanges) {
			ulChangeId = pc.first;
			hr = lpStream->Write(&ulChangeId, 4, &ulSize);
			if(hr != hrSuccess)
				goto exit;
			ulSourceKeySize = pc.second.size();
			hr = lpStream->Write(&ulSourceKeySize, 4, &ulSize);
			if(hr != hrSuccess)
				goto exit;
			hr = lpStream->Write(pc.second.c_str(), pc.second.size(), &ulSize);
			if(hr != hrSuccess)
				goto exit;
		}
	}

	// Seek back to the beginning after we've finished
	lpStream->Seek(liPos, STREAM_SEEK_SET, NULL);
exit:
	if (hr != hrSuccess)
		return zlog("Stream operation failed", hr);
	return hrSuccess;
}

//convert (delete) changes to entrylist for message and folder deletion.
HRESULT ECExchangeExportChanges::ChangesToEntrylist(std::list<ICSCHANGE> * lpLstChanges, LPENTRYLIST * lppEntryList){
	HRESULT 		hr = hrSuccess;
	memory_ptr<ENTRYLIST> lpEntryList;
	ULONG			ulCount = 0;

	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~lpEntryList);
	if (hr != hrSuccess)
		return hr;

	lpEntryList->cValues = lpLstChanges->size();
	if(lpEntryList->cValues > 0){
		hr = MAPIAllocateMore(sizeof(SBinary) * lpEntryList->cValues, lpEntryList, reinterpret_cast<void **>(&lpEntryList->lpbin));
		if (hr != hrSuccess)
			return hr;
		ulCount = 0;
		for (const auto &change : *lpLstChanges) {
			lpEntryList->lpbin[ulCount].cb = change.sSourceKey.cb;
			hr = KAllocCopy(change.sSourceKey.lpb, change.sSourceKey.cb,
			     reinterpret_cast<void **>(&lpEntryList->lpbin[ulCount].lpb), lpEntryList);
			if (hr != hrSuccess)
				return hr;
			++ulCount;
		}
	}else{
		lpEntryList->lpbin = NULL;
	}

	lpEntryList->cValues = ulCount;
	*lppEntryList = lpEntryList.release();
	return hrSuccess;
}

/**
 * Add processed changes to the precessed changes list
 *
 * @param[in] lstChanges	List with changes
 *
 */
HRESULT ECExchangeExportChanges::AddProcessedChanges(ChangeList &lstChanges)
{
	for (const auto &i : lstChanges)
		m_setProcessedChanges.emplace(i.ulChangeId,
			std::string(reinterpret_cast<const char *>(i.sSourceKey.lpb), i.sSourceKey.cb));
	return hrSuccess;
}

void ECExchangeExportChanges::LogMessageProps(int loglevel, ULONG cValues, LPSPropValue lpPropArray)
{
	if (!ec_log_get()->Log(loglevel))
		return;
	auto lpPropEntryID = PCpropFindProp(lpPropArray, cValues, PR_ENTRYID);
	auto lpPropSK = PCpropFindProp(lpPropArray, cValues, PR_SOURCE_KEY);
	auto lpPropFlags = PCpropFindProp(lpPropArray, cValues, PR_MESSAGE_FLAGS);
	auto lpPropHierarchyId = PCpropFindProp(lpPropArray, cValues, PR_EC_HIERARCHYID);
	auto lpPropParentId = PCpropFindProp(lpPropArray, cValues, PR_EC_PARENT_HIERARCHYID);

	ec_log(loglevel | EC_LOGLEVEL_SYNC, "ExportFast:   Message info: id=%u, parentid=%u, msgflags=%x, entryid=%s, sourcekey=%s",
		lpPropHierarchyId != NULL ? lpPropHierarchyId->Value.ul : 0,
		lpPropParentId != NULL ? lpPropParentId->Value.ul : 0,
		lpPropFlags != NULL ? lpPropFlags->Value.ul : 0,
		lpPropEntryID != NULL ? bin2hex(lpPropEntryID->Value.bin).c_str() : "<Unknown>",
		lpPropSK != NULL ? bin2hex(lpPropSK->Value.bin).c_str() : "<Unknown>");
}

HRESULT ECExchangeExportChanges::zlog(const char *msg, HRESULT code)
{
	if (code == hrSuccess)
		ec_log_ics("%s", msg);
	else
		ec_log_ics("%s: %s (%x)", msg, GetMAPIErrorMessage(code), code);
	return code;
}

HRESULT ECExchangeExportChanges::HrDecodeSyncStateStream(IStream *lpStream,
    unsigned int *lpulSyncId, unsigned int *lpulChangeId)
{
	STATSTG stat;
	ULONG		ulSyncId = 0;
	ULONG		ulChangeId = 0;
	ULONG		ulChangeCount = 0;
	ULONG		ulProcessedChangeId = 0;
	ULONG		ulSourceKeySize = 0;
	LARGE_INTEGER liPos = {{0, 0}};
	PROCESSEDCHANGESSET setProcessedChanged;

	auto hr = lpStream->Stat(&stat, STATFLAG_NONAME);
	if (hr != hrSuccess)
		return hr;

	if (stat.cbSize.HighPart == 0 && stat.cbSize.LowPart == 0) {
		ulSyncId = 0;
		ulChangeId = 0;
	} else {
		if (stat.cbSize.HighPart != 0 || stat.cbSize.LowPart < 8)
			return MAPI_E_INVALID_PARAMETER;
		hr = lpStream->Seek(liPos, STREAM_SEEK_SET, nullptr);
		if (hr != hrSuccess)
			return hr;
		hr = lpStream->Read(&ulSyncId, 4, nullptr);
		if (hr != hrSuccess)
			return hr;
		hr = lpStream->Read(&ulChangeId, 4, nullptr);
		if (hr != hrSuccess)
			return hr;

		// Following the sync ID and the change ID is the list of changes that were already processed for
		// this sync ID / change ID combination. This allows us partial processing of items retrieved from
		// the server.
		if (lpStream->Read(&ulChangeCount, 4, nullptr) == hrSuccess) {
			// The stream contains a list of already processed items, read them
			for (ULONG i = 0; i < ulChangeCount; ++i) {
				std::unique_ptr<char[]> lpData;

				hr = lpStream->Read(&ulProcessedChangeId, 4, nullptr);
				if (hr != hrSuccess)
					/* Not the amount of expected bytes are there */
					return hr;
				hr = lpStream->Read(&ulSourceKeySize, 4, nullptr);
				if (hr != hrSuccess)
					return hr;
				if (ulSourceKeySize > 1024)
					// Stupidly large source key, the stream must be bad.
					return MAPI_E_INVALID_PARAMETER;
				lpData.reset(new char[ulSourceKeySize]);
				hr = lpStream->Read(lpData.get(), ulSourceKeySize, nullptr);
				if (hr != hrSuccess)
					return hr;
				setProcessedChanged.emplace(ulProcessedChangeId, std::string(lpData.get(), ulSourceKeySize));
			}
		}
	}

	if (lpulSyncId != nullptr)
		*lpulSyncId = ulSyncId;
	if (lpulChangeId != nullptr)
		*lpulChangeId = ulChangeId;
	m_setProcessedChanges.insert(std::make_move_iterator(setProcessedChanged.begin()), std::make_move_iterator(setProcessedChanged.end()));
	return hrSuccess;
}
