/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <kopano/platform.h>

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
#include <kopano/ECDebug.h>

#include "Mem.h"
#include "ECMessage.h"
#include <kopano/stringutil.h>
#include "ECSyncLog.h"
#include "ECSyncUtil.h"
#include "ECSyncSettings.h"
#include "EntryPoint.h"
#include <kopano/CommonUtil.h>
// We use ntohl/htonl for network-order conversion
#include <arpa/inet.h>
#include <kopano/charset/convert.h>

ECExchangeExportChanges::ECExchangeExportChanges(ECMsgStore *lpStore, const std::string &sk, const wchar_t * szDisplay, unsigned int ulSyncType)
: m_iidMessage(IID_IMessage)
{
	ECSyncLog::GetLogger(&m_lpLogger);

	m_lpStore = lpStore;
	m_sourcekey = sk;
	m_strDisplay = szDisplay ? szDisplay : L"<Unknown>";
	m_ulSyncType = ulSyncType;

	m_bConfiged = false;
	m_lpStream = NULL;
	m_lpImportContents = NULL;
	m_lpImportStreamedContents = NULL;
	m_lpImportHierarchy = NULL;
	m_ulFlags = 0;
	m_ulSyncId = 0;
	m_ulChangeId = 0;
	m_ulStep = 0;
	// In server-side sync, only use a batch size of 1.
	if (m_sourcekey.empty())
		m_ulBatchSize = 1;
	else
		m_ulBatchSize = 256;
	m_ulBufferSize = 0;
	m_ulChanges = 0;
	m_lpChanges = NULL;
	m_lpRestrict = NULL;
	m_ulMaxChangeId = 0;
	m_ulEntryPropTag = PR_SOURCE_KEY; 		// This is normally the tag that is sent to exportMessageChangeAsStream()

	m_clkStart = 0;
	memset(&m_tmsStart, 0, sizeof(m_tmsStart));

	m_lpStore->AddRef();
}

ECExchangeExportChanges::~ECExchangeExportChanges(){
	MAPIFreeBuffer(m_lpChanges);
	if(m_lpStore)
		m_lpStore->Release();

	if(m_lpStream)
		m_lpStream->Release();

	if(m_lpImportContents)
		m_lpImportContents->Release();

	if(m_lpImportStreamedContents)
		m_lpImportStreamedContents->Release();

	if(m_lpImportHierarchy)
		m_lpImportHierarchy->Release();
	MAPIFreeBuffer(m_lpRestrict);
	if(m_lpLogger)
		m_lpLogger->Release();
}

HRESULT ECExchangeExportChanges::SetLogger(ECLogger *lpLogger)
{
	if(m_lpLogger)
		m_lpLogger->Release();

	m_lpLogger = lpLogger;
	if (m_lpLogger != NULL)
		m_lpLogger->AddRef();

	return hrSuccess;
}

HRESULT ECExchangeExportChanges::Create(ECMsgStore *lpStore, REFIID iid, const std::string& sourcekey, const wchar_t *szDisplay, unsigned int ulSyncType, LPEXCHANGEEXPORTCHANGES* lppExchangeExportChanges){
	ECExchangeExportChanges *lpEEC = NULL;

	if (lpStore == NULL || (ulSyncType != ICS_SYNC_CONTENTS && ulSyncType != ICS_SYNC_HIERARCHY))
		return MAPI_E_INVALID_PARAMETER;

	lpEEC = new ECExchangeExportChanges(lpStore, sourcekey, szDisplay, ulSyncType);
	return lpEEC->QueryInterface(iid, reinterpret_cast<void **>(lppExchangeExportChanges));
}

HRESULT	ECExchangeExportChanges::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECExchangeExportChanges, this);
	REGISTER_INTERFACE(IID_ECUnknown, this);

	REGISTER_INTERFACE(IID_IExchangeExportChanges, &this->m_xECExportChanges);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xECExportChanges);

	REGISTER_INTERFACE(IID_IECExportChanges, &this->m_xECExportChanges);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECExchangeExportChanges::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError){
	HRESULT		hr = hrSuccess;
	LPMAPIERROR	lpMapiError = NULL;
	LPTSTR		lpszErrorMsg = NULL;

	//FIXME: give synchronization errors messages
	hr = Util::HrMAPIErrorToText((hResult == hrSuccess)?MAPI_E_NO_ACCESS : hResult, &lpszErrorMsg);
	if (hr != hrSuccess)
		goto exit;

	hr = ECAllocateBuffer(sizeof(MAPIERROR),(void **)&lpMapiError);
	if(hr != hrSuccess)
		goto exit;

	if ((ulFlags & MAPI_UNICODE) == MAPI_UNICODE) {
		std::wstring wstrErrorMsg = convert_to<std::wstring>(lpszErrorMsg);
		std::wstring wstrCompName = convert_to<std::wstring>(g_strProductName.c_str());

		if ((hr = MAPIAllocateMore(sizeof(std::wstring::value_type) * (wstrErrorMsg.size() + 1), lpMapiError, (void**)&lpMapiError->lpszError)) != hrSuccess)
			goto exit;
		wcscpy((wchar_t*)lpMapiError->lpszError, wstrErrorMsg.c_str());

		if ((hr = MAPIAllocateMore(sizeof(std::wstring::value_type) * (wstrCompName.size() + 1), lpMapiError, (void**)&lpMapiError->lpszComponent)) != hrSuccess)
			goto exit;
		wcscpy((wchar_t*)lpMapiError->lpszComponent, wstrCompName.c_str());

	} else {
		std::string strErrorMsg = convert_to<std::string>(lpszErrorMsg);
		std::string strCompName = convert_to<std::string>(g_strProductName.c_str());

		if ((hr = MAPIAllocateMore(strErrorMsg.size() + 1, lpMapiError, (void**)&lpMapiError->lpszError)) != hrSuccess)
			goto exit;
		strcpy((char*)lpMapiError->lpszError, strErrorMsg.c_str());

		if ((hr = MAPIAllocateMore(strCompName.size() + 1, lpMapiError, (void**)&lpMapiError->lpszComponent)) != hrSuccess)
			goto exit;
		strcpy((char*)lpMapiError->lpszComponent, strCompName.c_str());
	}

	lpMapiError->ulContext		= 0;
	lpMapiError->ulLowLevelError= 0;
	lpMapiError->ulVersion		= 0;

	*lppMAPIError = lpMapiError;

exit:
	MAPIFreeBuffer(lpszErrorMsg);
	if( hr != hrSuccess && lpMapiError)
		ECFreeBuffer(lpMapiError);

	return hr;
}

