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
#include <kopano/ECConfig.h>
#include "operations.h"
#include "ECArchiverLogger.h"
#include "helpers/MAPIPropHelper.h"
#include "helpers/ArchiveHelper.h"
#include "ArchiverSession.h"
#include <kopano/ECRestriction.h>

#include <mapiutil.h>
#include <kopano/mapiext.h>

#include <kopano/Util.h>
#include <kopano/stringutil.h>
#include <kopano/mapi_ptr.h>
#include <kopano/mapiguidext.h>

#include <algorithm>
using namespace KC::helpers;

namespace KC { namespace operations {

/**
 * @param[in]	lpLogger
 *					Pointer to an ECLogger object that's used for logging.
 */
ArchiveOperationBase::ArchiveOperationBase(ECArchiverLogger *lpLogger, int ulAge, bool bProcessUnread, ULONG ulInhibitMask)
: m_lpLogger(lpLogger)
, m_ulAge(ulAge)
, m_bProcessUnread(bProcessUnread)
, m_ulInhibitMask(ulInhibitMask)
{
	GetSystemTimeAsFileTime(&m_ftCurrent);
}

HRESULT ArchiveOperationBase::GetRestriction(LPMAPIPROP lpMapiProp, LPSRestriction *lppRestriction)
{
	HRESULT hr = hrSuccess;
	ULARGE_INTEGER li;
	SPropValue sPropRefTime;
	ECAndRestriction resResult;

	PROPMAP_START(1)
	PROPMAP_NAMED_ID(FLAGS, PT_LONG, PSETID_Archive, dispidFlags)
	PROPMAP_INIT(lpMapiProp)

	if (lppRestriction == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (m_ulAge < 0)
		return MAPI_E_NOT_FOUND;

	li.LowPart = m_ftCurrent.dwLowDateTime;
	li.HighPart = m_ftCurrent.dwHighDateTime;
	
	li.QuadPart -= (m_ulAge * _DAY);
		
	sPropRefTime.ulPropTag = PROP_TAG(PT_SYSTIME, 0);
	sPropRefTime.Value.ft.dwLowDateTime = li.LowPart;
	sPropRefTime.Value.ft.dwHighDateTime = li.HighPart;
	resResult += ECOrRestriction(
		ECAndRestriction(
			ECExistRestriction(PR_MESSAGE_DELIVERY_TIME) +
			ECPropertyRestriction(RELOP_LT, PR_MESSAGE_DELIVERY_TIME, &sPropRefTime, ECRestriction::Cheap)
		) +
		ECAndRestriction(
			ECExistRestriction(PR_CLIENT_SUBMIT_TIME) +
			ECPropertyRestriction(RELOP_LT, PR_CLIENT_SUBMIT_TIME, &sPropRefTime, ECRestriction::Cheap)
		));
	if (!m_bProcessUnread)
		resResult += ECBitMaskRestriction(BMR_NEZ, PR_MESSAGE_FLAGS, MSGFLAG_READ);
	resResult +=
		ECNotRestriction(
			ECAndRestriction(
				ECExistRestriction(PROP_FLAGS) +
				ECBitMaskRestriction(BMR_NEZ, PROP_FLAGS, m_ulInhibitMask)
			)
		);
	hr = resResult.CreateMAPIRestriction(lppRestriction, ECRestriction::Full);
	return hr;
}

HRESULT ArchiveOperationBase::VerifyRestriction(LPMESSAGE lpMessage)
{
	HRESULT hr = hrSuccess;
	SRestrictionPtr ptrRestriction;

	hr = GetRestriction(lpMessage, &~ptrRestriction);
	if (hr != hrSuccess)
		return hr;

	return TestRestriction(ptrRestriction, lpMessage, createLocaleFromName(""));
}

/**
 * @param[in]	lpLogger
 *					Pointer to an ECLogger object that's used for logging.
 */
ArchiveOperationBaseEx::ArchiveOperationBaseEx(ECArchiverLogger *lpLogger, int ulAge, bool bProcessUnread, ULONG ulInhibitMask)
: ArchiveOperationBase(lpLogger, ulAge, bProcessUnread, ulInhibitMask)
{ }

/**
 * Process the message that exists in the search folder ptrFolder and deletegate the work to the derived class.
 * The property array lpProps contains at least the entryid of the message to open and the entryid of the parent
 * folder of the message to open.
 *
 * @param[in]	lpFolder
 *					A MAPIFolder pointer that points to the parent folder.
 * @param[in]	cProps
 *					The number op properties pointed to by lpProps.
 * @param[in]	lpProps
 *					Pointer to an array of properties that are used by the Operation object.
 */
HRESULT ArchiveOperationBaseEx::ProcessEntry(IMAPIFolder *lpFolder,
    const SRow &proprow)
{
	HRESULT hr;
	bool bReloadFolder = false;
	ULONG ulType = 0;

	assert(lpFolder != NULL);
	if (lpFolder == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto lpFolderEntryId = proprow.cfind(PR_PARENT_ENTRYID);
	if (lpFolderEntryId == NULL) {
		Logger()->Log(EC_LOGLEVEL_FATAL, "PR_PARENT_ENTRYID missing");
		return MAPI_E_NOT_FOUND;
	}
	
	if (m_ptrCurFolderEntryId != nullptr) {
		int nResult = 0;
		// @todo: Create correct locale.
		hr = Util::CompareProp(m_ptrCurFolderEntryId, lpFolderEntryId, createLocaleFromName(""), &nResult);
		if (hr != hrSuccess)
			return Logger()->perr("Failed to compare current and new entryid", hr);
		
		if (nResult != 0) {
			Logger()->logf(EC_LOGLEVEL_DEBUG, "Leaving folder (%s)", bin2hex(m_ptrCurFolderEntryId->Value.bin).c_str());
			Logger()->SetFolder(KC_T(""));
			hr = LeaveFolder();
			if (hr != hrSuccess)
				return Logger()->perr("Failed to leave folder", hr);
			bReloadFolder = true;
		}
	}

	if (m_ptrCurFolderEntryId != nullptr && !bReloadFolder)
		return DoProcessEntry(proprow);

	SPropValuePtr ptrPropValue;
	Logger()->logf(EC_LOGLEVEL_DEBUG, "Opening folder (%s)", bin2hex(lpFolderEntryId->Value.bin).c_str());
	hr = lpFolder->OpenEntry(lpFolderEntryId->Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(lpFolderEntryId->Value.bin.lpb),
	     &iid_of(m_ptrCurFolder), MAPI_BEST_ACCESS | fMapiDeferredErrors,
	     &ulType, &~m_ptrCurFolder);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to open folder", hr);
	hr = MAPIAllocateBuffer(sizeof(SPropValue), &~m_ptrCurFolderEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = Util::HrCopyProperty(m_ptrCurFolderEntryId, lpFolderEntryId, m_ptrCurFolderEntryId);
	if (hr != hrSuccess)
		return hr;
	if (HrGetOneProp(m_ptrCurFolder, PR_DISPLAY_NAME, &~ptrPropValue) == hrSuccess)
		Logger()->SetFolder(ptrPropValue->Value.LPSZ);
	else
		Logger()->SetFolder(KC_T("<Unnamed>"));
	hr = EnterFolder(m_ptrCurFolder);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to enter folder", hr);
	return DoProcessEntry(proprow);
}

}} /* namespace */
