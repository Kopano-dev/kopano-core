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
#include <new>
#include <stdexcept>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include "WSMAPIFolderOps.h"

#include "Mem.h"
#include <kopano/ECGuid.h>

// Utils
#include "SOAPUtils.h"
#include "WSUtil.h"

#include <kopano/charset/utf8string.h>
#include "soapKCmdProxy.h"

#define START_SOAP_CALL retry:
#define END_SOAP_CALL 	\
	if (er == KCERR_END_OF_SESSION && m_lpTransport->HrReLogon() == hrSuccess) \
		goto retry; \
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND); \
	if(hr != hrSuccess) \
		goto exit;

/*
 * The WSMAPIFolderOps for use with the WebServices transport
 */

WSMAPIFolderOps::WSMAPIFolderOps(ECSESSIONID sid, ULONG cbEntryId,
    const ENTRYID *lpEntryId, WSTransport *lpTransport) :
	ECUnknown("WSMAPIFolderOps"), ecSessionId(sid),
	m_lpTransport(lpTransport)
{
	lpTransport->AddSessionReloadCallback(this, Reload, &m_ulSessionReloadCallback);
	auto ret = CopyMAPIEntryIdToSOAPEntryId(cbEntryId, lpEntryId, &m_sEntryId);
	if (ret != hrSuccess)
		throw std::runtime_error("CopyMAPIEntryIdToSOAPEntryId");
}

WSMAPIFolderOps::~WSMAPIFolderOps()
{
	m_lpTransport->RemoveSessionReloadCallback(m_ulSessionReloadCallback);

	FreeEntryId(&m_sEntryId, false);
}

HRESULT WSMAPIFolderOps::Create(ECSESSIONID ecSessionId, ULONG cbEntryId,
    const ENTRYID *lpEntryId, WSTransport *lpTransport,
    WSMAPIFolderOps **lppFolderOps)
{
	return alloc_wrap<WSMAPIFolderOps>(ecSessionId, cbEntryId, lpEntryId,
	       lpTransport).put(lppFolderOps);
}

HRESULT WSMAPIFolderOps::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE3(ECMAPIFolderOps, WSMAPIFolderOps, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}
	
HRESULT WSMAPIFolderOps::HrCreateFolder(ULONG ulFolderType,
    const utf8string &strFolderName, const utf8string &strComment,
    BOOL fOpenIfExists, ULONG ulSyncId, const SBinary *lpsSourceKey,
    ULONG cbNewEntryId, LPENTRYID lpNewEntryId, ULONG* lpcbEntryId,
    LPENTRYID *lppEntryId)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	struct xsd__base64Binary sSourceKey;
	struct createFolderResponse sResponse;
	entryId*	lpsEntryId = NULL;

	LockSoap();

	if(lpNewEntryId) {
		hr = CopyMAPIEntryIdToSOAPEntryId(cbNewEntryId, lpNewEntryId, &lpsEntryId);
		if(hr != hrSuccess)
			goto exit;
	}

	if (lpsSourceKey) {
		sSourceKey.__ptr = lpsSourceKey->lpb;
		sSourceKey.__size = lpsSourceKey->cb;
	} else {
		sSourceKey.__ptr = NULL;
		sSourceKey.__size = 0;
	}

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->createFolder(ecSessionId,
		    m_sEntryId, lpsEntryId, ulFolderType, strFolderName.c_str(),
		    strComment.c_str(), !!fOpenIfExists, ulSyncId, sSourceKey,
		    &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if(lpcbEntryId != NULL && lppEntryId != NULL)
	{
		hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sEntryId, lpcbEntryId, lppEntryId);
		if(hr != hrSuccess)
			goto exit;
	}

exit:
	UnLockSoap();

	if(lpsEntryId)
		FreeEntryId(lpsEntryId, true);

	return hr;
}