HRESULT ECExchangeExportChanges::Config(LPSTREAM lpStream, ULONG ulFlags, LPUNKNOWN lpCollector, LPSRestriction lpRestriction, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize){
	HRESULT hr;

	ULONG		ulSyncId = 0;
	ULONG		ulChangeId = 0;
	ULONG		ulStep = 0;
	BOOL		bCanStream = FALSE;

	bool		bForceImplicitStateUpdate = false;
	ECSyncSettings *lpSyncSettings = ECSyncSettings::GetInstance();

	typedef std::map<SBinary, ChangeListIter, Util::SBinaryLess>	ChangeMap;
	typedef ChangeMap::iterator					ChangeMapIter;
	ChangeMap		mapChanges;
	ChangeMapIter	iterLastChange;
	ChangeList		lstChange;
	std::string	sourcekey;

	if(m_bConfiged){
		ZLOG_DEBUG(m_lpLogger, "Config() called twice");
		return MAPI_E_UNCONFIGURED;
	}

	if(lpRestriction) {
		hr = Util::HrCopySRestriction(&m_lpRestrict, lpRestriction);
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Invalid restriction");
			return hr;
		}
	} else {
		m_lpRestrict = NULL;
	}

	m_ulFlags = ulFlags;

	if(! (ulFlags & SYNC_CATCHUP)) {
		if(lpCollector == NULL) {
			ZLOG_DEBUG(m_lpLogger, "No importer to export to");
			return MAPI_E_INVALID_PARAMETER;
		}

		// We don't need the importer when doing SYNC_CATCHUP
		if(m_ulSyncType == ICS_SYNC_CONTENTS){
			hr = lpCollector->QueryInterface(IID_IExchangeImportContentsChanges, (LPVOID*) &m_lpImportContents);
			if (hr == hrSuccess && lpSyncSettings->SyncStreamEnabled()) {
				m_lpStore->lpTransport->HrCheckCapabilityFlags(KOPANO_CAP_ENHANCED_ICS, &bCanStream);
				if (bCanStream == TRUE) {
					ZLOG_DEBUG(m_lpLogger, "Exporter supports enhanced ICS, checking importer...");
					hr = lpCollector->QueryInterface(IID_IECImportContentsChanges, (LPVOID*) &m_lpImportStreamedContents);
					if (hr == MAPI_E_INTERFACE_NOT_SUPPORTED) {
						ASSERT(m_lpImportStreamedContents == NULL);
						hr = hrSuccess;
						ZLOG_DEBUG(m_lpLogger, "Importer doesn't support enhanced ICS");
					} else
						ZLOG_DEBUG(m_lpLogger, "Importer supports enhanced ICS");
				} else
					ZLOG_DEBUG(m_lpLogger, "Exporter doesn't support enhanced ICS");
			}
		}else if(m_ulSyncType == ICS_SYNC_HIERARCHY){
			hr = lpCollector->QueryInterface(IID_IExchangeImportHierarchyChanges, (LPVOID*) &m_lpImportHierarchy);
		}else{
			hr = MAPI_E_INVALID_PARAMETER;
		}
		if(hr != hrSuccess)
			return hr;
	}

	if (lpStream == NULL){
		LARGE_INTEGER lint = {{ 0, 0 }};
		ULONG tmp[2] = { 0, 0 };
		ULONG ulSize = 0;

		ZLOG_DEBUG(m_lpLogger, "Creating new exporter stream");
		hr = CreateStreamOnHGlobal(GlobalAlloc(GPTR, sizeof(tmp)), true, &m_lpStream);
		if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to create new exporter stream");
			return hr;
		}

		m_lpStream->Seek(lint, STREAM_SEEK_SET, NULL);
		m_lpStream->Write(tmp, sizeof(tmp), &ulSize);
	} else {
		hr = lpStream->QueryInterface(IID_IStream, (LPVOID*)&m_lpStream);
		if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Passed state stream does not support IStream interface");
			return hr;
		}
	}

	hr = HrDecodeSyncStateStream(m_lpStream, &ulSyncId, &ulChangeId, &m_setProcessedChanges);
	if(hr != hrSuccess) {
		ZLOG_DEBUG(m_lpLogger, "Unable to decode sync state stream, hr=0x%08x", hr);
		return hr;
	}

	ZLOG_DEBUG(m_lpLogger, "Decoded state stream: syncid=%u, changeid=%u, processed changes=%lu", ulSyncId, ulChangeId, (long unsigned int)m_setProcessedChanges.size());

	if(ulSyncId == 0) {
		ZLOG_DEBUG(m_lpLogger, "Getting new sync id for %s folder '%ls'...", (m_lpStore->IsOfflineStore() ? "offline" : "online"), m_strDisplay.c_str());

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
		hr = m_lpStore->lpTransport->HrSetSyncStatus(sourcekey, 0, 0, m_ulSyncType, 0, &ulSyncId);
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to update sync status on server, hr=0x%08x", hr);
			return hr;
		}

		ZLOG_DEBUG(m_lpLogger, "New sync id for %s folder '%ls': %u", (m_lpStore->IsOfflineStore() ? "offline" : "online"), m_strDisplay.c_str(), ulSyncId);

		bForceImplicitStateUpdate = true;
	}

	MAPIFreeBuffer(m_lpChanges);
	m_lpChanges = NULL;

	hr = m_lpStore->lpTransport->HrGetChanges(sourcekey, ulSyncId, ulChangeId, m_ulSyncType, ulFlags, m_lpRestrict, &m_ulMaxChangeId, &m_ulChanges, &m_lpChanges);
	if(hr != hrSuccess) {
		ZLOG_DEBUG(m_lpLogger, "Unable to get changes from server, hr=0x%08x", hr);
		return hr;
	}

	m_ulSyncId = ulSyncId;
	m_ulChangeId = ulChangeId;

	m_lpLogger->Log(EC_LOGLEVEL_INFO, "%s folder: %ls changes: %u syncid: %u changeid: %u", (m_lpStore->IsOfflineStore() ? "offline" : "online"), m_strDisplay.c_str(), m_ulChanges, m_ulSyncId, m_ulChangeId);

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
		if (m_setProcessedChanges.find(std::pair<unsigned int, std::string>(m_lpChanges[ulStep].ulChangeId, std::string((char *)m_lpChanges[ulStep].sSourceKey.lpb, m_lpChanges[ulStep].sSourceKey.cb))) != m_setProcessedChanges.end())
			continue;

		iterLastChange = mapChanges.find(m_lpChanges[ulStep].sSourceKey);
		if (iterLastChange == mapChanges.end()) {
			switch (ICS_ACTION(m_lpChanges[ulStep].ulChangeType)) {
				case ICS_NEW:
				case ICS_CHANGE:
					mapChanges.insert(ChangeMap::value_type(m_lpChanges[ulStep].sSourceKey, lstChange.insert(lstChange.end(), m_lpChanges[ulStep])));
					break;

				case ICS_FLAG:
					mapChanges.insert(ChangeMap::value_type(m_lpChanges[ulStep].sSourceKey, m_lstFlag.insert(m_lstFlag.end(), m_lpChanges[ulStep])));
					break;

				case ICS_SOFT_DELETE:
					mapChanges.insert(ChangeMap::value_type(m_lpChanges[ulStep].sSourceKey, m_lstSoftDelete.insert(m_lstSoftDelete.end(), m_lpChanges[ulStep])));
					break;

				case ICS_HARD_DELETE:
					mapChanges.insert(ChangeMap::value_type(m_lpChanges[ulStep].sSourceKey, m_lstHardDelete.insert(m_lstHardDelete.end(), m_lpChanges[ulStep])));
					break;

				default:
					break;
			}
		} else {
			switch (ICS_ACTION(m_lpChanges[ulStep].ulChangeType)) {
				case ICS_NEW:
					// This shouldn't happen since apparently we have another change for the same object.
					// However, if an object gets moved to another folder and back, we'll get a delete followed by an add. If that happens
					// we skip the delete and morph the current add to a change.
					if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_SOFT_DELETE || ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_HARD_DELETE) {
						ChangeListIter iterNewChange = lstChange.insert(lstChange.end(), *iterLastChange->second);
						iterNewChange->ulChangeType = (iterNewChange->ulChangeType & ~ICS_ACTION_MASK) | ICS_CHANGE;
						if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_SOFT_DELETE)
							m_lstSoftDelete.erase(iterLastChange->second);
						else
							m_lstHardDelete.erase(iterLastChange->second);
						iterLastChange->second = iterNewChange;

						ZLOG_DEBUG(m_lpLogger, "Got an ICS_NEW change for a previously deleted object. I converted it to a change. sourcekey=%s", bin2hex(m_lpChanges[ulStep].sSourceKey.cb, m_lpChanges[ulStep].sSourceKey.lpb).c_str());
					} else
						ZLOG_DEBUG(m_lpLogger, "Got an ICS_NEW change for an object we've seen before. prev_change=%04x, sourcekey=%s", iterLastChange->second->ulChangeType, bin2hex(m_lpChanges[ulStep].sSourceKey.cb, m_lpChanges[ulStep].sSourceKey.lpb).c_str());
					break;

				case ICS_CHANGE:
					// Any change is allowed as the previous change except a change.
					if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_CHANGE)
						ZLOG_DEBUG(m_lpLogger, "Got an ICS_CHANGE on an object for which we just saw an ICS_CHANGE. sourcekey=%s", bin2hex(m_lpChanges[ulStep].sSourceKey.cb, m_lpChanges[ulStep].sSourceKey.lpb).c_str());
					// A previous delete is allowed for the same reason as in ICS_NEW.
					if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_SOFT_DELETE || ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_HARD_DELETE) {
						if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_SOFT_DELETE)
							m_lstSoftDelete.erase(iterLastChange->second);
						else
							m_lstHardDelete.erase(iterLastChange->second);
						iterLastChange->second = lstChange.insert(lstChange.end(), m_lpChanges[ulStep]);
						ZLOG_DEBUG(m_lpLogger, "Got an ICS_CHANGE change for a previously deleted object. sourcekey=%s", bin2hex(m_lpChanges[ulStep].sSourceKey.cb, m_lpChanges[ulStep].sSourceKey.lpb).c_str());
					} else if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_FLAG) {
						m_lstFlag.erase(iterLastChange->second);
						iterLastChange->second = lstChange.insert(lstChange.end(), m_lpChanges[ulStep]);
						ZLOG_DEBUG(m_lpLogger, "Upgraded a previous ICS_FLAG to ICS_CHANGED. sourcekey=%s", bin2hex(m_lpChanges[ulStep].sSourceKey.cb, m_lpChanges[ulStep].sSourceKey.lpb).c_str());
					} else
						ZLOG_DEBUG(m_lpLogger, "Ignoring ICS_CHANGE due to a previous change. prev_change=%04x, sourcekey=%s", iterLastChange->second->ulChangeType, bin2hex(m_lpChanges[ulStep].sSourceKey.cb, m_lpChanges[ulStep].sSourceKey.lpb).c_str());
					break;

				case ICS_FLAG:
					// This is only allowed after an ICS_NEW and ICS_CHANGE. It will be ignored in any case.
					if (ICS_ACTION(iterLastChange->second->ulChangeType) != ICS_NEW && ICS_ACTION(iterLastChange->second->ulChangeType) != ICS_CHANGE)
						ZLOG_DEBUG(m_lpLogger, "Got an ICS_FLAG with something else than a ICS_NEW or ICS_CHANGE as the previous changes. prev_change=%04x, sourcekey=%s", iterLastChange->second->ulChangeType, bin2hex(m_lpChanges[ulStep].sSourceKey.cb, m_lpChanges[ulStep].sSourceKey.lpb).c_str());
					ZLOG_DEBUG(m_lpLogger, "Ignoring ICS_FLAG due to previous ICS_NEW or ICS_CHANGE. prev_change=%04x, sourcekey=%s", iterLastChange->second->ulChangeType, bin2hex(m_lpChanges[ulStep].sSourceKey.cb, m_lpChanges[ulStep].sSourceKey.lpb).c_str());
					break;

				case ICS_SOFT_DELETE:
				case ICS_HARD_DELETE:
					// We'll ignore the previous change and replace it with this delete. We won't write it now as
					// we could get an add for the same object. But because of the reordering (deletes after all adds/changes) we would delete
					// the new object. Therefore we'll make a change out of a delete - add/change.
					ZLOG_DEBUG(m_lpLogger, "Replacing previous change with current ICS_xxxx_DELETE. prev_change=%04x, sourcekey=%s", iterLastChange->second->ulChangeType, bin2hex(m_lpChanges[ulStep].sSourceKey.cb, m_lpChanges[ulStep].sSourceKey.lpb).c_str());
					if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_NEW || ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_CHANGE)
						lstChange.erase(iterLastChange->second);
					else if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_FLAG)
						m_lstFlag.erase(iterLastChange->second);
					else if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_SOFT_DELETE)
						m_lstSoftDelete.erase(iterLastChange->second);
					else if (ICS_ACTION(iterLastChange->second->ulChangeType) == ICS_HARD_DELETE)
						m_lstHardDelete.erase(iterLastChange->second);

					if (ICS_ACTION(m_lpChanges[ulStep].ulChangeType) == ICS_SOFT_DELETE)
						iterLastChange->second = m_lstSoftDelete.insert(m_lstSoftDelete.end(), m_lpChanges[ulStep]);
					else
						iterLastChange->second = m_lstHardDelete.insert(m_lstHardDelete.end(), m_lpChanges[ulStep]);
					break;

				default:
					ZLOG_DEBUG(m_lpLogger, "Got an unknown change. change=%04x, sourcekey=%s", m_lpChanges[ulStep].ulChangeType, bin2hex(m_lpChanges[ulStep].sSourceKey.cb, m_lpChanges[ulStep].sSourceKey.lpb).c_str());
					break;
			}
		}
	}

	m_lstChange.assign(lstChange.begin(), lstChange.end());

	if(ulBufferSize != 0){
		m_ulBufferSize = ulBufferSize;
	}else{
		m_ulBufferSize = 10;
	}

	m_bConfiged = true;

	if (bForceImplicitStateUpdate) {
		ZLOG_DEBUG(m_lpLogger, "Forcing state update for %s folder '%ls'", (m_lpStore->IsOfflineStore() ? "offline" : "online"), m_strDisplay.c_str());
		/**
		 * bForceImplicitStateUpdate is only set to true when the StateStream contained a ulSyncId of 0. In that
		 * case the ulChangeId can't be enything other then 0. As ulChangeId is assigned to m_ulChangeId and neither
		 * of them changes in this function, m_ulChangeId can't be anything other than 0 when we reach this point.
		 **/
		ASSERT(m_ulChangeId == 0);
		UpdateState(NULL);
	}
	return hrSuccess;
}

HRESULT ECExchangeExportChanges::Synchronize(ULONG FAR * lpulSteps, ULONG FAR * lpulProgress){
	HRESULT			hr = hrSuccess;
	PROCESSEDCHANGESSET::const_iterator iterProcessedChange;

	if(!m_bConfiged){
		ZLOG_DEBUG(m_lpLogger, "Config() not called before Synchronize()");
		return MAPI_E_UNCONFIGURED;
	}

	if(m_ulFlags & SYNC_CATCHUP){
		m_ulChangeId = m_ulMaxChangeId > m_ulChangeId ? m_ulMaxChangeId : m_ulChangeId;
		hr = UpdateStream(m_lpStream);
		if (hr == hrSuccess)
			*lpulProgress = *lpulSteps = 0;
		return hr;
	}

	if (*lpulProgress == 0 && m_lpLogger->Log(EC_LOGLEVEL_DEBUG))
		m_clkStart = z_times(&m_tmsStart);

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
		if(m_ulSyncType == ICS_SYNC_CONTENTS){
			hr = m_lpImportContents->UpdateState(NULL);
		}else{
			hr = m_lpImportHierarchy->UpdateState(NULL);
		}
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Importer state update failed, hr=0x%08x", hr);
			return hr;
		}
	}

progress:
	if(hr == hrSuccess) {
		// The sync has completed. This means we can move to the next change ID on the server, and clear our
		// processed change list. If we can't save the new state the the server, we just keep the previous
		// change ID and the (large) change list. This allows us always to have a state, even when we can't
		// communicate with the server.
		if(m_lpStore->lpTransport->HrSetSyncStatus(m_sourcekey, m_ulSyncId, m_ulMaxChangeId, m_ulSyncType, 0, &m_ulSyncId) == hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Done: syncid=%u, changeid=%u/%u", m_ulSyncId, m_ulChangeId, m_ulMaxChangeId);

			m_ulChangeId = m_ulMaxChangeId;
			m_setProcessedChanges.clear();

			if(m_ulChanges) {
				if (m_lpLogger->Log(EC_LOGLEVEL_DEBUG)) {
					struct z_tms	tmsEnd = {0};
					clock_t		clkEnd = z_times(&tmsEnd);
					double		dblDuration = 0;
					char		szDuration[64] = {0};

					// Calculate diff
					dblDuration = (double)(clkEnd - m_clkStart) / TICKS_PER_SEC;
					if (dblDuration >= 60)
						_snprintf(szDuration, sizeof(szDuration), "%u:%02u.%03u min.", (unsigned)(dblDuration / 60), (unsigned)dblDuration % 60, (unsigned)(dblDuration * 1000 + .5) % 1000);
					else
						_snprintf(szDuration, sizeof(szDuration), "%u.%03u s.", (unsigned)dblDuration % 60, (unsigned)(dblDuration * 1000 + .5) % 1000);

					m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "folder changes synchronized in %s", szDuration);
				} else
					m_lpLogger->Log(EC_LOGLEVEL_INFO, "folder changes synchronized");
			}
		}
	}

	*lpulSteps = m_lstChange.size();
	*lpulProgress = m_ulStep;
	return hr;
}

HRESULT ECExchangeExportChanges::UpdateState(LPSTREAM lpStream){
	if(!m_bConfiged){
		ZLOG_DEBUG(m_lpLogger, "Config() not called before UpdateState()");
		return MAPI_E_UNCONFIGURED;
	}

	if(lpStream == NULL) {
		lpStream = m_lpStream;
	}

	return UpdateStream(lpStream);
}

HRESULT ECExchangeExportChanges::GetChangeCount(ULONG *lpcChanges) {
	ULONG cChanges = 0;

	if(!m_bConfiged){
		ZLOG_DEBUG(m_lpLogger, "Config() not called before GetChangeCount()");
		return MAPI_E_UNCONFIGURED;
	}

	// Changes in flags or deletions are only one step all together
	if(!m_lstHardDelete.empty() || !m_lstSoftDelete.empty() || !m_lstFlag.empty())
		++cChanges;
	cChanges += m_lstChange.size();

	*lpcChanges = cChanges;
	return hrSuccess;
}

/**
 * This allows you to configure the exporter for a selective export of messages. Since the caller
 * specifies which items to export, it is not an incremental export and it has no state. It will call
 * the importer with data for all the specified messages, unless they are unavailable. This means that
 * the importer will only receive ImportMessageChange() or ImportMessageChangeAsStream() calls.
 *
 * @param[in] ulPropTag 		Property tag of identifiers in lpEntries, either PR_SOURCE_KEY or PR_ENTRYID
 * @param[in] lpEntries 		List of entries to export
 * @param[in] lpParents			List of parents for entries in lpEntries. Must be the same size as lpEntries. Must be NULL if ulPropTag == PR_ENTRYID.
 * @param[in] ulFlags   		Unused for now
 * @param[in] lpCollector		Importer to send the data to. Must implement either IExchangeImportContentsChanges or IECImportContentsChanges
 * @param[in] lpIncludeProps	Unused for now
 * @param[in] lpExcludeProps	Unused for now
 * @param[in] ulBufferSize		Number of messages so synchronize during a single Synchronize() call. A value of 0 means 'default', which is 1
 * @return result
 */