HRESULT WSMAPIFolderOps::HrDeleteFolder(ULONG cbEntryId,
    const ENTRYID *lpEntryId, ULONG ulFlags, ULONG ulSyncId)
{
	ECRESULT	er = erSuccess;
	entryId		sEntryId;

	LockSoap();

	// Cheap copy entryid
	auto hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryId, lpEntryId, &sEntryId, true);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->deleteFolder(ecSessionId, sEntryId,
		    ulFlags, ulSyncId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSMAPIFolderOps::HrEmptyFolder(ULONG ulFlags, ULONG ulSyncId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;

	LockSoap();

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->emptyFolder(ecSessionId,
		    m_sEntryId, ulFlags, ulSyncId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSMAPIFolderOps::HrSetReadFlags(ENTRYLIST *lpMsgList, ULONG ulFlags, ULONG ulSyncId)
{
	HRESULT			hr = hrSuccess;
	ECRESULT		er = erSuccess;
	struct entryList sEntryList;

	LockSoap();

	if(lpMsgList) {
		if(lpMsgList->cValues == 0)
			goto exit;
		
		hr = CopyMAPIEntryListToSOAPEntryList(lpMsgList, &sEntryList);
		if(hr != hrSuccess)
			goto exit;
	}

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->setReadFlags(ecSessionId, ulFlags,
		    &m_sEntryId, lpMsgList != nullptr ? &sEntryList : nullptr,
		    ulSyncId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	FreeEntryList(&sEntryList, false);

	return hr;
}

HRESULT WSMAPIFolderOps::HrSetSearchCriteria(ENTRYLIST *lpMsgList, SRestriction *lpRestriction, ULONG ulFlags)
{
	ECRESULT		er = erSuccess;
	HRESULT			hr = hrSuccess;

	struct entryList*		lpsEntryList = NULL;
	struct restrictTable*	lpsRestrict = NULL;

	LockSoap();

	if(lpMsgList) {
		lpsEntryList = s_alloc<entryList>(nullptr);
		hr = CopyMAPIEntryListToSOAPEntryList(lpMsgList, lpsEntryList);
		if(hr != hrSuccess)
			goto exit;
	}

	if(lpRestriction) 
	{
		hr = CopyMAPIRestrictionToSOAPRestriction(&lpsRestrict, lpRestriction);
		if(hr != hrSuccess)
			goto exit;
	}
	
	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableSetSearchCriteria(ecSessionId,
		    m_sEntryId, lpsRestrict, lpsEntryList, ulFlags, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

	hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		goto exit;

exit:
	UnLockSoap();

	if(lpsRestrict)
		FreeRestrictTable(lpsRestrict);

	if(lpsEntryList)
		FreeEntryList(lpsEntryList, true);
	
	return hr;
}

HRESULT WSMAPIFolderOps::HrGetSearchCriteria(ENTRYLIST **lppMsgList, LPSRestriction *lppRestriction, ULONG *lpulFlags)
{
	HRESULT			hr = hrSuccess;
	ECRESULT		er = erSuccess;
	ecmem_ptr<ENTRYLIST> lpMsgList;
	ecmem_ptr<SRestriction> lpRestriction;

	struct tableGetSearchCriteriaResponse sResponse;

	LockSoap();

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableGetSearchCriteria(ecSessionId,
		    m_sEntryId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if(lppRestriction) {
		hr = ECAllocateBuffer(sizeof(SRestriction), &~lpRestriction);
		if(hr != hrSuccess)
			goto exit;

		hr = CopySOAPRestrictionToMAPIRestriction(lpRestriction, sResponse.lpRestrict, lpRestriction);
		if(hr != hrSuccess)
			goto exit;
	}

	if(lppMsgList) {
		hr = CopySOAPEntryListToMAPIEntryList(sResponse.lpFolderIDs, &~lpMsgList);
		if(hr != hrSuccess)
			goto exit;

	}

	if(lppMsgList)
		*lppMsgList = lpMsgList.release();
	if(lppRestriction)
		*lppRestriction = lpRestriction.release();
	if(lpulFlags)
		*lpulFlags = sResponse.ulFlags;

exit:
	UnLockSoap();
	return hr;
}

HRESULT WSMAPIFolderOps::HrCopyFolder(ULONG cbEntryFrom,
    const ENTRYID *lpEntryFrom, ULONG cbEntryDest, const ENTRYID *lpEntryDest,
    const utf8string &strNewFolderName, ULONG ulFlags, ULONG ulSyncId)
{
	ECRESULT	er = erSuccess;
	entryId sEntryFrom, sEntryDest; /* Do not free, cheap copy */

	LockSoap();
	auto hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryFrom, lpEntryFrom, &sEntryFrom, true);
	if(hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryDest, lpEntryDest, &sEntryDest, true);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->copyFolder(ecSessionId, sEntryFrom, sEntryDest,
		    strNewFolderName.c_str(), ulFlags, ulSyncId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSMAPIFolderOps::HrCopyMessage(ENTRYLIST *lpMsgList, ULONG cbEntryDest,
    const ENTRYID *lpEntryDest, ULONG ulFlags, ULONG ulSyncId)
{
	HRESULT			hr = hrSuccess;
	ECRESULT		er = erSuccess;
	struct entryList sEntryList;
	entryId			sEntryDest;	//Do not free, cheap copy

	LockSoap();

	if(lpMsgList->cValues == 0)
		goto exit;

	hr = CopyMAPIEntryListToSOAPEntryList(lpMsgList, &sEntryList);
	if(hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryDest, lpEntryDest, &sEntryDest, true);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->copyObjects(ecSessionId,
		    &sEntryList, sEntryDest, ulFlags, ulSyncId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();
	FreeEntryList(&sEntryList, false);
	
	return hr;
}

HRESULT WSMAPIFolderOps::HrGetMessageStatus(ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG ulFlags, ULONG *lpulMessageStatus)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId		sEntryId;	//Do not free, cheap copy
	
	struct messageStatus sMessageStatus;

	LockSoap();

	if(lpEntryID == NULL) {
		hr = MAPI_E_INVALID_ENTRYID;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->getMessageStatus(ecSessionId,
		    sEntryId, ulFlags, &sMessageStatus) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sMessageStatus.er;
	}
	END_SOAP_CALL

	*lpulMessageStatus = sMessageStatus.ulMessageStatus;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSMAPIFolderOps::HrSetMessageStatus(ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG ulNewStatus, ULONG ulNewStatusMask,
    ULONG ulSyncId, ULONG *lpulOldStatus)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId		sEntryId;	//Do not free, cheap copy
	
	struct messageStatus sMessageStatus;

	LockSoap();

	if(lpEntryID == NULL)	{
		hr = MAPI_E_INVALID_ENTRYID;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->setMessageStatus(ecSessionId,
		    sEntryId, ulNewStatus, ulNewStatusMask, ulSyncId,
		    &sMessageStatus) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sMessageStatus.er;
	}
	END_SOAP_CALL

	if(lpulOldStatus)
		*lpulOldStatus = sMessageStatus.ulMessageStatus;
exit:
	UnLockSoap();

	return hr;
}

HRESULT WSMAPIFolderOps::HrGetChangeInfo(ULONG cbEntryID,
    const ENTRYID *lpEntryID, SPropValue **lppPropPCL, SPropValue **lppPropCK)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId sEntryId;
	KC::memory_ptr<SPropValue> lpSPropValPCL, lpSPropValCK;
	getChangeInfoResponse sChangeInfo;

	LockSoap();

	if (lpEntryID == NULL)	{
		hr = MAPI_E_INVALID_ENTRYID;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exit;
	if (m_lpTransport->m_lpCmd->getChangeInfo(ecSessionId, sEntryId, &sChangeInfo) != SOAP_OK)
		er = KCERR_NETWORK_ERROR;
	else
		er = sChangeInfo.er;

	hr = kcerr_to_mapierr(er);
	if (hr != hrSuccess)
		goto exit;

	if (lppPropPCL) {
		hr = MAPIAllocateBuffer(sizeof *lpSPropValPCL, &~lpSPropValPCL);
		if (hr != hrSuccess)
			goto exit;

		hr = CopySOAPPropValToMAPIPropVal(lpSPropValPCL, &sChangeInfo.sPropPCL, lpSPropValPCL);
		if (hr != hrSuccess)
			goto exit;
	}

	if (lppPropCK) {
		hr = MAPIAllocateBuffer(sizeof *lpSPropValCK, &~lpSPropValCK);
		if (hr != hrSuccess)
			goto exit;

		hr = CopySOAPPropValToMAPIPropVal(lpSPropValCK, &sChangeInfo.sPropCK, lpSPropValCK);
		if (hr != hrSuccess)
			goto exit;
	}

	// All went well, modify output parameters
	if (lppPropPCL != nullptr)
		*lppPropPCL = lpSPropValPCL.release();
	if (lppPropCK != nullptr)
		*lppPropCK = lpSPropValCK.release();
exit:
	UnLockSoap();
	return hr;
}

//FIXME: one lock/unlock function
void WSMAPIFolderOps::LockSoap()
{
	m_lpTransport->m_hDataLock.lock();
}

void WSMAPIFolderOps::UnLockSoap()
{
	//Clean up data create with soap_malloc
	if (m_lpTransport->m_lpCmd->soap != nullptr) {
		soap_destroy(m_lpTransport->m_lpCmd->soap);
		soap_end(m_lpTransport->m_lpCmd->soap);
	}
	m_lpTransport->m_hDataLock.unlock();
}

HRESULT WSMAPIFolderOps::Reload(void *lpParam, ECSESSIONID sessionid)
{
	static_cast<WSMAPIFolderOps *>(lpParam)->ecSessionId = sessionid;
	return hrSuccess;
}