HRESULT ECExchangeExportChanges::ConfigSelective(ULONG ulPropTag, LPENTRYLIST lpEntries, LPENTRYLIST lpParents, ULONG ulFlags, LPUNKNOWN lpCollector, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize)
{
	HRESULT hr;
	ECSyncSettings *lpSyncSettings = ECSyncSettings::GetInstance();
	BOOL bCanStream = false;
	BOOL bSupportsPropTag = false;
	
	if (ulPropTag != PR_ENTRYID && ulPropTag != PR_SOURCE_KEY)
		return MAPI_E_INVALID_PARAMETER;
	
	if(ulPropTag == PR_ENTRYID) {
		m_lpStore->lpTransport->HrCheckCapabilityFlags(KOPANO_CAP_EXPORT_PROPTAG, &bSupportsPropTag);
		if (!bSupportsPropTag)
			return MAPI_E_NO_SUPPORT;
	}
	
	if (ulPropTag == PR_ENTRYID && lpParents != NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (ulPropTag == PR_SOURCE_KEY && lpParents == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (lpParents != NULL && lpParents->cValues != lpEntries->cValues)
		return MAPI_E_INVALID_PARAMETER;
	
	if(m_bConfiged){
		ZLOG_DEBUG(m_lpLogger, "Config() called twice");
		return MAPI_E_UNCONFIGURED;
	}
	
	// Only available for message syncing	
	if (m_ulSyncType != ICS_SYNC_CONTENTS)
		return MAPI_E_NO_SUPPORT;

	// Select an importer interface
	hr = lpCollector->QueryInterface(IID_IExchangeImportContentsChanges, (LPVOID*) &m_lpImportContents);
	if (hr == hrSuccess && lpSyncSettings->SyncStreamEnabled()) {
		m_lpStore->lpTransport->HrCheckCapabilityFlags(KOPANO_CAP_ENHANCED_ICS, &bCanStream);
		if (bCanStream == TRUE) {
			ZLOG_DEBUG(m_lpLogger, "Exporter supports enhanced ICS, checking importer...");
			hr = lpCollector->QueryInterface(IID_IECImportContentsChanges, (LPVOID*) &m_lpImportStreamedContents);
			if (hr == MAPI_E_INTERFACE_NOT_SUPPORTED) {
				ASSERT(m_lpImportStreamedContents == NULL);
				hr = hrSuccess;
				ZLOG_DEBUG(m_lpLogger, "Importer doesn't support enhanced ICS");
			} else
				ZLOG_DEBUG(m_lpLogger, "Importer supports enhanced ICS");
		} else
			ZLOG_DEBUG(m_lpLogger, "Exporter doesn't support enhanced ICS");
	}
	
	m_ulEntryPropTag = ulPropTag;

	// Fill m_lpChanges with items from lpEntries
	hr = MAPIAllocateBuffer(sizeof(ICSCHANGE) * lpEntries->cValues, (void **)&m_lpChanges);
	if(hr != hrSuccess)
		return hr;

	for (unsigned int i = 0; i < lpEntries->cValues; ++i) {
		memset(&m_lpChanges[i], 0, sizeof(ICSCHANGE));
		hr = MAPIAllocateMore(lpEntries->lpbin[i].cb, m_lpChanges, (void **)&m_lpChanges[i].sSourceKey.lpb);
		if(hr != hrSuccess)
			return hr;
			
		memcpy(m_lpChanges[i].sSourceKey.lpb, lpEntries->lpbin[i].lpb, lpEntries->lpbin[i].cb);
		m_lpChanges[i].sSourceKey.cb = lpEntries->lpbin[i].cb;
		
		if(lpParents) {
			hr = MAPIAllocateMore(lpParents->lpbin[i].cb, m_lpChanges, (void **)&m_lpChanges[i].sParentSourceKey.lpb);
			if(hr != hrSuccess)
				return hr;
				
			memcpy(m_lpChanges[i].sParentSourceKey.lpb, lpParents->lpbin[i].lpb, lpParents->lpbin[i].cb);
			m_lpChanges[i].sParentSourceKey.cb = lpParents->lpbin[i].cb;
		}
		
		m_lpChanges[i].ulChangeType = ICS_MESSAGE_NEW;

		// Since all changes are 'change' modifications, duplicate all data in m_lpChanges in m_lstChange
		m_lstChange.push_back(m_lpChanges[i]);
	}
	
	m_bConfiged = true;
	return hrSuccess;
}

HRESULT ECExchangeExportChanges::SetMessageInterface(REFIID refiid) {
	m_iidMessage = refiid;
	return hrSuccess;
}

ULONG ECExchangeExportChanges::xECExportChanges::AddRef(){
	TRACE_MAPI(TRACE_ENTRY, "IExchangeExportChanges::AddRef", "");
	METHOD_PROLOGUE_(ECExchangeExportChanges, ECExportChanges);
	return pThis->AddRef();
}

ULONG ECExchangeExportChanges::xECExportChanges::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "IExchangeExportChanges::Release", "");
	METHOD_PROLOGUE_(ECExchangeExportChanges, ECExportChanges);
	return pThis->Release();
}

HRESULT ECExchangeExportChanges::xECExportChanges::QueryInterface(REFIID refiid, void **lppInterface)
{
	HRESULT hr;
	TRACE_MAPI(TRACE_ENTRY, "IExchangeExportChanges::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECExchangeExportChanges, ECExportChanges);
	hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IExchangeExportChanges::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECExchangeExportChanges::xECExportChanges::GetLastError(HRESULT hError, ULONG ulFlags, LPMAPIERROR * lppMapiError)
{
	HRESULT hr;
	TRACE_MAPI(TRACE_ENTRY, "IExchangeExportChanges::GetLastError", "");
	METHOD_PROLOGUE_(ECExchangeExportChanges, ECExportChanges);
	hr = pThis->GetLastError(hError, ulFlags, lppMapiError);
	TRACE_MAPI(TRACE_RETURN, "IExchangeExportChanges::GetLastError", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECExchangeExportChanges::xECExportChanges::Config(LPSTREAM lpStream, ULONG ulFlags, LPUNKNOWN lpCollector, LPSRestriction lpRestriction, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize){
	HRESULT hr;
	TRACE_MAPI(TRACE_ENTRY, "IExchangeExportChanges::Config", "lpStream=%08x, ulFlags=%d, lpCollector=0x%08x, lpRestriction = %08x, lpIncludeProps = %08x, lpExcludeProps = %08x, ulBufferSize = %d", lpStream, ulFlags, lpCollector, lpRestriction, lpIncludeProps, lpExcludeProps, ulBufferSize);
	METHOD_PROLOGUE_(ECExchangeExportChanges, ECExportChanges);
	hr =  pThis->Config(lpStream, ulFlags, lpCollector, lpRestriction, lpIncludeProps, lpExcludeProps, ulBufferSize);
	TRACE_MAPI(TRACE_RETURN, "IExchangeExportChanges::Config", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECExchangeExportChanges::xECExportChanges::ConfigSelective(ULONG ulPropTag, LPENTRYLIST lpEntries, LPENTRYLIST lpParents, ULONG ulFlags, LPUNKNOWN lpCollector, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize)
{
	HRESULT hr;
	TRACE_MAPI(TRACE_ENTRY, "IExchangeExportChanges::ConfigSelective", "");
	METHOD_PROLOGUE_(ECExchangeExportChanges, ECExportChanges);
	hr =  pThis->ConfigSelective(ulPropTag, lpEntries, lpParents, ulFlags, lpCollector, lpIncludeProps, lpExcludeProps, ulBufferSize);
	TRACE_MAPI(TRACE_RETURN, "IExchangeExportChanges::ConfigSelective", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECExchangeExportChanges::xECExportChanges::Synchronize(ULONG FAR * pulSteps, ULONG FAR * pulProgress){
	HRESULT hr;
	TRACE_MAPI(TRACE_ENTRY, "IExchangeExportChanges::Synchronize", "");
	METHOD_PROLOGUE_(ECExchangeExportChanges, ECExportChanges);
	hr = pThis->Synchronize(pulSteps, pulProgress);
	TRACE_MAPI(TRACE_RETURN, "IExchangeExportChanges::Synchronize", "%s", (hr != SYNC_W_PROGRESS) ? GetMAPIErrorDescription(hr).c_str() : "SYNC_W_PROGRESS");
	return hr;
}

HRESULT ECExchangeExportChanges::xECExportChanges::UpdateState(LPSTREAM lpStream){
	HRESULT hr;
	TRACE_MAPI(TRACE_ENTRY, "IExchangeExportChanges::UpdateState", "");
	METHOD_PROLOGUE_(ECExchangeExportChanges, ECExportChanges);
	hr = pThis->UpdateState(lpStream);
	TRACE_MAPI(TRACE_RETURN, "IExchangeExportChanges::UpdateState", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECExchangeExportChanges::xECExportChanges::GetChangeCount(ULONG *lpcChanges) {
	TRACE_MAPI(TRACE_ENTRY, "IECExportChanges::GetChangeCount", "");
	METHOD_PROLOGUE_(ECExchangeExportChanges, ECExportChanges);
	return pThis->GetChangeCount(lpcChanges);
}

HRESULT ECExchangeExportChanges::xECExportChanges::SetMessageInterface(REFIID refiid)
{
	TRACE_MAPI(TRACE_ENTRY, "IECExportChanges::SetMessageInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECExchangeExportChanges, ECExportChanges);
	return pThis->SetMessageInterface(refiid);
}

HRESULT ECExchangeExportChanges::xECExportChanges::SetLogger(ECLogger *lpLogger)
{
	TRACE_MAPI(TRACE_ENTRY, "IECExportChanges::SetLogger", "");
	METHOD_PROLOGUE_(ECExchangeExportChanges, ECExportChanges);
	return pThis->SetLogger(lpLogger);
}

HRESULT ECExchangeExportChanges::ExportMessageChanges() {
	ASSERT(m_lpImportContents != NULL);
	if (m_lpImportStreamedContents != NULL)
		return ExportMessageChangesFast();
	else
		return ExportMessageChangesSlow();
}

HRESULT ECExchangeExportChanges::ExportMessageChangesSlow() {
	HRESULT			hr = hrSuccess;

	LPMESSAGE		lpSourceMessage = NULL;
	LPATTACH		lpSourceAttach = NULL;

	LPMESSAGE		lpDestMessage = NULL;
	LPATTACH		lpDestAttach = NULL;

	LPMAPITABLE		lpTable = NULL;
	LPSRowSet		lpRows = NULL;

	LPSPropValue	lpPropArray = NULL;

	LPSPropTagArray lpPropTagArray = NULL;

	ULONG			ulObjType;
	ULONG			ulCount;
	ULONG			ulFlags;
	ULONG			ulSteps = 0;

	ULONG			cbEntryID = 0;
	LPENTRYID		lpEntryID = NULL;

	SizedSPropTagArray(5, sptMessageExcludes) = { 5, {
		PR_MESSAGE_SIZE,
		PR_MESSAGE_RECIPIENTS,
		PR_MESSAGE_ATTACHMENTS,
		PR_ATTACH_SIZE,
		PR_PARENT_SOURCE_KEY
	} };

	SizedSPropTagArray(7, sptImportProps) = { 7, {
		PR_SOURCE_KEY,
		PR_LAST_MODIFICATION_TIME,
		PR_CHANGE_KEY,
		PR_PARENT_SOURCE_KEY,
		PR_PREDECESSOR_CHANGE_LIST,
		PR_ENTRYID,
		PR_ASSOCIATED
	} };
	SizedSPropTagArray(1, sptAttach) = { 1, {PR_ATTACH_NUM} };

	while(m_ulStep < m_lstChange.size() && (m_ulBufferSize == 0 || ulSteps < m_ulBufferSize)){
		ulFlags = 0;
		if((m_lstChange.at(m_ulStep).ulChangeType & ICS_ACTION_MASK) == ICS_NEW){
			ulFlags |= SYNC_NEW_MESSAGE;
		}

		if(!m_sourcekey.empty()) {
			// Normal exporter, get the import properties we need by opening the source message

			hr = m_lpStore->EntryIDFromSourceKey(
				m_lstChange.at(m_ulStep).sParentSourceKey.cb,
				m_lstChange.at(m_ulStep).sParentSourceKey.lpb,
				m_lstChange.at(m_ulStep).sSourceKey.cb,
				m_lstChange.at(m_ulStep).sSourceKey.lpb,
				&cbEntryID, &lpEntryID);
			if(hr == MAPI_E_NOT_FOUND){
				hr = hrSuccess;
				goto next;
			}
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "change sourcekey: %s", bin2hex(m_lstChange.at(m_ulStep).sSourceKey.cb, m_lstChange.at(m_ulStep).sSourceKey.lpb).c_str());

			if(hr != hrSuccess) {
				ZLOG_DEBUG(m_lpLogger, "Error while getting entryid from sourcekey %s", bin2hex(m_lstChange.at(m_ulStep).sSourceKey.cb, m_lstChange.at(m_ulStep).sSourceKey.lpb).c_str());
				goto exit;
			}

			hr = m_lpStore->OpenEntry(cbEntryID, lpEntryID, &m_iidMessage, MAPI_MODIFY, &ulObjType, (LPUNKNOWN*) &lpSourceMessage);
			if(hr == MAPI_E_NOT_FOUND){
				hr = hrSuccess;
				goto next;
			}
			if(hr != hrSuccess) {
				ZLOG_DEBUG(m_lpLogger, "Unable to open message with entryid %s", bin2hex(cbEntryID, (unsigned char *)lpEntryID).c_str());
				goto exit;
			}
			hr = lpSourceMessage->GetProps((LPSPropTagArray)&sptImportProps, 0, &ulCount, &lpPropArray);
			if(FAILED(hr)) {
				ZLOG_DEBUG(m_lpLogger, "Unable to get properties from source message");
				goto exit;
			}

			hr = m_lpImportContents->ImportMessageChange(ulCount, lpPropArray, ulFlags, &lpDestMessage);
		} else {
			// Server-wide ICS exporter, only export source key and parent source key
			SPropValue sProps[2];

			sProps[0].ulPropTag = PR_SOURCE_KEY;
			sProps[0].Value.bin = m_lstChange.at(m_ulStep).sSourceKey; // cheap copy is ok since pointer is valid during ImportMessageChange call
			sProps[1].ulPropTag = PR_PARENT_SOURCE_KEY;
			sProps[1].Value.bin = m_lstChange.at(m_ulStep).sParentSourceKey;

			hr = m_lpImportContents->ImportMessageChange(2, sProps, ulFlags, &lpDestMessage);
		}

		if (hr == SYNC_E_IGNORE) {
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "ignored change");
			// Mark this change as processed

			hr = hrSuccess;
			goto next;
		}else if(hr == SYNC_E_OBJECT_DELETED){
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "ignored change for deleted item");

			// Mark this change as processed
			hr = hrSuccess;
			goto next;
		}else if(hr == SYNC_E_INVALID_PARAMETER){
			//exchange doesn't like our input sometimes
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "ignored change parameter");
			hr = hrSuccess;
			goto next;
		// TODO handle SYNC_E_OBJECT_DELETED, SYNC_E_CONFLICT, SYNC_E_NO_PARENT, SYNC_E_INCEST, SYNC_E_UNSYNCHRONIZED
		}else if(hr != hrSuccess){
			//m_lpLogger->Log(EC_LOGLEVEL_INFO, "change error: %s", stringify(hr, true).c_str());
			ZLOG_DEBUG(m_lpLogger, "Error during message import");
			goto exit;
		}

		if(lpDestMessage == NULL) {
			// Import succeeded, but we have no message to export to. Treat this the same as
			// SYNC_E_IGNORE. This is required for the BES ICS exporter
			goto next;
		}

		hr = lpSourceMessage->CopyTo(0, NULL, (LPSPropTagArray)&sptMessageExcludes, 0, NULL, &IID_IMessage, lpDestMessage, 0, NULL);
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to copy to imported message");
			goto exit;
		}

		hr = lpSourceMessage->GetRecipientTable(0, &lpTable);
		if(hr !=  hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to read source message's recipient table");
			goto exit;
		}

		hr = lpTable->QueryColumns(TBL_ALL_COLUMNS, &lpPropTagArray);
		if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to get column set from source message's recipient table");
			goto exit;
		}

		hr = lpTable->SetColumns(lpPropTagArray, 0);
		if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to set column set for source message's recipient table");
			goto exit;
		}

		hr = lpTable->QueryRows(0xFFFF, 0, &lpRows);
		if(hr !=  hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to read recipients from source message");
			goto exit;
		}

		//FIXME: named property in the recipienttable ?

		hr = lpDestMessage->ModifyRecipients(0, (LPADRLIST)lpRows);
		if(hr !=  hrSuccess)
			hr = hrSuccess;
			//goto exit;

		if(lpRows){
			FreeProws(lpRows);
			lpRows = NULL;
		}

		if(lpTable){
			lpTable->Release();
			lpTable = NULL;
		}

		//delete every attachment
		hr = lpDestMessage->GetAttachmentTable(0, &lpTable);
		if(hr !=  hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to get destination's attachment table");
			goto exit;
		}

		hr = lpTable->SetColumns((LPSPropTagArray)&sptAttach, 0);
		if(hr !=  hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to set destination's attachment table's column set");
			goto exit;
		}

		hr = lpTable->QueryRows(0xFFFF, 0, &lpRows);
		if(hr !=  hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to read destination's attachment list");
			goto exit;
		}

		for (ulCount = 0; ulCount < lpRows->cRows; ++ulCount) {
			hr = lpDestMessage->DeleteAttach(lpRows->aRow[ulCount].lpProps[0].Value.ul, 0, NULL, 0);
			if(hr != hrSuccess) {
				ZLOG_DEBUG(m_lpLogger, "Unable to delete destination's attachment number %d", lpRows->aRow[ulCount].lpProps[0].Value.ul);
				goto exit;
			}
		}

		if(lpRows){
			FreeProws(lpRows);
			lpRows = NULL;
		}

		if(lpTable){
			lpTable->Release();
			lpTable = NULL;
		}

		//add every attachment
		hr = lpSourceMessage->GetAttachmentTable(0, &lpTable);
		if(hr !=  hrSuccess)
			goto exit;

		hr = lpTable->SetColumns((LPSPropTagArray)&sptAttach, 0);
		if(hr !=  hrSuccess)
			goto exit;

		hr = lpTable->QueryRows(0xFFFF, 0, &lpRows);
		if(hr !=  hrSuccess)
			goto exit;

		for (ulCount = 0; ulCount < lpRows->cRows; ++ulCount) {
			hr = lpSourceMessage->OpenAttach(lpRows->aRow[ulCount].lpProps[0].Value.ul, &IID_IAttachment, 0, &lpSourceAttach);
			if(hr !=  hrSuccess) {
				ZLOG_DEBUG(m_lpLogger, "Unable to open attachment %d in source message", lpRows->aRow[ulCount].lpProps[0].Value.ul);
				goto exit;
			}

			hr = lpDestMessage->CreateAttach(&IID_IAttachment, 0, &ulObjType, &lpDestAttach);
			if(hr !=  hrSuccess) {
				ZLOG_DEBUG(m_lpLogger, "Unable to create attachment");
				goto exit;
			}

			hr = lpSourceAttach->CopyTo(0, NULL, (LPSPropTagArray)&sptAttach, 0, NULL, &IID_IAttachment, lpDestAttach, 0, NULL);
			if(hr !=  hrSuccess) {
				ZLOG_DEBUG(m_lpLogger, "Unable to copy attachment");
				goto exit;
			}

			hr = lpDestAttach->SaveChanges(0);
			if(hr !=  hrSuccess) {
				ZLOG_DEBUG(m_lpLogger, "SaveChanges() failed for destination attachment");
				goto exit;
			}

			if(lpSourceAttach){
				lpSourceAttach->Release();
				lpSourceAttach = NULL;
			}

			lpDestAttach->Release();
			lpDestAttach = NULL;
		}

		if(lpRows){
			FreeProws(lpRows);
			lpRows = NULL;
		}

		if(lpTable){
			lpTable->Release();
			lpTable = NULL;
		}

		MAPIFreeBuffer(lpPropTagArray);
		lpPropTagArray = NULL;

		hr = lpSourceMessage->GetPropList(0, &lpPropTagArray);
		if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to get property list of source message");
			goto exit;
		}

		hr = Util::HrDeleteResidualProps(lpDestMessage, lpSourceMessage, lpPropTagArray);
		if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to remove old properties from destination message");
			goto exit;
		}

		hr = lpDestMessage->SaveChanges(0);
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "SaveChanges failed for destination message");
			goto exit;
		}

next:
		// Mark this change as processed, even if we skipped it due to SYNC_E_IGNORE or because the item was deleted on the source server

		m_setProcessedChanges.insert(std::pair<unsigned int, std::string>(m_lstChange.at(m_ulStep).ulChangeId, std::string((char *)m_lstChange.at(m_ulStep).sSourceKey.lpb, m_lstChange.at(m_ulStep).sSourceKey.cb)));

		MAPIFreeBuffer(lpEntryID);
		lpEntryID = NULL;

		if(lpRows){
			FreeProws(lpRows);
			lpRows = NULL;
		}

		if(lpTable){
			lpTable->Release();
			lpTable = NULL;
		}

		MAPIFreeBuffer(lpPropArray);
		lpPropArray = NULL;

		if(lpSourceAttach){
			lpSourceAttach->Release();
			lpSourceAttach = NULL;
		}

		if(lpDestAttach){
			lpDestAttach->Release();
			lpDestAttach = NULL;
		}

		if(lpSourceMessage){
			lpSourceMessage->Release();
			lpSourceMessage = NULL;
		}

		if(lpDestMessage){
			lpDestMessage->Release();
			lpDestMessage = NULL;
		}

		MAPIFreeBuffer(lpPropTagArray);
		lpPropTagArray = NULL;
		++m_ulStep;
		++ulSteps;
	}

	if(m_ulStep < m_lstChange.size())
		hr = SYNC_W_PROGRESS;
exit:
	if(hr != hrSuccess && hr != SYNC_W_PROGRESS)
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "change error: %s", stringify(hr, true).c_str());
	MAPIFreeBuffer(lpEntryID);
	if(lpRows)
		FreeProws(lpRows);

	if(lpTable)
		lpTable->Release();

	if(lpDestAttach)
		lpDestAttach->Release();

	if(lpDestMessage)
		lpDestMessage->Release();

	if(lpSourceAttach)
		lpSourceAttach->Release();

	if(lpSourceMessage)
		lpSourceMessage->Release();
	MAPIFreeBuffer(lpPropArray);
	MAPIFreeBuffer(lpPropTagArray);
	return hr;
}

HRESULT ECExchangeExportChanges::ExportMessageChangesFast()
{
	HRESULT hr = hrSuccess;
	WSSerializedMessagePtr ptrSerializedMessage;
	ULONG cbProps = 0;
	SPropValuePtr ptrProps;
	LPSPropValue lpPropVal = NULL;
	ULONG ulFlags = 0;
	StreamPtr ptrDestStream;

	SizedSPropTagArray(11, sptImportProps) = { 11, {
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

	SizedSPropTagArray(7, sptImportPropsServerWide) = { 7, {
		PR_SOURCE_KEY,
		PR_PARENT_SOURCE_KEY,
		PR_STORE_RECORD_KEY,
		PR_STORE_ENTRYID, 
		PR_EC_HIERARCHYID,
		PR_EC_PARENT_HIERARCHYID,
		PR_ENTRYID
	} };

	LPSPropTagArray lpImportProps = m_sourcekey.empty() ? (LPSPropTagArray)&sptImportPropsServerWide : (LPSPropTagArray)&sptImportProps;

	// No more changes (add/modify).
	ZLOG_DEBUG(m_lpLogger, "ExportFast: At step %u, changeset contains %lu items)",
		m_ulStep, static_cast<unsigned long>(m_lstChange.size()));
	if (m_ulStep >= m_lstChange.size())
		goto exit;

	if (!m_ptrStreamExporter || m_ptrStreamExporter->IsDone()) {
		ZLOG_DEBUG(m_lpLogger, "ExportFast: Requesting new batch, batch size = %u", m_ulBatchSize);
		hr = m_lpStore->ExportMessageChangesAsStream(m_ulFlags & (SYNC_BEST_BODY | SYNC_LIMITED_IMESSAGE), m_ulEntryPropTag, m_lstChange, m_ulStep, m_ulBatchSize, lpImportProps, &m_ptrStreamExporter);
		if (hr == MAPI_E_UNABLE_TO_COMPLETE) {
			// There was nothing to export (see ExportMessageChangesAsStream documentation)
			assert(m_ulStep >= m_lstChange.size());	// @todo: Is this a correct assumption?
			hr = hrSuccess;
			goto exit;
		} else if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "ExportFast: %s", "Stream export failed");
			goto exit;
		}
		ZLOG_DEBUG(m_lpLogger, "ExportFast: %s", "Got new batch");
	}

	ZLOG_DEBUG(m_lpLogger, "ExportFast: Requesting serialized message, step = %u", m_ulStep);
	hr = m_ptrStreamExporter->GetSerializedMessage(m_ulStep, &ptrSerializedMessage);
	if (hr == SYNC_E_OBJECT_DELETED) {
		ZLOG_DEBUG(m_lpLogger, "ExportFast: %s", "Source message is deleted");
		hr = hrSuccess;
		goto skip;
	} else if (hr != hrSuccess) {
		ZLOG_DEBUG(m_lpLogger, "ExportFast: Unable to get serialized message, hr = 0x%08x", hr);
		goto exit;
	}

	hr = ptrSerializedMessage->GetProps(&cbProps, &ptrProps);
	if (hr != hrSuccess) {
		ZLOG_DEBUG(m_lpLogger, "ExportFast: %s", "Unable to get required properties from serialized message");
		goto exit;
	}

	lpPropVal = PpropFindProp(ptrProps, cbProps, PR_MESSAGE_FLAGS);
	if (lpPropVal != NULL && (lpPropVal->Value.ul & MSGFLAG_ASSOCIATED))
		ulFlags |= SYNC_ASSOCIATED;
	if ((m_lstChange.at(m_ulStep).ulChangeType & ICS_ACTION_MASK) == ICS_NEW)
		ulFlags |= SYNC_NEW_MESSAGE;

	ZLOG_DEBUG(m_lpLogger, "ExportFast: %s", "Importing message change");
	hr = m_lpImportStreamedContents->ImportMessageChangeAsAStream(cbProps, ptrProps, ulFlags, &ptrDestStream);
	if (hr == hrSuccess) {
		ZLOG_DEBUG(m_lpLogger, "ExportFast: %s", "Copying data");
		hr = ptrSerializedMessage->CopyData(ptrDestStream);
		if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "ExportFast: Failed to copy data, hr = 0x%08x", hr);
			LogMessageProps(EC_LOGLEVEL_DEBUG, cbProps, ptrProps);
			goto exit;
		}
		ZLOG_DEBUG(m_lpLogger, "ExportFast: %s", "Copied data");
	} else if (hr == SYNC_E_IGNORE || hr == SYNC_E_OBJECT_DELETED) {
		ZLOG_DEBUG(m_lpLogger, "ExportFast: Change ignored, code = 0x%08x", hr);
		hr = ptrSerializedMessage->DiscardData();
		if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "ExportFast: Failed to discard data, hr = 0x%08x", hr);
			LogMessageProps(EC_LOGLEVEL_DEBUG, cbProps, ptrProps);
			goto exit;
		}
	} else {
		ZLOG_DEBUG(m_lpLogger, "ExportFast: Import failed, hr = 0x%08x", hr);
		LogMessageProps(EC_LOGLEVEL_DEBUG, cbProps, ptrProps);
		goto exit;
	}

skip:
	m_setProcessedChanges.insert(std::pair<unsigned int, std::string>(m_lstChange.at(m_ulStep).ulChangeId, std::string((char *)m_lstChange.at(m_ulStep).sSourceKey.lpb, m_lstChange.at(m_ulStep).sSourceKey.cb)));
	if (++m_ulStep < m_lstChange.size())
		hr = SYNC_W_PROGRESS;

exit:
	if (FAILED(hr))
		m_ptrStreamExporter.reset();

	ZLOG_DEBUG(m_lpLogger, "ExportFast: Done, hr = 0x%08x", hr);
	return hr;
}

HRESULT ECExchangeExportChanges::ExportMessageFlags(){
	HRESULT			hr = hrSuccess;
	LPREADSTATE		lpReadState = NULL;
	ULONG			ulCount;
	std::list<ICSCHANGE>::const_iterator lpChange;

	if(m_lstFlag.empty())
		goto exit;

	if ((hr = MAPIAllocateBuffer(sizeof(READSTATE) * m_lstFlag.size(), (LPVOID *)&lpReadState)) != hrSuccess)
		goto exit;

	ulCount = 0;
	for (lpChange = m_lstFlag.begin(); lpChange != m_lstFlag.end(); ++lpChange) {
		if ((hr = MAPIAllocateMore(lpChange->sSourceKey.cb, lpReadState, (LPVOID *)&lpReadState[ulCount].pbSourceKey)) != hrSuccess)
			goto exit;
		lpReadState[ulCount].cbSourceKey = lpChange->sSourceKey.cb;
		memcpy(lpReadState[ulCount].pbSourceKey, lpChange->sSourceKey.lpb, lpChange->sSourceKey.cb );
		lpReadState[ulCount].ulFlags = lpChange->ulFlags;
		++ulCount;
	}

	if(ulCount > 0){
		hr = m_lpImportContents->ImportPerUserReadStateChange(ulCount, lpReadState);
		if (hr == SYNC_E_IGNORE){
			hr = hrSuccess;
		}
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Read state change failed");
			goto exit;
		}

		// Mark the flag changes as processed
		for (lpChange = m_lstFlag.begin(); lpChange != m_lstFlag.end(); ++lpChange)
			m_setProcessedChanges.insert(std::pair<unsigned int, std::string>(lpChange->ulChangeId, std::string((char *)lpChange->sSourceKey.lpb, lpChange->sSourceKey.cb)));
	}

exit:
	if (hr != hrSuccess)
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to sync message flags, 0x%08X", hr);
	MAPIFreeBuffer(lpReadState);
	return hr;
}

HRESULT ECExchangeExportChanges::ExportMessageDeletes(){
	HRESULT			hr = hrSuccess;

	LPENTRYLIST		lpEntryList = NULL;

	if(!m_lstSoftDelete.empty()){
		hr = ChangesToEntrylist(&m_lstSoftDelete, &lpEntryList);
		if(hr != hrSuccess)
			goto exit;

		hr = m_lpImportContents->ImportMessageDeletion(SYNC_SOFT_DELETE, lpEntryList);
		if (hr == SYNC_E_IGNORE){
			hr = hrSuccess;
		}
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Message deletion import failed");
			goto exit;
		}

		hr = AddProcessedChanges(m_lstSoftDelete);
		if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to add processed soft deletion changes");
			goto exit;
		}
	}

	MAPIFreeBuffer(lpEntryList);
	lpEntryList = NULL;

	if(!m_lstHardDelete.empty()){
		hr = ChangesToEntrylist(&m_lstHardDelete, &lpEntryList);
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to create entry list");
			goto exit;
		}

		hr = m_lpImportContents->ImportMessageDeletion(0, lpEntryList);
		if (hr == SYNC_E_IGNORE){
			hr = hrSuccess;
		}
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Message hard deletion failed");
			goto exit;
		}

		hr = AddProcessedChanges(m_lstHardDelete);
		if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to add processed hard deletion changes");
			goto exit;
		}
	}

exit:
	MAPIFreeBuffer(lpEntryList);
	return hr;
}

HRESULT ECExchangeExportChanges::ExportFolderChanges(){
	HRESULT			hr = hrSuccess;

	LPMAPIFOLDER	lpFolder = NULL;
	LPSPropValue	lpPropArray = NULL;
	LPSPropValue	lpPropVal = NULL;

	ULONG			ulObjType = 0;
	ULONG			ulCount = 0;
	ULONG			ulSteps = 0;

	ULONG			cbEntryID = 0;
	LPENTRYID		lpEntryID = NULL;

	while(m_ulStep < m_lstChange.size() && (m_ulBufferSize == 0 || ulSteps < m_ulBufferSize)){
		if(!m_sourcekey.empty()) {
			// Normal export, need all properties
			hr = m_lpStore->EntryIDFromSourceKey(
				m_lstChange.at(m_ulStep).sSourceKey.cb,
				m_lstChange.at(m_ulStep).sSourceKey.lpb,
				0, NULL,
				&cbEntryID, &lpEntryID);

			if(hr != hrSuccess){
				m_lpLogger->Log(EC_LOGLEVEL_INFO, "change sourcekey not found");
				hr = hrSuccess;
				goto next;
			}
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "change sourcekey: %s", bin2hex(m_lstChange.at(m_ulStep).sSourceKey.cb, m_lstChange.at(m_ulStep).sSourceKey.lpb).c_str());

			hr = m_lpStore->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, (LPUNKNOWN*) &lpFolder);
			if(hr != hrSuccess){
				hr = hrSuccess;
				goto next;
			}

			hr = HrGetAllProps(lpFolder, (m_ulFlags & SYNC_UNICODE ? MAPI_UNICODE : 0), &ulCount, &lpPropArray);
			if(FAILED(hr)) {
				ZLOG_DEBUG(m_lpLogger, "Unable to get source folder properties");
				goto exit;
			}

			//for folders directly under m_lpFolder PR_PARENT_SOURCE_KEY must be NULL
			//this protects against recursive problems during syncing when the PR_PARENT_SOURCE_KEY
			//equals the sourcekey of the folder which we are already syncing.
			//If the exporter sends an empty PR_PARENT_SOURCE_KEY to the importer the importer can
			//assume the parent is the folder which it is syncing.

			lpPropVal = PpropFindProp(lpPropArray, ulCount, PR_PARENT_SOURCE_KEY);
			if(lpPropVal && m_sourcekey.size() == lpPropVal->Value.bin.cb && memcmp(lpPropVal->Value.bin.lpb, m_sourcekey.c_str(), m_sourcekey.size())==0){
				lpPropVal->Value.bin.cb = 0;
			}

			hr = m_lpImportHierarchy->ImportFolderChange(ulCount, lpPropArray);
		} else {
			// Server-wide ICS
			SPropValue sProps[1];

			sProps[0].ulPropTag = PR_SOURCE_KEY;
			sProps[0].Value.bin = m_lstChange.at(m_ulStep).sSourceKey;

			hr = m_lpImportHierarchy->ImportFolderChange(1, sProps);
		}

		if (hr == SYNC_E_IGNORE){
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "change ignored");
			hr = hrSuccess;
			goto next;
		}else if (hr == MAPI_E_INVALID_PARAMETER){
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "change invalid parameter");
			hr = hrSuccess;
			goto next;
		}else if(hr == MAPI_E_NOT_FOUND){
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "change not found");
			hr = hrSuccess;
			goto next;
		}else if(FAILED(hr)) {
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "change error: %s", stringify(hr, true).c_str());
			goto exit;
		}else if(hr != hrSuccess){
			m_lpLogger->Log(EC_LOGLEVEL_WARNING, "change warning: %s", stringify(hr, true).c_str());
		}
next:
		// Mark this change as processed
		m_setProcessedChanges.insert(std::pair<unsigned int, std::string>(m_lstChange.at(m_ulStep).ulChangeId, std::string((char *)m_lstChange.at(m_ulStep).sSourceKey.lpb, m_lstChange.at(m_ulStep).sSourceKey.cb)));

		if(lpFolder){
			lpFolder->Release();
			lpFolder = NULL;
		}

		MAPIFreeBuffer(lpPropArray);
		lpPropArray = NULL;
		MAPIFreeBuffer(lpEntryID);
		lpEntryID = NULL;
		++ulSteps;
		++m_ulStep;
	}

	if(m_ulStep < m_lstChange.size())
		hr = SYNC_W_PROGRESS;

exit:
	MAPIFreeBuffer(lpEntryID);
	if(lpFolder)
		lpFolder->Release();
	MAPIFreeBuffer(lpPropArray);
	return hr;
}

HRESULT ECExchangeExportChanges::ExportFolderDeletes(){
	HRESULT			hr = hrSuccess;

	LPENTRYLIST		lpEntryList = NULL;

	if(!m_lstSoftDelete.empty()){
		hr = ChangesToEntrylist(&m_lstSoftDelete, &lpEntryList);
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to create folder deletion entry list");
			goto exit;
		}

		hr = m_lpImportHierarchy->ImportFolderDeletion(SYNC_SOFT_DELETE, lpEntryList);
		if (hr == SYNC_E_IGNORE){
			hr = hrSuccess;
		}
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to import folder deletions");
			goto exit;
		}

		hr = AddProcessedChanges(m_lstSoftDelete);
		if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to add processed folder soft deletions");
			goto exit;
		}
	}

	MAPIFreeBuffer(lpEntryList);
	lpEntryList = NULL;

	if(!m_lstHardDelete.empty()){
		hr = ChangesToEntrylist(&m_lstHardDelete, &lpEntryList);
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to create folder hard delete entry list");
			goto exit;
		}

		hr = m_lpImportHierarchy->ImportFolderDeletion(0, lpEntryList);
		if (hr == SYNC_E_IGNORE){
			hr = hrSuccess;
		}
		if(hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Hard delete folder import failed");
			goto exit;
		}

		hr = AddProcessedChanges(m_lstHardDelete);
		if (hr != hrSuccess) {
			ZLOG_DEBUG(m_lpLogger, "Unable to add processed folder hard deletions");
			goto exit;
		}
	}

exit:
	MAPIFreeBuffer(lpEntryList);
	return hr;
}

//write in the stream 4 bytes syncid, 4 bytes changeid, 4 bytes changecount, {4 bytes changeid, 4 bytes sourcekeysize, sourcekey}
HRESULT ECExchangeExportChanges::UpdateStream(LPSTREAM lpStream){
	HRESULT hr = hrSuccess;
	LARGE_INTEGER liPos = {{0, 0}};
	ULARGE_INTEGER liZero = {{0, 0}};
	ULONG ulSize;
	ULONG ulChangeCount = 0;
	ULONG ulChangeId = 0;
	ULONG ulSourceKeySize = 0;
	PROCESSEDCHANGESSET::const_iterator iterProcessedChange;
	
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

	if(m_ulSyncId == 0){
		m_ulChangeId = 0;
	}

	hr = lpStream->Write(&m_ulChangeId, 4, &ulSize);
	if(hr != hrSuccess)
		goto exit;

	if(!m_setProcessedChanges.empty()) {
		ulChangeCount = m_setProcessedChanges.size();

		hr = lpStream->Write(&ulChangeCount, 4, &ulSize);
		if(hr != hrSuccess)
			goto exit;

		for (iterProcessedChange = m_setProcessedChanges.begin();
		     iterProcessedChange != m_setProcessedChanges.end();
		     ++iterProcessedChange)
		{
			ulChangeId = iterProcessedChange->first;
			hr = lpStream->Write(&ulChangeId, 4, &ulSize);
			if(hr != hrSuccess)
				goto exit;

			ulSourceKeySize = iterProcessedChange->second.size();

			hr = lpStream->Write(&ulSourceKeySize, 4, &ulSize);
			if(hr != hrSuccess)
				goto exit;

			hr = lpStream->Write(iterProcessedChange->second.c_str(), iterProcessedChange->second.size(), &ulSize);
			if(hr != hrSuccess)
				goto exit;
		}
	}

	// Seek back to the beginning after we've finished
	lpStream->Seek(liPos, STREAM_SEEK_SET, NULL);

exit:
	if(hr != hrSuccess)
		ZLOG_DEBUG(m_lpLogger, "Stream operation failed");

	return hr;
}

//convert (delete) changes to entrylist for message and folder deletion.
HRESULT ECExchangeExportChanges::ChangesToEntrylist(std::list<ICSCHANGE> * lpLstChanges, LPENTRYLIST * lppEntryList){
	HRESULT 		hr = hrSuccess;
	LPENTRYLIST		lpEntryList = NULL;
	ULONG			ulCount = 0;
	std::list<ICSCHANGE>::const_iterator lpChange;

	if ((hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), (LPVOID *)&lpEntryList)) != hrSuccess)
		goto exit;

	lpEntryList->cValues = lpLstChanges->size();
	if(lpEntryList->cValues > 0){
		if ((hr = MAPIAllocateMore(sizeof(SBinary) * lpEntryList->cValues, lpEntryList, (LPVOID *)&lpEntryList->lpbin)) != hrSuccess)
			goto exit;
	}else{
		lpEntryList->lpbin = NULL;
	}
	ulCount = 0;
	for (lpChange = lpLstChanges->begin(); lpChange != lpLstChanges->end(); ++lpChange) {
		lpEntryList->lpbin[ulCount].cb = lpChange->sSourceKey.cb;
		if ((hr = MAPIAllocateMore(lpChange->sSourceKey.cb, lpEntryList, (void **)&lpEntryList->lpbin[ulCount].lpb)) != hrSuccess)
			goto exit;
		memcpy(lpEntryList->lpbin[ulCount].lpb, lpChange->sSourceKey.lpb, lpChange->sSourceKey.cb);
		++ulCount;
	}

	lpEntryList->cValues = ulCount;

	*lppEntryList = lpEntryList;

exit:
	if (hr != hrSuccess)
		MAPIFreeBuffer(lpEntryList);

	return hr;
}

/**
 * Add processed changes to the precessed changes list
 *
 * @param[in] lstChanges	List with changes
 *
 */
HRESULT ECExchangeExportChanges::AddProcessedChanges(ChangeList &lstChanges)
{
	ChangeListIter iterChange;

	for (iterChange = lstChanges.begin(); iterChange != lstChanges.end(); ++iterChange)
		m_setProcessedChanges.insert(std::pair<unsigned int, std::string>(iterChange->ulChangeId, std::string((char *)iterChange->sSourceKey.lpb, iterChange->sSourceKey.cb)));

	return hrSuccess;
}

void ECExchangeExportChanges::LogMessageProps(int loglevel, ULONG cValues, LPSPropValue lpPropArray)
{
	if (m_lpLogger->Log(loglevel)) {
		LPSPropValue lpPropEntryID = PpropFindProp(lpPropArray, cValues, PR_ENTRYID);
		LPSPropValue lpPropSK = PpropFindProp(lpPropArray, cValues, PR_SOURCE_KEY);
		LPSPropValue lpPropFlags = PpropFindProp(lpPropArray, cValues, PR_MESSAGE_FLAGS);
		LPSPropValue lpPropHierarchyId = PpropFindProp(lpPropArray, cValues, PR_EC_HIERARCHYID);
		LPSPropValue lpPropParentId = PpropFindProp(lpPropArray, cValues, PR_EC_PARENT_HIERARCHYID);

		m_lpLogger->Log(loglevel, "ExportFast:   Message info: id=%u, parentid=%u, msgflags=%x, entryid=%s, sourcekey=%s",
						(lpPropHierarchyId ? lpPropHierarchyId->Value.ul : 0),
						(lpPropParentId ? lpPropParentId->Value.ul : 0),
						(lpPropFlags ? lpPropFlags->Value.ul : 0),
						(lpPropEntryID ? bin2hex(lpPropEntryID->Value.bin.cb, lpPropEntryID->Value.bin.lpb).c_str() : "<Unknown>"),
						(lpPropSK ? bin2hex(lpPropSK->Value.bin.cb, lpPropSK->Value.bin.lpb).c_str() : "<Unknown>"));
	}
}
