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
#include <memory>
#include <utility>
#include <kopano/ECRestriction.h>
#include "vconverter.h"
#include "valarm.h"
#include "icalrecurrence.h"
#include <mapi.h>
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include <kopano/tie.hpp>
#include <kopano/CommonUtil.h>
#include <kopano/Util.h>
#include "icaluid.h"
#include "nameids.h"
#include <kopano/stringutil.h>
#include <ctime>
#include <kopano/mapi_ptr.h>
#include <kopano/namedprops.h>
#include "icalcompat.hpp"
#include "icalmem.hpp"

using namespace KCHL;

namespace KC {

using std::string;
using std::wstring;

/**
 * Copies string from source to destination
 * 
 * The function also does charset conversion according to the ECIConv object passed
 *
 * @param[in]	lpIConv		ECIConv object pointer for charset conversion, may be NULL for no conversion
 * @param[in]	base		Base from memory allocation, cannot not be NULL
 * @param[in]	lpszSrc		Source chararacter string, NULL allowed, since icalproperty_get_*() may return NULL
 * @param[out]	lppszDst	Destination char pointer, cannot be NULL
 * @return		MAPI error code
 */
// expect input to be UTF-8 from libical ?
HRESULT HrCopyString(convert_context& converter, std::string& strCharset, void *base, const char* lpszSrc, WCHAR** lppszDst)
{
	std::wstring strWide;

	if (lpszSrc)
		strWide = converter.convert_to<wstring>(lpszSrc, rawsize(lpszSrc), strCharset.c_str());

	return HrCopyString(base, strWide.c_str(), lppszDst);
}

HRESULT HrCopyString(void *base, const wchar_t *src, wchar_t **dst)
{
	if (src == nullptr)
		src = L"";
	auto len = (wcslen(src) + 1) * sizeof(*src);
	HRESULT hr = MAPIAllocateMore(len, base, reinterpret_cast<void **>(dst));
	if (hr != hrSuccess)
		return hr;
	memcpy(*dst, src, len);
	return hrSuccess;
}

/**
 * @param[in]	lpAdrBook		Mapi addresss book
 * @param[in]	mapTimeZones	std::map containing timezone names and corresponding timezone structures
 * @param[in]	lpNamedProps	Named property tag array
 * @param[in]	strCharset		Charset to convert the final item to
 * @param[in]	blCensor		Censor some properties for private items if set to true
 * @param[in]	bNoRecipients	Skip recipients during conversion if set to true
 * @param[in]	lpMailUser		IMailUser Object pointer of the logged in user
 */
VConverter::VConverter(LPADRBOOK lpAdrBook, timezone_map *mapTimeZones, LPSPropTagArray lpNamedProps, const std::string& strCharset, bool blCensor, bool bNoRecipients, IMailUser *lpMailUser)
{
	m_lpAdrBook = lpAdrBook;
	m_mapTimeZones = mapTimeZones;
	m_iCurrentTimeZone = m_mapTimeZones->end();
	m_lpNamedProps = lpNamedProps;
	m_strCharset = strCharset;
	m_lpMailUser = lpMailUser;
	m_bCensorPrivate = blCensor;
	m_bNoRecipients = bNoRecipients;
	m_ulUserStatus = 0;
}

/**
 * Basic ical to mapi conversion, common to all VEVENT, VTODO,
 * VFREEBUSY.  It returns an internal icalitem struct, which can later
 * be converted into an existing IMessage.
 *
 * @param[in]	lpEventRoot		Top level VCALENDAR component class
 * @param[in]	lpEvent			VTODO/VEVENT/VFREEBUSY component which is parsed
 * @param[in]	lpPrevItem		Previous ical component that was parsed, NULL if this is the first item, used for adding exceptions.
 * @param[in]	lppRet			Return structure in which all mapi properties are stored
 * @return		MAPI error code
 */
HRESULT VConverter::HrICal2MAPI(icalcomponent *lpEventRoot, icalcomponent *lpEvent, icalitem *lpPrevItem, icalitem **lppRet)
{
	icaltimetype icLastModifed;
	bool bIsAllday;

	// Retrieve the Allday status of the event
	auto hr = HrRetrieveAlldayStatus(lpEvent, &bIsAllday);
	if (hr != hrSuccess)
		return hr;

	// we might be updating for exceptions
	if (*lppRet != NULL && lpPrevItem != NULL && lpPrevItem == *lppRet) {
		hr = HrAddException(lpEventRoot, lpEvent, bIsAllday, lpPrevItem);
		if (hr == hrSuccess)
			return hrSuccess;
	}

	std::unique_ptr<icalitem> lpIcalItem(new icalitem);
	hr = MAPIAllocateBuffer(sizeof(void *), &~lpIcalItem->base);
	if (hr != hrSuccess)
		return hr;

	auto icMethod = icalcomponent_get_method(lpEventRoot);
	auto lpicLastModified = icalcomponent_get_first_property(lpEvent, ICAL_LASTMODIFIED_PROPERTY);
	if (lpicLastModified)
		icLastModifed = icalproperty_get_lastmodified(lpicLastModified);
	else
		icLastModifed = icaltime_null_time();
	// according to the RFC, LASTMODIFIED is always in UTC
	lpIcalItem->tLastModified = icaltime_as_timet(icLastModifed);

	// also sets strUid in icalitem struct
	hr = HrAddUids(lpEvent, lpIcalItem.get());
	if (hr != hrSuccess)
		return hr;

	// Handles RECURRENCE-ID tag for exception update
	hr = HrAddRecurrenceID(lpEvent, lpIcalItem.get());
	if (hr != hrSuccess)
		return hr;
	hr = HrAddStaticProps(icMethod, lpIcalItem.get());
	if (hr != hrSuccess)
		return hr;
	hr = HrAddSimpleHeaders(lpEvent, lpIcalItem.get()); // subject, location, ...
	if (hr != hrSuccess)
		return hr;
	hr = HrAddXHeaders(lpEvent, lpIcalItem.get());
	if (hr != hrSuccess)
		return hr;
	hr = HrAddCategories(lpEvent, lpIcalItem.get());
	if (hr != hrSuccess)
		return hr;

	if (icMethod == ICAL_METHOD_REPLY)
		hr = HrAddReplyRecipients(lpEvent, lpIcalItem.get());
	else						// CANCEL, REQUEST, PUBLISH
		hr = HrAddRecipients(lpEvent, lpIcalItem.get(), &lpIcalItem->lstMsgProps, &lpIcalItem->lstRecips);
	if (hr != hrSuccess)
		return hr;
	
	hr = HrResolveUser(lpIcalItem->base, &(lpIcalItem->lstRecips));
	if (hr != hrSuccess)
		return hr;

	// This function uses m_ulUserStatus set by HrResolveUser.
	hr = HrAddBaseProperties(icMethod, lpEvent, lpIcalItem->base, false, &lpIcalItem->lstMsgProps);
	if (hr != hrSuccess)
		return hr;
	hr = HrAddBusyStatus(lpEvent, icMethod, lpIcalItem.get());
	if (hr != hrSuccess)
		return hr;
	
	// Important: m_iCurrentTimeZone will be set by this function, because of the possible recurrence lateron
	hr = HrAddTimes(icMethod, lpEventRoot, lpEvent, bIsAllday, lpIcalItem.get());
	if (hr != hrSuccess)
		return hr;

	// Set reminder / alarm
	hr = HrAddReminder(lpEventRoot, lpEvent, lpIcalItem.get());
	if (hr != hrSuccess)
		return hr;

	// Set recurrence.
	hr = HrAddRecurrence(lpEventRoot, lpEvent, bIsAllday, lpIcalItem.get());
	if (hr != hrSuccess)
		return hr;
	*lppRet = lpIcalItem.release();
	return hr;
}

/**
 * Returns the UID string from the ical data
 *
 * @param[in]	lpEvent		ical component that has to be parsed for uid property
 * @param[out]	strUid		Return string for ical UID property
 * @return		MAPI error code
 * @retval		MAPI_E_NOT_FOUND	UID property not present in ical data
 */
HRESULT VConverter::HrGetUID(icalcomponent *lpEvent, std::string *strUid)
{
	icalproperty *icProp = icalcomponent_get_first_property(lpEvent,
	                       ICAL_UID_PROPERTY);
	if (icProp == NULL)
		return MAPI_E_NOT_FOUND;
	auto uid = icalproperty_get_uid(icProp);
	if (uid == NULL || strcmp(uid,"") == 0)
		return MAPI_E_NOT_FOUND;
	*strUid = uid;
	return hrSuccess;
}

/**
 * Converts string UID to binary property. The converted UID is in a
 * format outlook wants, as described here.
 *
 * UID contents according to [MS-OXCICAL].pdf
 * UID = EncodedGlobalId or ThirdPartyGlobalId
 *
 * EncodedGlobalId    = Header GlobalIdData
 * ThirdPartyGlobalId = 1*UTF8-octets    ; Assuming UTF-8 is the encoding
 *
 * Header = ByteArrayID InstanceDate CreationDateTime Padding DataSize
 *
 * ByteArrayID        = "040000008200E00074C5B7101A82E008"
 * InstanceDate       = InstanceYear InstanceMonth InstanceDay
 * InstanceYear       = 4*4HEXDIGIT      ; UInt16
 * InstanceMonth      = 2*2HEXDIGIT      ; UInt8
 * InstanceDay        = 2*2HEXDIGIT      ; UInt8
 * CreationDateTime   = FileTime
 * FileTime           = 16*16HEXDIGIT    ; UInt6
 * Padding            = 16*16HEXDIGIT    ; "0000000000000000" recommended
 * DataSize           = 8*8HEXDIGIT      ; UInt32 little-endian
 * GlobalIdData       = 2*HEXDIGIT4
 * 
 * @param[in]	strUid			String UID 
 * @param[in]	base			Base for allocating memory
 * @param[out]	lpPropValue		The binary uid is returned in SPropValue structure
 * @return		Always return hrSuccess 
 */
HRESULT VConverter::HrMakeBinaryUID(const std::string &strUid, void *base, SPropValue *lpPropValue)
{
	SPropValue sPropValue;
	std::string strBinUid;
	std::string strByteArrayID = "040000008200E00074C5B7101A82E008";

	// Check whether this is a default Outlook UID
	// Exchange example: UID:040000008200E00074C5B7101A82E008 00000000 305D0F2A9A06C901 0000000000000000 10000000 7F64D28AE2DCC64C88F849733F5FBD1D
	// GMail example:    UID:rblkvqecgurvb0all6rjb3d1j8@google.com
	// Sunbird example: UID:1090c3de-36b2-4352-a155-a1436bc806b8
	if (strUid.compare(0, strByteArrayID.length(), strByteArrayID) == 0)
		// EncodedGlobalId
		strBinUid = hex2bin(strUid);
	else
		// ThirdPartyGlobalId
		HrMakeBinUidFromICalUid(strUid, &strBinUid);

	// Caller sets .ulPropTag
	sPropValue.Value.bin.cb = strBinUid.size();
	HRESULT hr = MAPIAllocateMore(sPropValue.Value.bin.cb, base,
	             reinterpret_cast<void **>(&sPropValue.Value.bin.lpb));
	if (hr != hrSuccess)
		return hr;
	memcpy(sPropValue.Value.bin.lpb, strBinUid.data(), sPropValue.Value.bin.cb);

	// set return value
	lpPropValue->Value.bin.cb  = sPropValue.Value.bin.cb;
	lpPropValue->Value.bin.lpb = sPropValue.Value.bin.lpb;
	return hrSuccess;
}

/**
 * Check if the user passed in param is the logged in user
 *
 * @param[in]	strUser		User name of user
 * @return		True if the user is logged in else false
 */
bool VConverter::bIsUserLoggedIn(const std::wstring &strUser)
{
	HRESULT hr = hrSuccess;
	memory_ptr<SPropValue> lpUserProp;
	
	if (m_lpMailUser)
		hr = HrGetOneProp(m_lpMailUser, PR_SMTP_ADDRESS_W, &~lpUserProp);
	else
		hr = MAPI_E_CALL_FAILED;
	if (hr != hrSuccess)
		return false;
	return wcsncmp(lpUserProp->Value.lpszW, strUser.c_str(), strUser.length()) == 0;
}

/**
 * Resolves the recipient email address of the users to correct user's
 * name and entry-id from addressbook.
 *
 * @param[in]		base				Base for memory allocation, cannot be NULL
 * @param[in,out]	lplstIcalRecip		List of recipients which are to be resolved, resolved users are returned in list
 * @return			MAPI error code
 */
HRESULT VConverter::HrResolveUser(void *base , std::list<icalrecip> *lplstIcalRecip)
{
	if (m_lpAdrBook == nullptr)
		/* no resolution attempted, as done from testsuite. */
		return hrSuccess;

	memory_ptr<SPropValue> lpUsrEidProp;
	adrlist_ptr lpAdrList;
	memory_ptr<ENTRYID> lpDDEntryID;
	ULONG cbDDEntryID;
	object_ptr<IABContainer> lpAddrFolder;
	memory_ptr<FlagList> lpFlagList;
	icalrecip icalRecipient;
	ULONG ulRetn = 0;
	ULONG ulObjType = 0;
	ULONG cbEID = 0;

	if (lplstIcalRecip->empty())
		return hrSuccess;
	if (m_lpMailUser != nullptr &&
	    HrGetOneProp(m_lpMailUser, PR_ENTRYID, &~lpUsrEidProp) != hrSuccess)
		/* ignore error - will check for pointer instead */;

	ULONG ulRecpCnt = lplstIcalRecip->size();
	auto hr = MAPIAllocateBuffer(CbNewFlagList(ulRecpCnt), &~lpFlagList);
	if (hr != hrSuccess)
		return hr;
	lpFlagList->cFlags = ulRecpCnt;
	hr = MAPIAllocateBuffer(CbNewADRLIST(ulRecpCnt), &~lpAdrList);
	if (hr != hrSuccess)
		return hr;

	lpAdrList->cEntries = ulRecpCnt;

	ulRecpCnt = 0;
	for (const auto &recip : *lplstIcalRecip) {
		lpAdrList->aEntries[ulRecpCnt].cValues = 1;

		hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **) &lpAdrList->aEntries[ulRecpCnt].rgPropVals);
		if (hr != hrSuccess)
			return hr;
		lpAdrList->aEntries[ulRecpCnt].rgPropVals[0].ulPropTag = PR_DISPLAY_NAME_W;
		lpAdrList->aEntries[ulRecpCnt].rgPropVals[0].Value.lpszW = const_cast<wchar_t *>(recip.strEmail.c_str());
		lpFlagList->ulFlag[ulRecpCnt++] = MAPI_UNRESOLVED;
	}

	hr = m_lpAdrBook->GetDefaultDir(&cbDDEntryID, &~lpDDEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = m_lpAdrBook->OpenEntry(cbDDEntryID, lpDDEntryID, &IID_IABContainer, 0, &ulObjType, &~lpAddrFolder);
	if (hr != hrSuccess)
		return hr;
	hr = lpAddrFolder->ResolveNames(NULL, MAPI_UNICODE, lpAdrList, lpFlagList);
	if (hr != hrSuccess)
		return hr;

	//reset the recepients with mapped names
	for (icalRecipient = lplstIcalRecip->front(), ulRecpCnt = 0;
	     ulRecpCnt < lplstIcalRecip->size(); ++ulRecpCnt) {
		if (lpFlagList->ulFlag[ulRecpCnt] == MAPI_RESOLVED)
		{
			auto lpMappedProp = lpAdrList->aEntries[ulRecpCnt].cfind(PR_DISPLAY_NAME_W);
			if (lpMappedProp)
				icalRecipient.strName = lpMappedProp->Value.lpszW;
		}
		
		//save the logged in user's satus , used in setting FB status  
		auto lpMappedProp = lpAdrList->aEntries[ulRecpCnt].cfind(PR_ENTRYID);
		if (lpMappedProp && lpUsrEidProp)
			hr = m_lpAdrBook->CompareEntryIDs(lpUsrEidProp->Value.bin.cb, (LPENTRYID)lpUsrEidProp->Value.bin.lpb, lpMappedProp->Value.bin.cb, (LPENTRYID)lpMappedProp->Value.bin.lpb , 0 , &ulRetn);
		if (hr == hrSuccess && ulRetn == TRUE)
			m_ulUserStatus = icalRecipient.ulTrackStatus;

		//Create EntryID by using mapped names, ical data might not have names.
		if (lpFlagList->ulFlag[ulRecpCnt] == MAPI_RESOLVED && lpMappedProp) {
			hr = MAPIAllocateMore(lpMappedProp->Value.bin.cb, base, (void**)&icalRecipient.lpEntryID);
			if (hr != hrSuccess)
				return hr;
			icalRecipient.cbEntryID = lpMappedProp->Value.bin.cb;
			memcpy(icalRecipient.lpEntryID, lpMappedProp->Value.bin.lpb, lpMappedProp->Value.bin.cb);
		} else {
			memory_ptr<ENTRYID> lpEID;
			hr = ECCreateOneOff((LPTSTR)icalRecipient.strName.c_str(), (LPTSTR)L"SMTP", (LPTSTR)icalRecipient.strEmail.c_str(), MAPI_UNICODE, &cbEID, &~lpEID);
			if (hr == hrSuccess) {
				// realloc on lpIcalItem
				hr = MAPIAllocateMore(cbEID, base, (void**)&icalRecipient.lpEntryID);
				if (hr != hrSuccess)
					return hr;
				icalRecipient.cbEntryID = cbEID;
				memcpy(icalRecipient.lpEntryID, lpEID, cbEID);
			}
		}

		lplstIcalRecip->emplace_back(icalRecipient);
		lplstIcalRecip->pop_front();
		icalRecipient = lplstIcalRecip->front();
	}
	return hrSuccess;
}

/**
 * Compare UIDs in icalitem and ical component.
 *
 * @param[in]	lpIcalItem		icalitem structure containing mapi properties
 * @param[in]	lpicEvent		ical component containing UID property
 * @return		MAPI error code
 * @retval		MAPI_E_BAD_VALUE	UIDs don't match
 */
HRESULT VConverter::HrCompareUids(icalitem *lpIcalItem, icalcomponent *lpicEvent)
{
	memory_ptr<SPropValue> lpPropVal;
	std::string strUid;
	int res;
	
	auto hr = HrGetUID(lpicEvent, &strUid);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpPropVal);
	if (hr != hrSuccess)
		return hr;
	hr = HrMakeBinaryUID(strUid, lpPropVal, lpPropVal);
	if (hr != hrSuccess)
		return hr;

	lpPropVal->ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY);

	hr = Util::CompareProp(lpPropVal, &lpIcalItem->sBinGuid, createLocaleFromName(""), &res);
	if (hr != hrSuccess || res != 0)
		hr = MAPI_E_BAD_VALUE;
	return hr;
}

/**
 * Sets UID property in icalitem mapi structure from the ical component.
 *
 * @param[in]	lpicEvent		ical component containing UID ical property
 * @param[in]	lpIcalItem		icalitem structure in which UID is stored
 * @return		MAPI error code
 */
HRESULT VConverter::HrAddUids(icalcomponent *lpicEvent, icalitem *lpIcalItem)
{
	SPropValue sPropValue;
	std::string strUid;
	
	// GlobalObjectId -> it has UID value & embeded Exception occurnece date for exceptions else 00000000
	// CleanGlobalObjectID -> it has UID value

	// Get Unique ID of ical item, or create new
	HRESULT hr = HrGetUID(lpicEvent, &strUid);
	if (hr != hrSuccess)
		hr = HrGenerateUid(&strUid);
	if (hr != hrSuccess)
		return hr;

	hr = HrMakeBinaryUID(strUid, lpIcalItem->base, &sPropValue);
	if (hr != hrSuccess)
		return hr;
	
	// sets exception date in GUID from recurrence-id
	hr = HrHandleExceptionGuid(lpicEvent, lpIcalItem->base, &sPropValue);
	if (hr != hrSuccess)
		return hr;

	// set as dispidGlobalObjectID ...
	sPropValue.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY);
	lpIcalItem->lstMsgProps.emplace_back(sPropValue);
	
	// replace date in GUID, the dispidCleanGlobalObjectID should be same for exceptions and reccurence message.
	// used for exceptions in outlook
	if(IsOutlookUid(strUid))
		strUid.replace(32, 8, "00000000");

	hr = HrMakeBinaryUID(strUid, lpIcalItem->base, &sPropValue);
	if (hr != hrSuccess)
		return hr;

	// set as dispidCleanGlobalObjectID...
	sPropValue.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_CLEANID], PT_BINARY);
	lpIcalItem->lstMsgProps.emplace_back(sPropValue);
	// save the strUid to lookup for occurrences
	lpIcalItem->sBinGuid = sPropValue;
	return hrSuccess;
}

/**
 * Sets the recurrence-id date in GlobalObjectID for exceptions in
 * SPropValue structure.
 *
 * @param[in]		lpiEvent	ical component containing the recurrence-id property
 * @param[in]		base		Base for memory allocation
 * @param[in,out]	lpsProp		SPropValue is modified to set the new GlobalObjectId
 *
 * @return			MAPI error code
 * @retval			MAPI_E_INVALID_PARAMETER	NULL for lpsProp parameter
 */
HRESULT VConverter::HrHandleExceptionGuid(icalcomponent *lpiEvent, void *base, SPropValue *lpsProp)
{
	char strHexDate[] = "00000000";
	
	if (lpsProp == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto icProp = icalcomponent_get_first_property(lpiEvent, ICAL_RECURRENCEID_PROPERTY);
	if (icProp == NULL)
		return hrSuccess; //ignoring Recurrence-ID.
	auto strUid = bin2hex(lpsProp->Value.bin);
	auto icTime = icaltime_from_timet_with_zone(ICalTimeTypeToUTC(lpiEvent, icProp), 0, nullptr);
	sprintf(strHexDate,"%04x%02x%02x", icTime.year, icTime.month, icTime.day);

	// Exception date is stored in GlobalObjectId
	strUid.replace(32, 8, strHexDate);
	auto strBinUid = hex2bin(strUid);

	lpsProp->Value.bin.cb = strBinUid.size();
	HRESULT hr = MAPIAllocateMore(strBinUid.size(), base,
	             reinterpret_cast<void **>(&lpsProp->Value.bin.lpb));
	if (hr != hrSuccess)
		return hr;
	memcpy(lpsProp->Value.bin.lpb, strBinUid.data(), lpsProp->Value.bin.cb);
	return hrSuccess;
}

/**
 * Sets Recurrence-id property for exceptions in mapi structure
 *
 * @param[in]		lpiEvent	ical component containing the recurrence-id property
 * @param[in,out]	lpIcalItem	icalitem structure in which the mapi properties are stored
 * @return			Always returns hrSuccess 
 */
HRESULT VConverter::HrAddRecurrenceID(icalcomponent *lpiEvent, icalitem *lpIcalItem)
{
	SPropValue sPropVal;
	icalproperty *icProp = icalcomponent_get_first_property(lpiEvent,
	                       ICAL_RECURRENCEID_PROPERTY);
	if (icProp == NULL)
		return hrSuccess;

	// if RECURRENCE-ID is date then series is all day,
	// so set the following properties as a flag to know if series is all day or not.
	if (icalproperty_get_recurrenceid(icProp).is_date)
	{
		// set RecurStartTime as 00:00 AM
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURSTARTTIME], PT_LONG);
		sPropVal.Value.ul = 0;
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
		
		// set RecurEndTime as 12:00 PM (24 hours)
		// 60 sec -> highest pow of 2 after 60 -> 64 
		// 60 mins -> 60 * 64 = 3840 -> highest pow of 2 after 3840 -> 4096
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURENDTIME], PT_LONG);
		sPropVal.Value.ul = 24 * 4096;
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	}

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRINGBASE], PT_SYSTIME);
	if (icalproperty_get_recurrenceid(icProp).is_date)
		UnixTimeToFileTime(icaltime_as_timet(icalproperty_get_recurrenceid (icProp)), &sPropVal.Value.ft);
	else
		UnixTimeToFileTime(ICalTimeTypeToLocal(icProp), &sPropVal.Value.ft);
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);	

	//RECURRENCE-ID is present only for exception
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_ISEXCEPTION], PT_BOOLEAN);
	sPropVal.Value.b = true;
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	return hrSuccess;
}

/**
 * Adds Static properties to mapi structure. These are properties that
 * do not directly depend on the event, but may depend on the ical
 * method.
 * 
 * Function sets properties such as PROP_SIDEEFFECT, PROP_SENDASICAL, PROP_COMMONASSIGN
 *
 * @param[in]		icMethod		ical method
 * @param[in,out]	lpIcalItem		structure in which mapi properties are set
 * @return			Always returns hrSuccess
 */
HRESULT VConverter::HrAddStaticProps(icalproperty_method icMethod, icalitem *lpIcalItem)
{
	SPropValue sPropVal;

	// From [MS-OXOCAL].pdf: All Calendar objects SHOULD include the following flags:
	sPropVal.Value.ul = seOpenToDelete | seOpenToCopy | seOpenToMove | seCoerceToInbox | seOpenForCtxMenu;
	//                      1               20             40             10               100 == 171
	if(icMethod == ICAL_METHOD_REPLY || icMethod == ICAL_METHOD_REQUEST || icMethod == ICAL_METHOD_CANCEL)
	{
		// 400 | 800 | 1000 == 1c00 -> 1d71 but 1c61 should be set (because outlook says so)
		sPropVal.Value.ul |= seCannotUndoDelete | seCannotUndoCopy | seCannotUndoMove;
		// thus disable coercetoinbox, openforctxmenu .. which outlook does aswell.
		sPropVal.Value.ul &= ~(seCoerceToInbox | seOpenForCtxMenu);
	}
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_SIDEEFFECT], PT_LONG);
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_SENDASICAL], PT_BOOLEAN);
	sPropVal.Value.b = 1;
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// Needed for deleting an occurrence of a recurring item in outlook
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_COMMONASSIGN], PT_LONG);
	sPropVal.Value.ul = 0;
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	return hrSuccess;
}

/**
 * Add Simple properties to mapi structure from ical data. These are
 * properties that map 1:1 from Ical to MAPI and do not need
 * complicated calculations.
 * 
 * Function sets summary, description, location, priority, private, sensitivity properties.
 *
 * @param[in]		lpicEvent	ical component containing the properties
 * @param[in,out]	lpIcalItem	mapi structure in which properties are set
 * @return			Always returns hrSuccess
 */	
HRESULT VConverter::HrAddSimpleHeaders(icalcomponent *lpicEvent, icalitem *lpIcalItem)
{
	SPropValue sPropVal;
	int lClass = 0;

	// Set subject / SUMMARY
	auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_SUMMARY_PROPERTY);
	if (lpicProp){
		sPropVal.ulPropTag = PR_SUBJECT_W;
		auto hr = HrCopyString(m_converter, m_strCharset, lpIcalItem->base, icalcomponent_get_summary(lpicEvent), &sPropVal.Value.lpszW);
		if (hr != hrSuccess)
			sPropVal.Value.lpszW = const_cast<wchar_t *>(L"");
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	} else {
		lpIcalItem->lstDelPropTags.emplace_back(PR_SUBJECT);
	}

	// Set body / DESCRIPTION
	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DESCRIPTION_PROPERTY);
	if (!lpicProp)
		// used by exchange on replies in meeting requests
		lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_COMMENT_PROPERTY);
	if (lpicProp){
		sPropVal.ulPropTag = PR_BODY_W;
		auto hr = HrCopyString(m_converter, m_strCharset, lpIcalItem->base, icalproperty_get_description(lpicProp), &sPropVal.Value.lpszW);
		if (hr != hrSuccess)
			sPropVal.Value.lpszW = const_cast<wchar_t *>(L"");
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	} else {
		lpIcalItem->lstDelPropTags.emplace_back(PR_BODY_W);
	}

	// Set location / LOCATION
	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_LOCATION_PROPERTY);
	if (lpicProp) {
		auto hr = HrCopyString(m_converter, m_strCharset, lpIcalItem->base, icalproperty_get_location(lpicProp), &sPropVal.Value.lpszW);
		if (hr != hrSuccess)
			sPropVal.Value.lpszW = const_cast<wchar_t *>(L"");

		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_LOCATION], PT_UNICODE);
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MEETINGLOCATION], PT_UNICODE);
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	} else {
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_LOCATION], PT_UNICODE));
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MEETINGLOCATION], PT_UNICODE));
	}

	// Set importance and priority / PRIORITY
	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_PRIORITY_PROPERTY);
	if (lpicProp) {
		auto lPriority = icalproperty_get_priority(lpicProp);
		// @todo: test input and output!
		if (lPriority == 0)
			;
		else if (lPriority < 5)
			lPriority = 1;
		else if (lPriority > 5)
			lPriority = -1;
		else
			lPriority = 0;
		
		sPropVal.ulPropTag = PR_IMPORTANCE;
		sPropVal.Value.ul = lPriority + 1;
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
		sPropVal.ulPropTag = PR_PRIORITY;
		sPropVal.Value.l = lPriority;
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	} else {
		lpIcalItem->lstDelPropTags.emplace_back(PR_IMPORTANCE);
		lpIcalItem->lstDelPropTags.emplace_back(PR_PRIORITY);
	}
	
	// Private
	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_CLASS_PROPERTY);
	if (lpicProp){
		lClass = icalproperty_get_class(lpicProp);
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PRIVATE], PT_BOOLEAN);
		sPropVal.Value.b = (lClass == ICAL_CLASS_PRIVATE);
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	}

	// Sensitivity, from same class property
	sPropVal.ulPropTag = PR_SENSITIVITY;
	if (lClass == ICAL_CLASS_PRIVATE)
		sPropVal.Value.ul = 2; // Private
	else if (lClass == ICAL_CLASS_CONFIDENTIAL)
		sPropVal.Value.ul = 3; // CompanyConfidential
	else
		sPropVal.Value.ul = 0; // Public
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	// hr not used with goto exit, always return success
	return hrSuccess;
}

/**
 * Sets busy status in mapi property from ical.
 * 
 * @param[in]	lpicEvent		ical VEVENT component
 * @param[in]	icMethod		ical method (e.g. REPLY, REQUEST)
 * @param[out]	lpIcalItem		icalitem in which mapi propertry is set
 * @return		MAPI error code
 */
HRESULT VConverter::HrAddBusyStatus(icalcomponent *lpicEvent, icalproperty_method icMethod, icalitem *lpIcalItem)
{
	SPropValue sPropVal;

	// default: busy
	// 0: free
	// 1: tentative
	// 2: busy
	// 3: oof
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_BUSYSTATUS], PT_LONG);
	// defaults if the TRANSP property is missing
	if (icMethod == ICAL_METHOD_CANCEL)
		sPropVal.Value.ul = 0;
	else
		sPropVal.Value.ul = 2;
	
	// caldav clients only uses the TRANSP property to set FreeBusy
	auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_TRANSP_PROPERTY);
	if (lpicProp) {
		switch (icalproperty_get_transp(lpicProp)) {
		case ICAL_TRANSP_TRANSPARENT: // free
		case ICAL_TRANSP_TRANSPARENTNOCONFLICT:
			sPropVal.Value.ul = 0;
			break;
		case ICAL_TRANSP_X:
		case ICAL_TRANSP_OPAQUE: // busy
		case ICAL_TRANSP_OPAQUENOCONFLICT:
		case ICAL_TRANSP_NONE:
        	sPropVal.Value.ul = 2;
			break;
		}
	}

	// Only process for Meeting Req from dagent
	if ((m_bNoRecipients && icMethod == ICAL_METHOD_REQUEST) || m_ulUserStatus == 5)
	    // Meeting requests always have a BusyStatus of 1 (tentative), since this is the status of
	    // the meeting which will be placed in your calendar when it has been processed but not accepted
	    // The busy status of meeting responses is less important but seems to be 2 (Busy) in Outlook.
		// If the attendee is editing the entry through caldav then if the PARTSTAT param is NEEDS-ACTION
		// then the meeting is marked as tentative.
		sPropVal.Value.ul = 1;

	lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	// save fbstatus in icalitem
	lpIcalItem->ulFbStatus = sPropVal.Value.ul;

	if (icMethod == ICAL_METHOD_REPLY) {
		// @note the documentation doesn't explain the -1 on replies, but
		// makes sense in the case that it shouldn't be used.
		sPropVal.Value.ul = -1;
	} else {
		// X-MICROSOFT-CDO-INTENDEDBUSYSTATUS is used to set IntendedBusyStatus
		for (lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_X_PROPERTY);
		     lpicProp != nullptr;
		     lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_X_PROPERTY))
		{
			// X-MICROSOFT-CDO-INTENDEDBUSYSTATUS:FREE
			if (strcmp(icalproperty_get_x_name(lpicProp), "X-MICROSOFT-CDO-INTENDEDSTATUS") != 0)
				continue;
			const char *lpVal = icalproperty_get_x(lpicProp);
			if (lpVal == NULL)
				sPropVal.Value.ul = 2; /* like else case */
			else if (strcmp(lpVal, "FREE") == 0)
				sPropVal.Value.ul = 0;
			else if (strcmp(lpVal, "TENTATIVE") == 0)
				sPropVal.Value.ul = 1;
			else if(strcmp(lpVal, "BUSY") == 0)
				sPropVal.Value.ul = 2;
			else if (strcmp(lpVal, "OOF") == 0)
				sPropVal.Value.ul = 3;
			else
				sPropVal.Value.ul = 2;
			break;
		}
		// if the value wasn't updated, it still contains the PROP_INTENDEDBUSYSTATUS value, which is what we want.
	}

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_INTENDEDBUSYSTATUS], PT_LONG);
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	return hrSuccess;
}

/**
 * Set X ical properties in mapi properties
 *
 * @param[in]	lpicEvent		ical component to search x ical properties
 * @param[out]	lpIcalItem		icalitem struture to store mapi properties
 * @return		Always returns hrSuccess
 */
HRESULT VConverter::HrAddXHeaders(icalcomponent *lpicEvent, icalitem *lpIcalItem)
{
	SPropValue sPropVal;
	int ulMaxCounter = 0;
	bool bHaveCounter = false;
	bool bOwnerApptID = false;
	bool bMozGen = false;
	
	// @todo: maybe save/restore headers to get "original" ical again?
	
	// add X-MICROSOFT-CDO & X-MOZ properties
	for (auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_X_PROPERTY);
	     lpicProp != nullptr;
	     lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_X_PROPERTY))
	{
		if (strcmp(icalproperty_get_x_name(lpicProp), "X-MICROSOFT-CDO-ATTENDEE-CRITICAL-CHANGE") == 0){
			auto lpicValue = icalvalue_new_from_string(ICAL_DATETIME_VALUE, icalproperty_get_x(lpicProp));
			if (lpicValue == nullptr)
				continue;
			auto ttCritcalChange = icaltime_as_timet_with_zone(icalvalue_get_datetime(lpicValue), NULL); // no timezone
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_ATTENDEECRITICALCHANGE], PT_SYSTIME);
			UnixTimeToFileTime(ttCritcalChange, &sPropVal.Value.ft);
			lpIcalItem->lstMsgProps.emplace_back(sPropVal);
			icalvalue_free(lpicValue);

		}else if (strcmp(icalproperty_get_x_name(lpicProp), "X-MICROSOFT-CDO-OWNER-CRITICAL-CHANGE") == 0){
			auto lpicValue = icalvalue_new_from_string(ICAL_DATETIME_VALUE, icalproperty_get_x(lpicProp));
			if (lpicValue == nullptr)
				continue;
			auto ttCritcalChange = icaltime_as_timet_with_zone(icalvalue_get_datetime(lpicValue), NULL); // no timezone
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_OWNERCRITICALCHANGE], PT_SYSTIME);
			UnixTimeToFileTime(ttCritcalChange, &sPropVal.Value.ft);
			lpIcalItem->lstMsgProps.emplace_back(sPropVal);
			icalvalue_free(lpicValue);

		}else if (strcmp(icalproperty_get_x_name(lpicProp), "X-MICROSOFT-CDO-OWNERAPPTID") == 0){
			auto lpicValue = icalvalue_new_from_string(ICAL_INTEGER_VALUE, icalproperty_get_x(lpicProp));
			if (lpicValue == nullptr)
				continue;
			sPropVal.ulPropTag = PR_OWNER_APPT_ID;
			sPropVal.Value.ul = icalvalue_get_integer(lpicValue);
			lpIcalItem->lstMsgProps.emplace_back(sPropVal);
			bOwnerApptID = true;
			icalvalue_free(lpicValue);

		}else if (strcmp(icalproperty_get_x_name(lpicProp), "X-MICROSOFT-CDO-APPT-SEQUENCE") == 0){
			auto lpicValue = icalvalue_new_from_string(ICAL_INTEGER_VALUE, icalproperty_get_x(lpicProp));
			if (lpicValue == nullptr)
				continue;
			ulMaxCounter = std::max(ulMaxCounter, icalvalue_get_integer(lpicValue));
			bHaveCounter = true;
			icalvalue_free(lpicValue);

		} else if (strcmp(icalproperty_get_x_name(lpicProp), "X-MOZ-GENERATION") == 0) {
			auto lpicValue = icalvalue_new_from_string(ICAL_INTEGER_VALUE, icalproperty_get_x(lpicProp));
			if (lpicValue == nullptr)
				continue;
			ulMaxCounter = std::max(ulMaxCounter, icalvalue_get_integer(lpicValue));
			bHaveCounter = bMozGen = true;
			icalvalue_free(lpicValue);

		} else if (strcmp(icalproperty_get_x_name(lpicProp), "X-MOZ-SEND-INVITATIONS") == 0) {
			auto lpicValue = icalvalue_new_from_string(ICAL_X_VALUE, icalproperty_get_x(lpicProp));
			if (lpicValue == nullptr)
				continue;
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MOZSENDINVITE], PT_BOOLEAN);
			const char *x = icalvalue_get_x(lpicValue);
			if (x == NULL)
				x = "";
			sPropVal.Value.b = strcmp(x, "TRUE") ? 0 : 1;
			lpIcalItem->lstMsgProps.emplace_back(sPropVal);
			icalvalue_free(lpicValue);
		}
	}

	auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_SEQUENCE_PROPERTY);
	if (lpicProp) {
		ulMaxCounter = std::max(ulMaxCounter, icalcomponent_get_sequence(lpicEvent));
		bHaveCounter = true;
	}

	// Add ApptSequenceNo only if its present in the ical data, see #6116
	if (bHaveCounter) {
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_APPTSEQNR], PT_LONG);
		sPropVal.Value.ul = ulMaxCounter;
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	}

	if (!bOwnerApptID) {
		sPropVal.ulPropTag = PR_OWNER_APPT_ID;
		sPropVal.Value.ul = -1;
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	}

	if (bMozGen) {
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MOZGEN], PT_LONG);
		sPropVal.Value.ul = ulMaxCounter;
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	}

	return hrSuccess;
}

/**
 * Sets Categories in mapi structure from ical data
 *
 * @param[in]	lpicEvent	ical component containing ical data
 * @param[in]	lpIcalItem	mapi structure in which the properties are set
 * @return		Always returns hrSuccess 
 */
HRESULT VConverter::HrAddCategories(icalcomponent *lpicEvent, icalitem *lpIcalItem)
{
	SPropValue sPropVal;
	const char* lpszCategories = NULL;
	std::vector<std::string> vCategories;

	// Set keywords / CATEGORIES
	auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_CATEGORIES_PROPERTY);
	if (!lpicProp) {
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_KEYWORDS], PT_MV_STRING8));
		return hrSuccess;
	}
	for (; lpicProp != nullptr && (lpszCategories = icalproperty_get_categories(lpicProp)) != nullptr;
	     lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_CATEGORIES_PROPERTY))
		vCategories.emplace_back(lpszCategories);

	HRESULT hr = MAPIAllocateMore(vCategories.size() * sizeof(LPSTR),
	            lpIcalItem->base, reinterpret_cast<void **>(&sPropVal.Value.MVszA.lppszA));
	if (hr != hrSuccess)
		return hr;

	int i = 0;
	for (const auto &cat : vCategories) {
		int length = cat.length() + 1;
		hr = MAPIAllocateMore(length, lpIcalItem->base, (void **) &sPropVal.Value.MVszA.lppszA[i]);
		if (hr != hrSuccess)
			return hr;
		memcpy(sPropVal.Value.MVszA.lppszA[i++], cat.c_str(), length);
	}

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_KEYWORDS], PT_MV_STRING8);
	sPropVal.Value.MVszA.cValues = vCategories.size();
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	return hrSuccess;
}

/** 
 * Set PR_SENT_REPRESENTING_* and PR_SENDER_* properties to mapi object.
 * 
 * @param[in] lpIcalItem Use base pointer from here for allocations
 * @param[in] lplstMsgProps add generated properties to this list
 * @param[in] strEmail email address of the organizer
 * @param[in] strName full name of the organizer
 * @param[in] strType SMTP or ZARAFA
 * @param[in] cbEntryID bytes in entryid
 * @param[in] lpEntryID entryid describing organizer
 * 
 * @return MAPI Error code
 */
HRESULT VConverter::HrAddOrganizer(icalitem *lpIcalItem, std::list<SPropValue> *lplstMsgProps, const std::wstring &strEmail, const std::wstring &strName, const std::string &strType, ULONG cbEntryID, LPENTRYID lpEntryID)
{
	SPropValue sPropVal;
	auto strSearchKey = strToUpper(strType + ":" + m_converter.convert_to<std::string>(strEmail));

	sPropVal.ulPropTag = PR_SENDER_ADDRTYPE_W;
	HRESULT hr = HrCopyString(m_converter, m_strCharset, lpIcalItem->base,
	             strType.c_str(), &sPropVal.Value.lpszW);
	if (hr != hrSuccess)
		return hr;
	lplstMsgProps->emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_SENT_REPRESENTING_ADDRTYPE;
	lplstMsgProps->emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_SENDER_EMAIL_ADDRESS_W;
	hr = HrCopyString(lpIcalItem->base, strEmail.c_str(), &sPropVal.Value.lpszW);
	if (hr != hrSuccess)
		return hr;
	lplstMsgProps->emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_SENT_REPRESENTING_EMAIL_ADDRESS;
	lplstMsgProps->emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_SENDER_NAME_W;
	hr = HrCopyString(lpIcalItem->base, strName.c_str(), &sPropVal.Value.lpszW);
	if (hr != hrSuccess)
		return hr;
	lplstMsgProps->emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_SENT_REPRESENTING_NAME;
	lplstMsgProps->emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_SENDER_SEARCH_KEY;
	hr = Util::HrCopyBinary(strSearchKey.length() + 1, (LPBYTE)strSearchKey.c_str(), &sPropVal.Value.bin.cb, &sPropVal.Value.bin.lpb, lpIcalItem->base);
	if (hr != hrSuccess)
		return hr;
	lplstMsgProps->emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_SENT_REPRESENTING_SEARCH_KEY;
	lplstMsgProps->emplace_back(sPropVal);

	// re-allocate memory to list with lpIcalItem
	hr = Util::HrCopyBinary(cbEntryID, (LPBYTE)lpEntryID, &sPropVal.Value.bin.cb, &sPropVal.Value.bin.lpb, lpIcalItem->base);
	if (hr != hrSuccess)
		return hr;

	sPropVal.ulPropTag = PR_SENDER_ENTRYID;
	lplstMsgProps->emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_SENT_REPRESENTING_ENTRYID;
	lplstMsgProps->emplace_back(sPropVal);
	return hrSuccess;
}

/**
 * Sets Recipients in mapi structure from the ical data
 *
 * @param[in]		lpicEvent		ical component containing ical data
 * @param[in,out]	lpIcalItem		mapi structure in which the properties are set
 * @param[in,out]	lplstMsgProps	List in which of mapi properties set
 * @param[out]		lplstIcalRecip	List containing mapi recipients
 * @return			MAPI error code
 */
HRESULT VConverter::HrAddRecipients(icalcomponent *lpicEvent, icalitem *lpIcalItem, std::list<SPropValue> *lplstMsgProps, std::list<icalrecip> *lplstIcalRecip)
{
	HRESULT hr = hrSuccess;
	std::wstring strEmail, strName;
	std::string strType;
	icalrecip icrAttendee = {0};
	ULONG cbEntryID = 0;
	LPENTRYID lpEntryID = NULL;
	ULONG cbEntryIDOneOff = 0;
	memory_ptr<ENTRYID> lpEntryIDOneOff;
	memory_ptr<SPropValue> lpsPropVal;

	auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_ORGANIZER_PROPERTY);
	if (lpicProp) {
		const char *tmp = icalproperty_get_organizer(lpicProp);
		strEmail = m_converter.convert_to<wstring>(tmp, rawsize(tmp), m_strCharset.c_str());
		if (wcsncasecmp(strEmail.c_str(), L"mailto:", 7) == 0)
			strEmail = strEmail.erase(0, 7);
		auto lpicParam = icalproperty_get_first_parameter(lpicProp, ICAL_CN_PARAMETER);
		tmp = icalparameter_get_cn(lpicParam);
		if (lpicParam != NULL)
			strName = m_converter.convert_to<wstring>(tmp, rawsize(tmp), m_strCharset.c_str());
		else
			strName = strEmail; // set email as name OL does not display organiser name if not set.

		if (bIsUserLoggedIn(strEmail)) {
			static constexpr const SizedSPropTagArray(4, sPropTags) =
				{4, {PR_SMTP_ADDRESS_W, PR_DISPLAY_NAME_W, PR_ADDRTYPE_A, PR_ENTRYID}};
			ULONG count;

			hr = m_lpMailUser->GetProps(sPropTags, 0, &count, &~lpsPropVal);
			if (hr != hrSuccess)
				return hr;
			if (lpsPropVal[0].ulPropTag == PR_SMTP_ADDRESS_W)
				strEmail = lpsPropVal[0].Value.lpszW;
			if (lpsPropVal[1].ulPropTag == PR_DISPLAY_NAME_W)
				strName = lpsPropVal[1].Value.lpszW;
			if (lpsPropVal[2].ulPropTag == PR_ADDRTYPE_A)
				strType = lpsPropVal[2].Value.lpszA;
			if (lpsPropVal[3].ulPropTag == PR_ENTRYID) {
				cbEntryID = lpsPropVal[3].Value.bin.cb;
				lpEntryID = (LPENTRYID)lpsPropVal[3].Value.bin.lpb;
			}
		} else {
			strType = "SMTP";
			hr = ECCreateOneOff((LPTSTR)strName.c_str(), (LPTSTR)L"SMTP", (LPTSTR)strEmail.c_str(), MAPI_UNICODE, &cbEntryIDOneOff, &~lpEntryIDOneOff);
			if (hr != hrSuccess)
				return hr;
			cbEntryID = cbEntryIDOneOff;
			lpEntryID = lpEntryIDOneOff;
		}

		// add the organiser to the recipient list
		hr = MAPIAllocateMore(cbEntryID, lpIcalItem->base, (void**)&icrAttendee.lpEntryID);
		if (hr != hrSuccess)
			return hr;
		
		memcpy(icrAttendee.lpEntryID, lpEntryID, cbEntryID);
		icrAttendee.cbEntryID = cbEntryID;

		icrAttendee.strEmail = strEmail;
		icrAttendee.strName = strName;
		icrAttendee.ulRecipientType = MAPI_ORIG;
		icrAttendee.ulTrackStatus = 0;
		lplstIcalRecip->emplace_back(icrAttendee);

		// The DAgent does not want these properties from ical, since it writes them itself
		if (!m_bNoRecipients)
			hr = HrAddOrganizer(lpIcalItem, lplstMsgProps, strEmail, strName, strType, cbEntryID, lpEntryID);
	} else if (!m_bNoRecipients && m_lpMailUser) {
		// single item from caldav without organizer, no need to set recipients, only organizer to self
		static constexpr const SizedSPropTagArray(4, sPropTags) =
			{4, {PR_SMTP_ADDRESS_W, PR_DISPLAY_NAME_W, PR_ADDRTYPE_A, PR_ENTRYID}};
		ULONG count;

		hr = m_lpMailUser->GetProps(sPropTags, 0, &count, &~lpsPropVal);
		if (hr != hrSuccess)
			return hr;
		if (lpsPropVal[0].ulPropTag == PR_SMTP_ADDRESS_W)
			strEmail = lpsPropVal[0].Value.lpszW;
		if (lpsPropVal[1].ulPropTag == PR_DISPLAY_NAME_W)
			strName = lpsPropVal[1].Value.lpszW;
		if (lpsPropVal[2].ulPropTag == PR_ADDRTYPE_A)
			strType = lpsPropVal[2].Value.lpszA;
		if (lpsPropVal[3].ulPropTag == PR_ENTRYID) {
			cbEntryID = lpsPropVal[3].Value.bin.cb;
			lpEntryID = (LPENTRYID)lpsPropVal[3].Value.bin.lpb;
		}

		hr = HrAddOrganizer(lpIcalItem, lplstMsgProps, strEmail, strName, strType, cbEntryID, lpEntryID);
	}
	if (hr != hrSuccess)
		return hr;

	for (auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_ATTENDEE_PROPERTY);
		 lpicProp != NULL;
		 lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_ATTENDEE_PROPERTY))
	{
		const char *tmp = icalproperty_get_attendee(lpicProp);

		/* Temporary fix for #7740:
		 * Newer libical already fixed the problem where "invalid" parameters let libical return a NULL pointer here,
		 * but since libical svn is not binary backward compatible, we can't just upgrade the library, which we really should.
		 */
		if (!tmp)
			// unable to log error of missing attendee
			continue;

		icrAttendee.strEmail = m_converter.convert_to<wstring>(tmp, rawsize(tmp), m_strCharset.c_str());
		if (wcsncasecmp(icrAttendee.strEmail.c_str(), L"mailto:", 7) == 0)
			icrAttendee.strEmail.erase(0, 7);

		// @todo: Add organiser details if required.
		if(icrAttendee.strEmail == strEmail) // remove organiser from attendee list.
			continue;
		
		auto lpicParam = icalproperty_get_first_parameter(lpicProp, ICAL_CN_PARAMETER);
		if (lpicParam) {
			const char *lpszProp = icalparameter_get_cn(lpicParam);
			icrAttendee.strName = m_converter.convert_to<std::wstring>(lpszProp, rawsize(lpszProp), m_strCharset.c_str());
		} else
			icrAttendee.strName = icrAttendee.strEmail;

		lpicParam = icalproperty_get_first_parameter(lpicProp, ICAL_ROLE_PARAMETER);
		if (!lpicParam) {
			icrAttendee.ulRecipientType = MAPI_TO;
		} else {
			switch (icalparameter_get_role(lpicParam)) {
			case ICAL_ROLE_OPTPARTICIPANT:
				icrAttendee.ulRecipientType = MAPI_CC;
				break;
			case ICAL_ROLE_NONPARTICIPANT:
				icrAttendee.ulRecipientType = MAPI_BCC;
				break;
			case ICAL_ROLE_REQPARTICIPANT:
			default:
				icrAttendee.ulRecipientType = MAPI_TO;
				break;
			}
		}

		lpicParam = icalproperty_get_first_parameter(lpicProp, ICAL_PARTSTAT_PARAMETER);
		if (lpicParam) {
			switch (icalparameter_get_partstat(lpicParam)) {
			case ICAL_PARTSTAT_TENTATIVE:
				icrAttendee.ulTrackStatus = 2;
				break;
			case ICAL_PARTSTAT_ACCEPTED:
				icrAttendee.ulTrackStatus = 3;
				break;
			case ICAL_PARTSTAT_DECLINED:
				icrAttendee.ulTrackStatus = 4;
				break;
			case ICAL_PARTSTAT_NEEDSACTION:
				icrAttendee.ulTrackStatus = 5;
				break;
			default:
				icrAttendee.ulTrackStatus = 0;
				break;
			}
		}
		
		lplstIcalRecip->emplace_back(icrAttendee);
	}
	return hrSuccess;
}

/**
 * Set Recipients for REPLY
 * 
 * @param[in]		lpicEvent		ical component containing ical properties
 * @param[in,out]	lpIcalItem		mapi structure in which properties are set
 * @return			MAPI error code
 */
HRESULT VConverter::HrAddReplyRecipients(icalcomponent *lpicEvent, icalitem *lpIcalItem)
{
	wstring strEmail, strName;
	icalrecip icrAttendee;
	ULONG cbEntryID;
	memory_ptr<ENTRYID> lpEntryID;

	auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_ORGANIZER_PROPERTY);
	if (lpicProp) {
		const char *lpszProp = icalproperty_get_organizer(lpicProp);
		icrAttendee.strEmail = m_converter.convert_to<std::wstring>(lpszProp, rawsize(lpszProp), m_strCharset.c_str());
		if (wcsncasecmp(icrAttendee.strEmail.c_str(), L"mailto:", 7) == 0)
			icrAttendee.strEmail.erase(0, 7);
		auto lpicParam = icalproperty_get_first_parameter(lpicProp, ICAL_CN_PARAMETER);
		if (lpicParam != NULL) {
			lpszProp = icalparameter_get_cn(lpicParam);
			icrAttendee.strName = m_converter.convert_to<std::wstring>(lpszProp, rawsize(lpszProp), m_strCharset.c_str());
		}

		icrAttendee.ulRecipientType = MAPI_TO;
		lpIcalItem->lstRecips.emplace_back(icrAttendee);
	}

	// The DAgent does not want these properties from ical, since it writes them itself
	if (m_bNoRecipients)
		return hrSuccess;

	// @todo: what if >1 attendee ?!?
	//PR_SENDER = ATTENDEE
	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_ATTENDEE_PROPERTY);
	if (lpicProp) {
		const char *lpszProp = icalproperty_get_attendee(lpicProp);
		strEmail = m_converter.convert_to<std::wstring>(lpszProp, rawsize(lpszProp), m_strCharset.c_str());
		if (wcsncasecmp(strEmail.c_str(), L"mailto:", 7) == 0)
			strEmail.erase(0, 7);
		auto lpicParam = icalproperty_get_first_parameter(lpicProp, ICAL_CN_PARAMETER);
		if (lpicParam) {
			lpszProp = icalparameter_get_cn(lpicParam);
			strName = m_converter.convert_to<std::wstring>(lpszProp, rawsize(lpszProp), m_strCharset.c_str());
		}
	}
	auto hr = ECCreateOneOff((LPTSTR)strName.c_str(), (LPTSTR)L"SMTP", (LPTSTR)strEmail.c_str(), MAPI_UNICODE, &cbEntryID, &~lpEntryID);
	if (hr != hrSuccess)
		return hr;
	return HrAddOrganizer(lpIcalItem, &lpIcalItem->lstMsgProps, strEmail, strName, "SMTP", cbEntryID, lpEntryID);
}

/**
 * Sets reminder in mapi structure from ical data
 *
 * @param[in]		lpicEventRoot	Root VCALENDAR component
 * @param[in]		lpicEvent		ical component containing the reminder
 * @param[in,out]	lpIcalItem		Structure in which remiders are stored
 * @return			Always returns hrSuccess 
 */
HRESULT VConverter::HrAddReminder(icalcomponent *lpicEventRoot, icalcomponent *lpicEvent, icalitem *lpIcalItem)
{
	SPropValue sPropVal;
	SPropValue sPropMozAck;
	LONG ulRemindBefore = 0;
	time_t ttReminderTime = 0;
	time_t ttReminderNext = 0;
	time_t ttMozLastAckMax = 0;
	bool bReminderSet = false;
	bool bHasMozAck = false;

	auto lpicAlarm = icalcomponent_get_first_component(lpicEvent, ICAL_VALARM_COMPONENT);
	if (lpicAlarm == NULL) {
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERSET], PT_BOOLEAN);
		sPropVal.Value.b = false;
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERTIME], PT_SYSTIME));
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERNEXTTIME], PT_SYSTIME));
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERMINUTESBEFORESTART], PT_LONG));
		/* No alarms found, so we can safely exit here. */
		return hrSuccess;
	}

	HRESULT hr = HrParseVAlarm(lpicAlarm, &ulRemindBefore, &ttReminderTime,
	             &bReminderSet);
	if (hr != hrSuccess)
		// just skip the reminder
		return hrSuccess;

	// Handle Sunbird's dismiss/snooze, see: https://wiki.mozilla.org/Calendar:Feature_Implementations:Alarms
	// X-MOZ-SNOOZE-TIME-1231250400000000:20090107T132846Z
	// X-MOZ-LASTACK:20090107T132846Z
	for (auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_X_PROPERTY);
	     lpicProp != nullptr;
	     lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_X_PROPERTY))
	{
		if (strcmp(icalproperty_get_x_name(lpicProp), "X-MOZ-LASTACK") == 0){
			auto lpicValue = icalvalue_new_from_string(ICAL_DATETIME_VALUE, icalproperty_get_x(lpicProp));
			auto ttMozLastAck = icaltime_as_timet_with_zone(icalvalue_get_datetime(lpicValue), NULL);
			if(ttMozLastAck > ttMozLastAckMax)//save max of X-MOZ-LAST-ACK if present twice.
				ttMozLastAckMax = ttMozLastAck;
			icalvalue_free(lpicValue);
			bHasMozAck = true;
		}
		else if (strcmp(icalproperty_get_x_name(lpicProp), "X-MOZ-SNOOZE-TIME") == 0) {
			// x properties always return a char* as value :(
			auto lpicValue = icalvalue_new_from_string(ICAL_DATETIME_VALUE, icalproperty_get_x(lpicProp));
			ttReminderNext = icaltime_as_timet_with_zone(icalvalue_get_datetime(lpicValue), NULL); // no timezone			
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERNEXTTIME], PT_SYSTIME);
			UnixTimeToFileTime(ttReminderNext, &sPropVal.Value.ft);
			lpIcalItem->lstMsgProps.emplace_back(sPropVal);
			
			// X-MOZ-SNOOZE-TIME-1231250400000000
			std::string strSuffix = icalproperty_get_x_name(lpicProp);
			if(strSuffix.compare("X-MOZ-SNOOZE-TIME") != 0)
			{
				sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MOZ_SNOOZE_SUFFIX], PT_SYSTIME);
				strSuffix.erase(0, strlen("X-MOZ-SNOOZE-TIME-"));
				strSuffix.erase(10);									// ignoring trailing 6 zeros for hh:mm:ss
				UnixTimeToFileTime(atoi(strSuffix.c_str()), &sPropVal.Value.ft);
				lpIcalItem->lstMsgProps.emplace_back(sPropVal);
			}
			icalvalue_free(lpicValue);
		} else if (strcmp(icalproperty_get_x_name(lpicProp), "X-MICROSOFT-RTF") == 0) {
			auto lpicValue = icalvalue_new_from_string(ICAL_X_VALUE, icalproperty_get_x(lpicProp));
			string rtf = base64_decode(icalvalue_get_x(lpicValue));
			sPropVal.ulPropTag = PR_RTF_COMPRESSED;
			sPropVal.Value.bin.cb = rtf.size();
			
			hr = MAPIAllocateMore(sPropVal.Value.bin.cb,
			     lpIcalItem->base, reinterpret_cast<void **>(&sPropVal.Value.bin.lpb));
			if (hr != hrSuccess)
				return hr;
			memcpy(sPropVal.Value.bin.lpb, (LPBYTE)rtf.c_str(), sPropVal.Value.bin.cb);
			lpIcalItem->lstMsgProps.emplace_back(sPropVal);
			icalvalue_free(lpicValue);
		}

	}

	if (bHasMozAck) { // save X-MOZ-LAST-ACK if found in request.
		sPropMozAck.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MOZLASTACK], PT_SYSTIME);
		UnixTimeToFileTime(ttMozLastAckMax, &sPropMozAck.Value.ft);		
		lpIcalItem->lstMsgProps.emplace_back(sPropMozAck);
	}
	else { //delete X-MOZ-LAST-ACK if not found in request.
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MOZLASTACK], PT_SYSTIME));		
	}

	// reminderset
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERSET], PT_BOOLEAN);
	sPropVal.Value.b = bReminderSet;
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// remindbefore
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERMINUTESBEFORESTART], PT_LONG);
	sPropVal.Value.ul = ulRemindBefore;
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// remindertime
	if (ttReminderTime == 0) {
		// get starttime from item
		// DTSTART must be available
		auto lpicDTStartProp = icalcomponent_get_first_property(lpicEvent, ICAL_DTSTART_PROPERTY);
		if (lpicDTStartProp == NULL)
			return MAPI_E_INVALID_PARAMETER;
		ttReminderTime = ICalTimeTypeToUTC(lpicEventRoot, lpicDTStartProp);
	}
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERTIME], PT_SYSTIME);
	UnixTimeToFileTime(ttReminderTime, &sPropVal.Value.ft);
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	if (ttReminderNext != 0)
		return hrSuccess;
	if (!bReminderSet) {
		//delete the next-reminder time if X-MOZ-SNOOZE-TIME is absent and reminder is not set.
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERNEXTTIME], PT_SYSTIME));
		return hrSuccess;
	}
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERNEXTTIME], PT_SYSTIME);
	UnixTimeToFileTime(ttReminderTime - (ulRemindBefore * 60), &sPropVal.Value.ft);
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	return hrSuccess;
}

/**
 * Adds recurrence to icalitem structure from ical data
 *
 * @param[in]	lpicEventRoot	Root VCALENDAR component
 * @param[in]	lpicEvent		ical(VEVENT/VTODO) component being parsed
 * @param[in]	bIsAllday		set times for normal or allday event
 * @param[out]	lpIcalItem		icalitem structure in which the properties are set
 * @return		MAPI error code
 * @retval		MAPI_E_CORRUPT_DATA		timezone is not set in ical data
 * @retval		MAPI_E_NOT_FOUND		invalid recurrence is set
 */
HRESULT VConverter::HrAddRecurrence(icalcomponent *lpicEventRoot, icalcomponent *lpicEvent, bool bIsAllday, icalitem *lpIcalItem)
{
	SPropValue spSpropVal = {0};
	TIMEZONE_STRUCT zone;
	icalproperty *lpicProp = icalcomponent_get_first_property(lpicEvent,
	                         ICAL_RRULE_PROPERTY);
	if (lpicProp == NULL) {

		// set isRecurring to false , property required by BlackBerry.
		spSpropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRING], PT_BOOLEAN);
		spSpropVal.Value.b = false;
		lpIcalItem->lstMsgProps.emplace_back(spSpropVal);
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRENCESTATE], PT_BINARY));
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRENCEPATTERN], PT_STRING8));
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRENCE_START], PT_SYSTIME));
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRENCE_END], PT_SYSTIME));
		lpIcalItem->lpRecurrence.reset();
		// remove all exception attachments from existing message, done in ICal2Mapi.cpp
		return hrSuccess;
	}

	if (!bIsAllday && m_iCurrentTimeZone == m_mapTimeZones->end()) {
		// if we have an RRULE, we must have a timezone
		return MAPI_E_CORRUPT_DATA;
	} else if (m_iCurrentTimeZone == m_mapTimeZones->end()) {
		auto hr = HrGetTzStruct("Etc/UTC", &zone);
		if (hr != hrSuccess)
			return hr;
	} else {
		zone = m_iCurrentTimeZone->second;
	}
	auto hr = ICalRecurrence().HrParseICalRecurrenceRule(zone, lpicEventRoot,
	          lpicEvent, bIsAllday, m_lpNamedProps, lpIcalItem);
	if (hr != hrSuccess)
		return hr;

	for (lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_X_PROPERTY);
	     lpicProp != nullptr;
	     lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_X_PROPERTY))
		if (strcmp(icalproperty_get_x_name(lpicProp), "X-ZARAFA-REC-PATTERN") == 0 ||
		    strcmp(icalproperty_get_x_name(lpicProp), "X-KOPANO-REC-PATTERN") == 0) {
			spSpropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRENCEPATTERN], PT_UNICODE);
			HrCopyString(m_converter, m_strCharset, lpIcalItem->base, icalproperty_get_x(lpicProp), &spSpropVal.Value.lpszW);
			lpIcalItem->lstMsgProps.emplace_back(spSpropVal);
		}
	return hrSuccess;
}

/** 
 * Make a MAPI exception message, and add this to the previous parsed
 * icalitem (which is the main ical item).
 * 
 * @param[in] lpEventRoot		The top ical event which is recurring
 * @param[in] lpEvent			The current ical event describing an exception for lpEventRoot
 * @param[in] bIsAllday			set times for normal or allday event
 * @param[in,out] lpPrevItem	The icalitem struct that contains the MAPI representation of this recurrent item
 * 
 * @return MAPI error code
 */
HRESULT VConverter::HrAddException(icalcomponent *lpEventRoot, icalcomponent *lpEvent, bool bIsAllday, icalitem *lpPrevItem)
{
	icalitem::exception ex;
	auto hr = HrCompareUids(lpPrevItem, lpEvent);
	if (hr != hrSuccess)
		return hr;

	if (lpPrevItem->lpRecurrence == NULL)
		// can't add exceptions if the previous item did not have an RRULE
		return MAPI_E_CORRUPT_DATA;

	auto icMethod = icalcomponent_get_method(lpEventRoot);

	// it's the same item, handle exception
	hr = ICalRecurrence().HrMakeMAPIException(lpEventRoot, lpEvent, lpPrevItem, bIsAllday, m_lpNamedProps, m_strCharset, &ex);
	if (hr != hrSuccess)
		return hr;
	hr = HrAddRecipients(lpEvent, lpPrevItem, &ex.lstMsgProps, &ex.lstRecips);
	if (hr != hrSuccess)
		return hr;
	hr = HrResolveUser(lpPrevItem->base, &ex.lstRecips);
	if (hr != hrSuccess)
		return hr;
	hr = HrAddBaseProperties(icMethod, lpEvent, lpPrevItem->base, true, &ex.lstMsgProps);
	if (hr != hrSuccess)
		return hr;
	lpPrevItem->lstExceptionAttachments.emplace_back(std::move(ex));
	return hrSuccess;
}

/** 
 * Returns the ical timezone of a MAPI calendar message. When there is no timezone information, UTC will be used.
 * 
 * @param[in]  ulProps Number of properties in lpProps
 * @param[in]  lpProps All (required) properties of the MAPI message
 * @param[out] lpstrTZid The name (unique id) of the timezone
 * @param[out] lpTZinfo MAPI timezone struct
 * @param[out] lppicTZinfo Ical timezone information
 * 
 * @return MAPI error code
 */
HRESULT VConverter::HrFindTimezone(ULONG ulProps, LPSPropValue lpProps, std::string *lpstrTZid, TIMEZONE_STRUCT *lpTZinfo, icaltimezone **lppicTZinfo)
{
	HRESULT hr = hrSuccess;
	string strTZid;
	TIMEZONE_STRUCT ttTZinfo = {0};
	icaltimezone *lpicTZinfo = NULL;
	icalcomponent *lpicComp = NULL;
	size_t ulPos = 0;

	// @todo if we ever encounter a timezone string with non-ascii characters, we need to move to std::wstring for the timezone string.
	// but since I haven't seen this, I'll be lazy and do a convert to us-ascii strings.

	// Retrieve timezone. If available (outlook fills this in for recurring items), place it in lpMapTimeZones
	auto lpPropTimeZoneString = PCpropFindProp(lpProps, ulProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TIMEZONE], PT_UNICODE));
	if (lpPropTimeZoneString == NULL) {
		// use all dates/times as UTC
		strTZid = "(GMT+0000)";
		
		auto lpProp = PCpropFindProp(lpProps, ulProps, PR_MESSAGE_CLASS_W);
		if(lpProp && (wcscasecmp(lpProp->Value.lpszW, L"IPM.Task") == 0) && !m_mapTimeZones->empty())
		{
			m_iCurrentTimeZone = m_mapTimeZones->begin();
			ttTZinfo = m_iCurrentTimeZone->second;			
			strTZid = m_iCurrentTimeZone->first;
		}
		else
			goto done;	// UTC is not placed in the map, and not placed in ical, so we're done here
	} else
		strTZid = m_converter.convert_to<std::string>(lpPropTimeZoneString->Value.lpszW);
	if (strTZid.empty()) {
		strTZid = "(GMT+0000)";
		// UTC not in map, ttTZInfo still 0
		goto done;
	}

	if (strTZid[0] == '(') {
		// shorten string so the timezone id is not so long, and hopefully matches more for the same timezone

		// from: (GMT+01:00) Amsterdam, Berlijn, Bern, Rome, Stockholm, Wenen
		// from: (GMT+01.00) Sarajevo/Warsaw/Zagreb
		// to: (GMT+01:00) (note: the dot is not converted .. should we?)
		auto pos = strTZid.rfind(')');
		strTZid.erase(pos+1);
	}
	ulPos = strTZid.find('+');

	// check if strTZid is in map
	m_iCurrentTimeZone = m_mapTimeZones->find(strTZid);
	if (m_iCurrentTimeZone != m_mapTimeZones->end()) {
		// already used this timezone before
		ttTZinfo = m_iCurrentTimeZone->second;
	} else {
		auto lpPropTimeZoneStruct = PCpropFindProp(lpProps, ulProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TIMEZONEDATA], PT_BINARY));
		if (lpPropTimeZoneStruct && lpPropTimeZoneStruct->Value.bin.cb >= sizeof(TIMEZONE_STRUCT) && lpPropTimeZoneStruct->Value.bin.lpb) {
			ttTZinfo = *(TIMEZONE_STRUCT*)lpPropTimeZoneStruct->Value.bin.lpb;
			(*m_mapTimeZones)[strTZid] = ttTZinfo;
			// keep timezone pointer for recurrence
			m_iCurrentTimeZone = m_mapTimeZones->find(strTZid);
		} else if(ulPos != string::npos){
			strTZid = "(GMT+0000)"; // identify GMT+XXX timezones
			goto done;
		}
		else {
			strTZid = "(GMT-0000)"; // identify GMT-XXX timezones
			goto done;
		}
	}

	// construct ical version for icaltime_from_timet_with_zone()
	hr = HrCreateVTimeZone(strTZid, ttTZinfo, &lpicComp);
	if (hr == hrSuccess) {
		lpicTZinfo = icaltimezone_new();
		if (icaltimezone_set_component(lpicTZinfo, lpicComp) == 0) {
			icalcomponent_free(lpicComp);
			icaltimezone_free(lpicTZinfo, true);
		}
	}
	hr = hrSuccess;

done:
	*lpstrTZid = std::move(strTZid);
	*lpTZinfo = ttTZinfo;
	*lppicTZinfo = lpicTZinfo;

	return hr;
}

HRESULT VConverter::HrSetTimeProperty(time_t tStamp, bool bDateOnly, icaltimezone *lpicTZinfo, const std::string &strTZid, icalproperty_kind icalkind, icalproperty *lpicProp)
{
	icaltimetype ittStamp;

	// if (bDateOnly && !lpicTZinfo)
	/*
	 * ZCP-12962: Disregarding tzinfo. Even a minor miscalculation can
	 * cause a day shift; if possible, we should probably improve the
	 * actual calculation when we encounter such a problem.
	 */
	if (bDateOnly) {
		struct tm date;
		// We have a problem now. This is a 'date' property type, so time information should not be sent. However,
		// the timestamp in tStamp *does* have a time part, which is indicating the start of the day in GMT (so, this
		// would be say 23:00 in central europe, and 03:00 in brasil). This means that if we 'just' take the date part
		// of the timestamp, you will get the wrong day if you're east of GMT. Unfortunately, we don't know the
		// timezone either, so we have to do some guesswork. What we do now is a 'round to closest date'. This will
		// basically work for any timezone that has an offset between GMT+13 and GMT-10. So the 4th at 23:00 will become
		// the 5h, and the 5th at 03:00 will become the 5th.

		/* So this is a known problem for users in GMT+14, GMT-12 and
		 * GMT-11 (Kiribati, Samoa, ..). Sorry. Fortunately, there are
		 * not many people in these timezones. For this to work
		 * correctly, clients should store the correct timezone in the
		 * appointment (WebApp does not do this currently), and we need
		 * to consider timezones here again.
		 */
		gmtime_r(&tStamp, &date);
		if (date.tm_hour >= 11)
			// Move timestamp up one day so that later conversion to date-only will be correct
			tStamp += 86400;
	}
	
	if (!bDateOnly && lpicTZinfo != NULL)
		ittStamp = icaltime_from_timet_with_zone(tStamp, bDateOnly, lpicTZinfo);
	else
		ittStamp = icaltime_from_timet_with_zone(tStamp, bDateOnly, icaltimezone_get_utc_timezone());

	icalproperty_set_value(lpicProp, icalvalue_new_datetime(ittStamp));

	// only allowed to add timezone information on non-allday events
	if (lpicTZinfo && !bDateOnly)
		icalproperty_add_parameter(lpicProp, icalparameter_new_from_value_string(ICAL_TZID_PARAMETER, strTZid.c_str()));

	return hrSuccess;
}

/**
 * Converts the Unix timestamp to iCal information and adds a new iCal
 * property to the given ical component.
 *
 * @param[in]  tStamp The Unix timestamp value to set in the iCal property
 * @param[in]  bDateOnly true if only the date should be set (all day events) or false for full time conversion
 * @param[in]  lpicTZinfo Pointer to ical timezone for this property (required for recurring events). If NULL, UTC will be used.
 * @param[in]  strTZid Human readable name of the timezone
 * @param[in]  icalkind Kind of property the timestamp is describing
 * @param[in,out] lpicEvent Ical property will be added to this event, when hrSuccess is returned.
 *
 * @return MAPI error code
*/
HRESULT VConverter::HrSetTimeProperty(time_t tStamp, bool bDateOnly, icaltimezone *lpicTZinfo, const std::string &strTZid, icalproperty_kind icalkind, icalcomponent *lpicEvent)
{
	icalproperty *lpicProp = icalproperty_new(icalkind);
	if (lpicProp == NULL)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = HrSetTimeProperty(tStamp, bDateOnly, lpicTZinfo, strTZid,
	             icalkind, lpicProp);
	icalcomponent_add_property(lpicEvent, lpicProp);
	return hr;
}

/** 
 * Sets the Organizer (From) and Attendees (To and Cc) in the given
 * ical event. It also determains the ical method for this event,
 * since the method and attendees depend on the message class and the
 * meeting status.
 * 
 * Adds one or more of the following ical properties:
 * - STATUS
 * - ATTENDEE
 * - ORGANIZER
 * - X-MOZ-SEND-INVITATIONS
 *
 * @param[in]  lpParentMsg The main message (different from lpMessage in case of exceptions)
 * @param[in]  lpMessage The main or exception message
 * @param[in]  ulProps Number of properties in lpProps
 * @param[in]  lpProps All (required) properties from lpMessage
 * @param[in]  lpicMethod The method for this ical event
 * @param[in,out] lpicEvent This ical event will be modified
 * 
 * @return MAPI error code
 */
HRESULT VConverter::HrSetOrganizerAndAttendees(LPMESSAGE lpParentMsg, LPMESSAGE lpMessage, ULONG ulProps, LPSPropValue lpProps, icalproperty_method *lpicMethod, icalcomponent *lpicEvent)
{
	icalproperty_method icMethod = ICAL_METHOD_NONE;
	wstring strSenderName, strSenderType, strSenderEmailAddr;
	wstring strReceiverName, strReceiverType, strReceiverEmailAddr;
	wstring strRepsSenderName, strRepsSenderType, strRepsSenderEmailAddr;
	object_ptr<IMAPITable> lpTable;
	memory_ptr<SPropValue> lpSpropVal;
	icalparameter *lpicParam = NULL;
	ULONG ulMeetingStatus = 0;
	bool bCounterProposal = false;

	auto lpPropVal = PCpropFindProp(lpProps, ulProps, m_lpNamedProps->aulPropTag[PROP_COUNTERPROPOSAL]);
	if(lpPropVal && PROP_TYPE(lpPropVal->ulPropTag) == PT_BOOLEAN && lpPropVal->Value.b)
		bCounterProposal = true;
	
	//Remove Organiser & Attendees of Root event for exception.
	auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_ORGANIZER_PROPERTY);
	if (lpicProp) {
		icalcomponent_remove_property(lpicEvent, lpicProp);
		icalproperty_free(lpicProp);
	}
	
	for (lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_ATTENDEE_PROPERTY);
	     lpicProp != nullptr;
	     lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_X_PROPERTY))
	{
		icalcomponent_remove_property(lpicEvent, lpicProp);
		icalproperty_free(lpicProp);
	}

	// PR_SENT_REPRESENTING_ENTRYID is the owner of the meeting.
	// PR_SENDER_ENTRYID can be a delegate/owner
	lpPropVal = PCpropFindProp(lpProps, ulProps, PR_SENT_REPRESENTING_ENTRYID);
	if (lpPropVal) // ignore error
		HrGetAddress(m_lpAdrBook, (LPENTRYID)lpPropVal->Value.bin.lpb, lpPropVal->Value.bin.cb, strRepsSenderName, strRepsSenderType, strRepsSenderEmailAddr);

	// Request mail address from addressbook to get the actual email address
	// use the parent message, OL does not set PR_SENDER_ENTRYID in exception message.
	auto hr = HrGetAddress(m_lpAdrBook, lpParentMsg,
					  PR_SENDER_ENTRYID, PR_SENDER_NAME, PR_SENDER_ADDRTYPE, PR_SENDER_EMAIL_ADDRESS,
					  strSenderName, strSenderType, strSenderEmailAddr);
	if (hr != hrSuccess)
		return hr;

	// get class to find method and type for attendees and organizer
	lpPropVal = PCpropFindProp(lpProps, ulProps, PR_MESSAGE_CLASS_W);
	if (lpPropVal == nullptr)
		return MAPI_E_NOT_FOUND;

	auto strMessageClass = m_converter.convert_to<std::string>(lpPropVal->Value.lpszW);
	// Set attendee info
	if (strMessageClass.compare(0, string("IPM.Schedule.Meeting.Resp.").length(), string("IPM.Schedule.Meeting.Resp.")) == 0)
	{
		// responding to a meeting request:
		// the to should only be the organizer of this meeting
		if(bCounterProposal)
			icMethod = ICAL_METHOD_COUNTER;
		else {
			icMethod = ICAL_METHOD_REPLY;

			// gmail always sets CONFIRMED, exchange fills in the correct value ... doesn't seem to matter .. for now
			icalcomponent_add_property(lpicEvent, icalproperty_new_status(ICAL_STATUS_CONFIRMED));
		}

		if (strMessageClass.rfind("Pos") != string::npos)
			lpicParam = icalparameter_new_partstat(ICAL_PARTSTAT_ACCEPTED);
		else if (strMessageClass.rfind("Neg") != string::npos)
			lpicParam = icalparameter_new_partstat(ICAL_PARTSTAT_DECLINED);
		else if (strMessageClass.rfind("Tent") != string::npos)
			lpicParam = icalparameter_new_partstat(ICAL_PARTSTAT_TENTATIVE);
		else
			// shouldn't happen, but better than having no lpicParam pointer
			lpicParam = icalparameter_new_partstat(ICAL_PARTSTAT_ACCEPTED);

		// I am the only attendee that is replying
		auto wstrBuf = L"mailto:" + (strRepsSenderEmailAddr.empty() ? strSenderEmailAddr : strRepsSenderEmailAddr);
		lpicProp = icalproperty_new_attendee(m_converter.convert_to<string>(wstrBuf).c_str());
		icalproperty_add_parameter(lpicProp, lpicParam);

		wstrBuf = strRepsSenderName.empty() ? strSenderName: strRepsSenderName;
		if (!wstrBuf.empty())
			icalproperty_add_parameter(lpicProp, icalparameter_new_cn(m_converter.convert_to<string>(m_strCharset.c_str(), wstrBuf, rawsize(wstrBuf), CHARSET_WCHAR).c_str()));
		
		wstrBuf = L"mailto:" + strSenderEmailAddr;
		if (!strSenderEmailAddr.empty() && strSenderEmailAddr != strRepsSenderEmailAddr)
			icalproperty_add_parameter(lpicProp, icalparameter_new_sentby(m_converter.convert_to<string>(wstrBuf).c_str()));

		icalcomponent_add_property(lpicEvent, lpicProp);

		// Organizer should be the only MAPI_TO entry
		hr = lpMessage->GetRecipientTable(MAPI_UNICODE, &~lpTable);
		if (hr != hrSuccess)
			return hr;

		rowset_ptr lpRows;
		hr = lpTable->QueryRows(-1, 0, &~lpRows);
		if (hr != hrSuccess)
			return hr;
		
		// The response should only be sent to the organizer (@todo restrict on MAPI_TO ? ...)
		if (lpRows->cRows != 1)
			return MAPI_E_CALL_FAILED;

		// @todo: use correct index number?
		hr = HrGetAddress(m_lpAdrBook, lpRows->aRow[0].lpProps, lpRows->aRow[0].cValues,
		     PR_ENTRYID, PR_DISPLAY_NAME, PR_ADDRTYPE, PR_EMAIL_ADDRESS,
		     strReceiverName, strReceiverType, strReceiverEmailAddr);
		if (hr != hrSuccess)
			return hr;

		wstrBuf = L"mailto:" + strReceiverEmailAddr;
		lpicProp = icalproperty_new_organizer(m_converter.convert_to<string>(m_strCharset.c_str(), wstrBuf, rawsize(wstrBuf), CHARSET_WCHAR).c_str());

		if (!strReceiverName.empty()) {
			lpicParam = icalparameter_new_cn(m_converter.convert_to<string>(m_strCharset.c_str(), strReceiverName, rawsize(strReceiverName), CHARSET_WCHAR).c_str());
			icalproperty_add_parameter(lpicProp, lpicParam);
		}

		icalcomponent_add_property(lpicEvent, lpicProp);
	}
	else
	{
		// strMessageClass == "IPM.Schedule.Meeting.Request", "IPM.Schedule.Meeting.Canceled" or ....?
		// strMessageClass == "IPM.Appointment": normal calendar item

		// If we're dealing with a meeting, preset status to 1. PROP_MEETINGSTATUS may not be set
		if (strMessageClass.compare(0, string("IPM.Schedule.Meeting").length(), string("IPM.Schedule.Meeting")) == 0)
			ulMeetingStatus = 1;

		// a normal calendar item has meeting status == 0, all other types != 0
		lpPropVal = PCpropFindProp(lpProps, ulProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MEETINGSTATUS], PT_LONG));
		if (lpPropVal)
			ulMeetingStatus = lpPropVal->Value.ul;
		else if (HrGetOneProp(lpParentMsg, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MEETINGSTATUS], PT_LONG), &~lpSpropVal) == hrSuccess)
			// if MeetingStatus flag is not set in exception message, retrive it from parent message.
			ulMeetingStatus = lpSpropVal->Value.ul;

		// meeting bit enabled
		if (ulMeetingStatus & 1) {
			if (ulMeetingStatus & 4) {
				icalcomponent_add_property(lpicEvent, icalproperty_new_status(ICAL_STATUS_CANCELLED));
				icMethod = ICAL_METHOD_CANCEL;
			} else {
				icalcomponent_add_property(lpicEvent, icalproperty_new_status(ICAL_STATUS_CONFIRMED));
				icMethod = ICAL_METHOD_REQUEST;
			}

			// meeting action, add all attendees, request reply when needed
			hr = HrSetICalAttendees(lpMessage, strSenderEmailAddr, lpicEvent);
			if (hr != hrSuccess)
				return hr;

			//Set this property to force thunderbird to send invitations mails.
			lpPropVal = PCpropFindProp (lpProps, ulProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MOZSENDINVITE], PT_BOOLEAN));
			if (lpPropVal && !lpPropVal->Value.b) 
				lpicProp = icalproperty_new_x("FALSE");
			else
				lpicProp = icalproperty_new_x("TRUE");
	
			icalproperty_set_x_name(lpicProp, "X-MOZ-SEND-INVITATIONS"); 
			icalcomponent_add_property(lpicEvent, lpicProp);
			
			// I am the Organizer
			auto wstrBuf = L"mailto:" + (strRepsSenderEmailAddr.empty()? strSenderEmailAddr : strRepsSenderEmailAddr);
			lpicProp = icalproperty_new_organizer(m_converter.convert_to<string>(m_strCharset.c_str(), wstrBuf, rawsize(wstrBuf), CHARSET_WCHAR).c_str());

			wstrBuf = strRepsSenderName.empty()? strSenderName : strRepsSenderName;
			if (!wstrBuf.empty())
				icalproperty_add_parameter(lpicProp, icalparameter_new_cn(m_converter.convert_to<string>(m_strCharset.c_str(), wstrBuf, rawsize(wstrBuf), CHARSET_WCHAR).c_str()) );

			wstrBuf = L"mailto:" + strSenderEmailAddr;
			if (!strSenderEmailAddr.empty() && strSenderEmailAddr != strRepsSenderEmailAddr)
				icalproperty_add_parameter(lpicProp, icalparameter_new_sentby(m_converter.convert_to<string>(m_strCharset.c_str(), wstrBuf, rawsize(wstrBuf), CHARSET_WCHAR).c_str()) );

			icalcomponent_add_property(lpicEvent, lpicProp);
		} else {
			// normal calendar item
			icMethod = ICAL_METHOD_PUBLISH;
		}
	}

	*lpicMethod = icMethod;
	return hrSuccess;
}

/** 
 * Sets default time properties in the ical event. The following ical
 * properties are added:
 * - CREATED
 * - LAST-MODIFIED
 * - DTSTAMP
 * 
 * @param[in]  lpMsgProps All (required) properties of the message to convert to ical
 * @param[in]  ulMsgProps Number of properties in lpMsgProps
 * @param[in]  lpicTZinfo ical timezone object to set times in, (unused in this version, all times set here are always UTC)
 * @param[in]  strTZid name of the given ical timezone, (unused in this version, all times set here are always UTC)
 * @param[in,out] lpEvent The ical event to modify
 * 
 * @return MAPI error code
 */
HRESULT VConverter::HrSetTimeProperties(LPSPropValue lpMsgProps, ULONG ulMsgProps, icaltimezone *lpicTZinfo, const std::string &strTZid, icalcomponent *lpEvent)
{
	bool bHasOwnerCriticalChange = false;

	// Set creation time / CREATED
	auto lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, PR_CREATION_TIME);
	if (lpPropVal) {
		auto ittICalTime = icaltime_from_timet_with_zone(FileTimeToUnixTime(lpPropVal->Value.ft.dwHighDateTime, lpPropVal->Value.ft.dwLowDateTime), 0, nullptr);
		kc_ical_utc(ittICalTime, true);
		auto lpProp = icalproperty_new_created(ittICalTime);
		icalcomponent_add_property(lpEvent, lpProp);
	}

	// exchange 2003 is using DTSTAMP for 'X-MICROSOFT-CDO-OWNER-CRITICAL-CHANGE'
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_OWNERCRITICALCHANGE], PT_SYSTIME));
	if (lpPropVal) {
		auto ittICalTime = icaltime_from_timet_with_zone(FileTimeToUnixTime(lpPropVal->Value.ft.dwHighDateTime, lpPropVal->Value.ft.dwLowDateTime), false, icaltimezone_get_utc_timezone());
		auto lpProp = icalproperty_new_dtstamp(ittICalTime);
		icalcomponent_add_property(lpEvent,lpProp);

		bHasOwnerCriticalChange = true;
	}

	// Set modification time / LAST-MODIFIED + DTSTAMP
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, PR_LAST_MODIFICATION_TIME);
	if (lpPropVal) {
		auto ittICalTime = icaltime_from_timet_with_zone(FileTimeToUnixTime(lpPropVal->Value.ft.dwHighDateTime, lpPropVal->Value.ft.dwLowDateTime), 0, nullptr);
		kc_ical_utc(ittICalTime, true);
		auto lpProp = icalproperty_new_lastmodified(ittICalTime);
		icalcomponent_add_property(lpEvent,lpProp);

		if (!bHasOwnerCriticalChange) {
			lpProp = icalproperty_new_dtstamp(ittICalTime);
			icalcomponent_add_property(lpEvent,lpProp);
		}
	}
	return hrSuccess;
}

/** 
 * Helper function for HrSetOrganizerAndAttendees to add recipients
 * from lpMessage to the ical object.
 * 
 * @param[in]  lpMessage The message to process the RecipientsTable of
 * @param[in]  strOrganizer The email address of the organizer, which is excluded as attendee
 * @param[in,out] lpicEvent The event to modify
 * 
 * @return MAPI error code.
 */
HRESULT VConverter::HrSetICalAttendees(LPMESSAGE lpMessage, const std::wstring &strOrganizer, icalcomponent *lpicEvent)
{
	icalparameter *lpParam = NULL;
	object_ptr<IMAPITable> lpTable;
	rowset_ptr lpRows;
	wstring strName, strType, strEmailAddress;
	static constexpr const SizedSPropTagArray(7, sptaRecipProps) =
		{7, {PR_ENTRYID, PR_DISPLAY_NAME_W, PR_ADDRTYPE_A,
		PR_EMAIL_ADDRESS_A, PR_RECIPIENT_FLAGS, PR_RECIPIENT_TYPE,
		PR_RECIPIENT_TRACKSTATUS}};

	auto hr = lpMessage->GetRecipientTable(0, &~lpTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->SetColumns(sptaRecipProps, 0);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->QueryRows(-1, 0, &~lpRows);
	if (hr != hrSuccess)
		return hr;
	
	// Set all recipients into icalcomponent lpicEvent
	for (ULONG ulCount = 0; ulCount < lpRows->cRows; ++ulCount) {
		// ZARAFA types go correct because of addressbook, (slow?, should use PR_SMTP_ADDRESS?)
		// SMTP types go correct because of PR_EMAIL_ADDRESS 
		hr = HrGetAddress(m_lpAdrBook, lpRows->aRow[ulCount].lpProps, lpRows->aRow[ulCount].cValues,
						  PR_ENTRYID, PR_DISPLAY_NAME_W, PR_ADDRTYPE_A, PR_EMAIL_ADDRESS_A,
						  strName, strType, strEmailAddress);

		// skip the organiser if present in the recipient table.
		if (hr != hrSuccess || strEmailAddress == strOrganizer)
			continue;

		// flags set to 3 is organizer, so skip that entry
		auto lpPropVal = lpRows->aRow[ulCount].cfind(PR_RECIPIENT_FLAGS);
		if (lpPropVal != NULL && lpPropVal->Value.ul == 3)
			continue;
		lpPropVal = lpRows->aRow[ulCount].cfind(PR_RECIPIENT_TYPE);
		if (lpPropVal == NULL)
			continue;

		switch (lpPropVal->Value.ul) {
		case MAPI_TO:
			lpParam = icalparameter_new_role(ICAL_ROLE_REQPARTICIPANT);
			break;
		case MAPI_CC:
			lpParam = icalparameter_new_role(ICAL_ROLE_OPTPARTICIPANT);
			break;
		case MAPI_BCC:
			lpParam = icalparameter_new_role(ICAL_ROLE_NONPARTICIPANT);
			break;
		default:
			continue;
		}

		strEmailAddress.insert(0, L"mailto:");
		auto lpProp = icalproperty_new_attendee(m_converter.convert_to<string>(m_strCharset.c_str(), strEmailAddress, rawsize(strEmailAddress), CHARSET_WCHAR).c_str());
		icalproperty_add_parameter(lpProp, lpParam);
		lpPropVal = lpRows->aRow[ulCount].cfind(PR_RECIPIENT_TRACKSTATUS);
		if (lpPropVal != NULL) {
			if (lpPropVal->Value.ul == 2)
				icalproperty_add_parameter(lpProp, icalparameter_new_partstat(ICAL_PARTSTAT_TENTATIVE));
			else if (lpPropVal->Value.ul == 3)
				icalproperty_add_parameter(lpProp, icalparameter_new_partstat(ICAL_PARTSTAT_ACCEPTED));
			else if (lpPropVal->Value.ul == 4)
				icalproperty_add_parameter(lpProp, icalparameter_new_partstat(ICAL_PARTSTAT_DECLINED));					
			else {
				icalproperty_add_parameter(lpProp, icalparameter_new_partstat(ICAL_PARTSTAT_NEEDSACTION));
				icalproperty_add_parameter(lpProp, icalparameter_new_rsvp(ICAL_RSVP_TRUE));
			}
		} else {
			// make sure clients are requested to send a reply on meeting requests
			icalproperty_add_parameter(lpProp, icalparameter_new_partstat(ICAL_PARTSTAT_NEEDSACTION));
			icalproperty_add_parameter(lpProp, icalparameter_new_rsvp(ICAL_RSVP_TRUE));
		}

		if (!strName.empty())
			icalproperty_add_parameter(lpProp, icalparameter_new_cn(m_converter.convert_to<string>(m_strCharset.c_str(), strName, rawsize(strName), CHARSET_WCHAR).c_str()));

		icalcomponent_add_property(lpicEvent, lpProp);
	}
	return hr;
}

/** 
 * Sets the busy status in the ical event. The following properties
 * are added:
 * - TRANSP
 * - X-MICROSOFT-CDO-INTENDEDSTATUS
 * 
 * @param[in] lpMessage The MAPI message to get the busy status from for the X property
 * @param[in] ulBusyStatus The normal busy status to set in the TRANSP property
 * @param[in,out] lpicEvent The ical event to modify
 * 
 * @return Always return hrSuccess
 */
HRESULT VConverter::HrSetBusyStatus(LPMESSAGE lpMessage, ULONG ulBusyStatus, icalcomponent *lpicEvent)
{
	memory_ptr<SPropValue> lpSpropVal;
	icalproperty *lpicProp = NULL;
	
	// set the TRANSP property
	if (ulBusyStatus == 0)
		lpicProp = icalproperty_new_transp(ICAL_TRANSP_TRANSPARENT);
	else
		lpicProp = icalproperty_new_transp(ICAL_TRANSP_OPAQUE);
	icalcomponent_add_property(lpicEvent, lpicProp);
	
	// set the X-MICROSOFT-CDO-INTENDEDSTATUS property
	auto hr = HrGetOneProp(lpMessage, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_INTENDEDBUSYSTATUS], PT_LONG), &~lpSpropVal);
	if(hr == hrSuccess && lpSpropVal->Value.ul != (ULONG)-1)
		ulBusyStatus = lpSpropVal->Value.ul;

	switch (ulBusyStatus) {
	case 0:
		lpicProp = icalproperty_new_x("FREE");
		break;
	case 1:
		lpicProp = icalproperty_new_x("TENTATIVE");
		break;
	default:
	case 2:
		lpicProp = icalproperty_new_x("BUSY");
		break;
	case 3:
		lpicProp = icalproperty_new_x("OOF");
		break;
	}
	
	icalproperty_set_x_name(lpicProp, "X-MICROSOFT-CDO-INTENDEDSTATUS"); 
	icalcomponent_add_property(lpicEvent, lpicProp);
	return hrSuccess;
}

/** 
 * Add extra Microsoft and Mozilla X headers. These are required for
 * client interchange and compatibility. The following ical properties
 * are added:
 * - X-MICROSOFT-CDO-OWNER-CRITICAL-CHANGE
 * - X-MICROSOFT-CDO-ATTENDEE-CRITICAL-CHANGE
 * - X-MICROSOFT-CDO-APPT-SEQUENCE
 * - X-MICROSOFT-CDO-OWNERAPPTID
 * - X-MOZ-GENERATION
 * - X-MICROSOFT-CDO-ALLDAYEVENT
 * 
 * @param[in] ulMsgProps Number of properties in lpMsgProps
 * @param[in] lpMsgProps Properties used for the conversion
 * @param[in]  lpMessage The message to convert the PR_RTF_COMPRESSED from
 * @param[in,out] lpEvent ical item to be modified
 * 
 * @return Always return hrSuccess
 */
HRESULT VConverter::HrSetXHeaders(ULONG ulMsgProps, LPSPropValue lpMsgProps, LPMESSAGE lpMessage, icalcomponent *lpEvent)
{
	time_t ttCriticalChange = 0;

	// set X-MICROSOFT-CDO & X-MOZ properties 
	// X-MICROSOFT-CDO-OWNER-CRITICAL-CHANGE
	auto lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_OWNERCRITICALCHANGE], PT_SYSTIME));
	if (lpPropVal != nullptr)
		FileTimeToUnixTime(lpPropVal->Value.ft, &ttCriticalChange);
	else
		ttCriticalChange = time(NULL);

	auto icCriticalChange = icaltime_from_timet_with_zone(ttCriticalChange, false, icaltimezone_get_utc_timezone());
	auto lpicValue = icalvalue_new_datetime(icCriticalChange);
	auto lpszTemp = icalvalue_as_ical_string_r(lpicValue);
	auto lpProp = icalproperty_new_x(lpszTemp);
	icalmemory_free_buffer(lpszTemp);
	icalproperty_set_x_name(lpProp, "X-MICROSOFT-CDO-OWNER-CRITICAL-CHANGE");
	icalcomponent_add_property(lpEvent, lpProp);
	icalvalue_free(lpicValue);

	// X-MICROSOFT-CDO-ATTENDEE-CRITICAL-CHANGE
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_ATTENDEECRITICALCHANGE], PT_SYSTIME));
	if (lpPropVal != nullptr)
		FileTimeToUnixTime(lpPropVal->Value.ft, &ttCriticalChange);
	else
		ttCriticalChange = time(NULL);

	icCriticalChange = icaltime_from_timet_with_zone(ttCriticalChange, false, icaltimezone_get_utc_timezone());
	lpicValue = icalvalue_new_datetime(icCriticalChange);
	lpszTemp = icalvalue_as_ical_string_r(lpicValue);
	lpProp = icalproperty_new_x(lpszTemp);
	icalmemory_free_buffer(lpszTemp);
	
	icalproperty_set_x_name(lpProp, "X-MICROSOFT-CDO-ATTENDEE-CRITICAL-CHANGE");
	icalcomponent_add_property(lpEvent, lpProp);
	icalvalue_free(lpicValue);
	
	// X-MICROSOFT-CDO-APPT-SEQUENCE
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_APPTSEQNR], PT_LONG));
	ULONG ulApptSeqNo = 0;
	if (lpPropVal != nullptr)
		ulApptSeqNo = lpPropVal->Value.ul;

	lpicValue = icalvalue_new_integer(ulApptSeqNo);
	lpszTemp = icalvalue_as_ical_string_r(lpicValue);
	lpProp = icalproperty_new_x(lpszTemp);
	icalmemory_free_buffer(lpszTemp);
	icalproperty_set_x_name(lpProp, "X-MICROSOFT-CDO-APPT-SEQUENCE");
	icalcomponent_add_property(lpEvent, lpProp);
	icalvalue_free(lpicValue);

	// X-MICROSOFT-CDO-OWNERAPPTID
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, PR_OWNER_APPT_ID);
	ULONG ulOwnerApptID = -1;
	if (lpPropVal != nullptr)
		ulOwnerApptID = lpPropVal->Value.ul;

	lpicValue = icalvalue_new_integer(ulOwnerApptID);
	lpszTemp = icalvalue_as_ical_string_r(lpicValue);
	lpProp = icalproperty_new_x(lpszTemp);
	icalmemory_free_buffer(lpszTemp);
	icalproperty_set_x_name(lpProp, "X-MICROSOFT-CDO-OWNERAPPTID");
	icalcomponent_add_property(lpEvent, lpProp);
	icalvalue_free(lpicValue);

	// X-MOZ-GENERATION
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps,  CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MOZGEN], PT_LONG));
	if (lpPropVal)
	{
		LONG ulXmozGen = 0;
		icalvalue *lpicValue = NULL;
		ulXmozGen = lpPropVal->Value.ul;
		lpicValue = icalvalue_new_integer(ulXmozGen);

		lpszTemp = icalvalue_as_ical_string_r(lpicValue);
		lpProp = icalproperty_new_x(lpszTemp);
		icalmemory_free_buffer(lpszTemp);
		icalproperty_set_x_name(lpProp, "X-MOZ-GENERATION");
		icalcomponent_add_property(lpEvent, lpProp);
		icalvalue_free(lpicValue);
	}

	// X-MICROSOFT-CDO-ALLDAYEVENT
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps,  CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_ALLDAYEVENT], PT_BOOLEAN));
	bool blIsAllday = false;
	if (lpPropVal != nullptr)
		blIsAllday = (lpPropVal->Value.b == TRUE);

	lpicValue = icalvalue_new_x(blIsAllday ? "TRUE" : "FALSE");
	lpszTemp = icalvalue_as_ical_string_r(lpicValue);
	lpProp = icalproperty_new_x(lpszTemp);
	icalmemory_free_buffer(lpszTemp);
	icalproperty_set_x_name(lpProp, "X-MICROSOFT-CDO-ALLDAYEVENT");
	icalcomponent_add_property(lpEvent, lpProp);
	icalvalue_free(lpicValue);

	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, PR_RTF_COMPRESSED);
	if (lpPropVal == nullptr || Util::GetBestBody(lpMsgProps, ulMsgProps, fMapiUnicode) != PR_RTF_COMPRESSED)
		return hrSuccess;

	string rtf;
	object_ptr<IStream> lpStream;

	if (lpMessage->OpenProperty(PR_RTF_COMPRESSED, &IID_IStream, 0, MAPI_DEFERRED_ERRORS, &~lpStream) != hrSuccess)
		return hrSuccess;
	if (Util::HrStreamToString(lpStream, rtf) != hrSuccess)
		return hrSuccess;

	string rtfbase64;
	rtfbase64 = base64_encode(rtf.c_str(), rtf.size());
	lpicValue = icalvalue_new_x(rtfbase64.c_str());
	lpszTemp = icalvalue_as_ical_string_r(lpicValue);
	lpProp = icalproperty_new_x(lpszTemp);
	icalmemory_free_buffer(lpszTemp);
	icalproperty_set_x_name(lpProp, "X-MICROSOFT-RTF");
	icalcomponent_add_property(lpEvent, lpProp);
	icalvalue_free(lpicValue);
	return hrSuccess;
}

/** 
 * Possebly adds a VAlarm (reminder) in the given event.
 * 
 * @note it is not described in the RFC how clients keep track (and
 * thus disable) alarms, so we might be adding a VAlarm for an item
 * where the client already disabled it.

 * @param[in] ulProps The number of properties in lpProps
 * @param[in] lpProps Properties of the message containing the reminder properties
 * @param[in,out] lpicEvent The ical event to modify
 * 
 * @return MAPI error code
 */
HRESULT VConverter::HrSetVAlarm(ULONG ulProps, LPSPropValue lpProps, icalcomponent *lpicEvent)
{
	icalcomponent *lpAlarm = NULL;
	time_t ttSnooze = 0;
	time_t ttSnoozeSuffix = 0;
	bool blxmozgen = false;
	bool blisItemReccr = false;
	LONG lRemindBefore = 0;
	time_t ttReminderTime = 0;
	bool bTask = false;
	
	// find bool, skip if error or false
	auto lpPropVal = PCpropFindProp(lpProps, ulProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERSET], PT_BOOLEAN));
	if (!lpPropVal || lpPropVal->Value.b == FALSE)
		return hrSuccess;
	lpPropVal = PCpropFindProp(lpProps, ulProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERMINUTESBEFORESTART], PT_LONG));
	if (lpPropVal)
		lRemindBefore = lpPropVal->Value.l;
	lpPropVal = PCpropFindProp(lpProps, ulProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERTIME], PT_SYSTIME));
	if (lpPropVal)
		FileTimeToUnixTime(lpPropVal->Value.ft, &ttReminderTime);
	lpPropVal = PCpropFindProp(lpProps, ulProps, PR_MESSAGE_CLASS);
	if (lpPropVal && _tcsicmp(lpPropVal->Value.LPSZ, KC_T("IPM.Task")) == 0)
		bTask = true;
	auto hr = HrParseReminder(lRemindBefore, ttReminderTime, bTask, &lpAlarm);
	if (hr != hrSuccess)
		return hr;

	icalcomponent_add_component(lpicEvent, lpAlarm);
	lpPropVal = PCpropFindProp(lpProps, ulProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRING], PT_BOOLEAN));
	if(lpPropVal && lpPropVal->Value.b == TRUE)
		blisItemReccr = true;

	// retrieve the suffix time for property X-MOZ-SNOOZE-TIME
	lpPropVal = PCpropFindProp(lpProps, ulProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MOZ_SNOOZE_SUFFIX], PT_SYSTIME));
	if(lpPropVal)
		FileTimeToUnixTime(lpPropVal->Value.ft, &ttSnoozeSuffix);	
	// check latest snooze time
	lpPropVal = PCpropFindProp(lpProps, ulProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERNEXTTIME], PT_SYSTIME));
	if (lpPropVal) {
		icaltimetype icSnooze;
		icalvalue *lpicValue = NULL;
		std::string strSnoozeTime = "X-MOZ-SNOOZE-TIME";

		// set timestamp in name for recurring items i.e. X-MOZ-SNOOZE-TIME-1231250400000000:20090107T132846Z
		if (ttSnoozeSuffix != 0 && blisItemReccr)
			strSnoozeTime += "-" + stringify(ttSnoozeSuffix) + "000000";

		FileTimeToUnixTime(lpPropVal->Value.ft, &ttSnooze);
		icSnooze = icaltime_from_timet_with_zone(ttSnooze, false, icaltimezone_get_utc_timezone());
		lpicValue = icalvalue_new_datetime(icSnooze);
		auto lpszTemp = icalvalue_as_ical_string_r(lpicValue);
		auto lpicProp = icalproperty_new_x(lpszTemp);
		icalmemory_free_buffer(lpszTemp);
		icalproperty_set_x_name(lpicProp, strSnoozeTime.c_str()); 

		icalcomponent_add_property(lpicEvent, lpicProp);
		icalvalue_free(lpicValue);
	}

	lpPropVal = PCpropFindProp(lpProps, ulProps,  CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MOZGEN], PT_LONG));
	if (lpPropVal)
		blxmozgen = true;

	// send X-MOZ-LASTACK
	lpPropVal = PCpropFindProp(lpProps, ulProps,  CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MOZLASTACK], PT_SYSTIME));
	if (lpPropVal)
	{
		time_t ttLastAckTime = 0;
		icaltimetype icModTime;
		icalvalue *lpicValue = NULL;
		
		FileTimeToUnixTime(lpPropVal->Value.ft, &ttLastAckTime);
		//do not send X-MOZ-LASTACK if reminder older than last ack time
		if(ttLastAckTime > ttSnooze && !blxmozgen)
			return hrSuccess;
		icModTime = icaltime_from_timet_with_zone(ttLastAckTime, false, icaltimezone_get_utc_timezone());
		lpicValue = icalvalue_new_datetime(icModTime);
		auto lpszTemp = icalvalue_as_ical_string_r(lpicValue);
		auto lpicProp = icalproperty_new_x(lpszTemp);
		icalmemory_free_buffer(lpszTemp);
		icalproperty_set_x_name(lpicProp, "X-MOZ-LASTACK"); 
		icalcomponent_add_property(lpicEvent, lpicProp);
		icalvalue_free(lpicValue);
	}
	return hrSuccess;
}

/** 
 * Converts the plain text body of the MAPI message to an ical
 * property (DESCRIPTION).
 * 
 * We add some extra fixes on the body, so all ical clients can parse
 * the body correctly.
 *
 * @param[in]  lpMessage The message to convert the PR_BODY from
 * @param[out] lppicProp The ical property containing the description, when hrSuccess is returned.
 * 
 * @return MAPI error code
 */
HRESULT VConverter::HrSetBody(LPMESSAGE lpMessage, icalproperty **lppicProp)
{
	object_ptr<IStream> lpStream;
	STATSTG sStreamStat;
	std::wstring strBody;

	auto hr = lpMessage->OpenProperty(PR_BODY_W, &IID_IStream, 0, MAPI_DEFERRED_ERRORS, &~lpStream);
	if (hr != hrSuccess)
		return hr;
	hr = lpStream->Stat(&sStreamStat, 0);
	if (hr != hrSuccess)
		return hr;
	if (sStreamStat.cbSize.LowPart == 0)
		return MAPI_E_NOT_FOUND;
	std::unique_ptr<wchar_t[]> lpBody(new WCHAR[sStreamStat.cbSize.LowPart + sizeof(WCHAR)]);
	memset(lpBody.get(), 0, (sStreamStat.cbSize.LowPart+1) * sizeof(WCHAR));

	hr = lpStream->Read(lpBody.get(), sStreamStat.cbSize.LowPart * sizeof(WCHAR), NULL);
	if (hr != hrSuccess)
		return hr;

	// The body is converted as OL2003 does not parse '\r' & '\t' correctly
	// Newer versions also have some issues parsing these chars
	// RFC specifies that new lines should be CRLF
	StringTabtoSpaces(lpBody.get(), &strBody);
	StringCRLFtoLF(strBody, &strBody);
	
	*lppicProp = icalproperty_new_description(m_converter.convert_to<string>(m_strCharset.c_str(), strBody, rawsize(strBody), CHARSET_WCHAR).c_str());
	return hrSuccess;
}

/** 
 * No specific properties on the base level. Override this function if
 * required (currently VTODO only).
 * 
 * @param[in]  ulProps Number of properties in lpProps
 * @param[in]  lpProps Properties of the message to convert
 * @param[in,out] lpicEvent The ical object to modify
 * 
 * @return Always return hrSuccess
 */
HRESULT VConverter::HrSetItemSpecifics(ULONG ulProps, LPSPropValue lpProps, icalcomponent *lpicEvent)
{
	return hrSuccess;
}

/** 
 * Adds the recurrence-id property to the event if we're handling an exception.
 * 
 * @param[in] lpMsgProps Contains all the (required) lpMessage properties for conversion
 * @param[in] ulMsgProps Number of properties in lpMsgProps
 * @param[in] lpicTZinfo Ical Timezone to set all time related properties in
 * @param[in] strTZid Name of the timezone
 * @param[in,out] lpEvent The ical event to modify
 * 
 * @return MAPI error code
 */
HRESULT VConverter::HrSetRecurrenceID(LPSPropValue lpMsgProps, ULONG ulMsgProps, icaltimezone *lpicTZinfo, const std::string &strTZid, icalcomponent *lpEvent)
{
	bool bIsSeriesAllDay = false;
	icaltimetype icTime = {0};
	time_t tRecId = 0;
	ULONG ulRecurStartTime = -1;	// as 0 states start of day
	ULONG ulRecurEndTime = -1;		// as 0 states start of day	

	// We cannot check if PROP_ISEXCEPTION is set to TRUE, since Outlook sends accept messages on excetions with that property set to false.
	auto lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_ISEXCEPTION], PT_BOOLEAN));
	if (!lpPropVal || lpPropVal->Value.b == FALSE) {
		auto lpPropGlobal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY));
		if (!lpPropGlobal)
			return hrSuccess;
		auto lpPropClean = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_CLEANID], PT_BINARY));
		if (!lpPropClean)
			return hrSuccess;
		if (lpPropClean->Value.bin.cb != lpPropGlobal->Value.bin.cb)
			return hrSuccess;
		if (memcmp(lpPropClean->Value.bin.lpb, lpPropGlobal->Value.bin.lpb, lpPropGlobal->Value.bin.cb) == 0)
			return hrSuccess;

		// different timestamp in dispidGlobalObjectID, export RECURRENCE-ID
	}

	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURSTARTTIME], PT_LONG));
	if (lpPropVal)
		ulRecurStartTime = lpPropVal->Value.ul;
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURENDTIME], PT_LONG));
	if (lpPropVal)
		ulRecurEndTime = lpPropVal->Value.ul;

	// check to know if series is all day or not.
	// ulRecurStartTime = 0 -> recurrence series starts at 00:00 AM 
	// ulRecurEndTime = 24 * 4096 -> recurrence series ends at 12:00 PM 
	// 60 sec -> highest pow of 2 after 60 -> 64 
	// 60 mins -> 60 * 64 = 3840 -> highest pow of 2 after 3840 -> 4096
	if (ulRecurStartTime == 0 && ulRecurEndTime == (24 * 4096))
		bIsSeriesAllDay = true;

	// set Recurrence-ID for exception msg if dispidRecurringbase prop is present	
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRINGBASE], PT_SYSTIME));
	if (!lpPropVal) {
		// This code happens when we're sending acceptance mail for an exception.

		// if RecurringBase prop is not present then retrieve date from GlobalObjId from 16-19th bytes
		// combine this date with time from dispidStartRecurrenceTime and set it as RECURRENCE-ID
		lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY));
		if (!lpPropVal)
			return hrSuccess;
		// @todo don't do this calculation using a std::string
		auto strUid = bin2hex(lpPropVal->Value.bin);
		if(!IsOutlookUid(strUid))
			return hrSuccess;
		if(strUid.substr(32, 8).compare("00000000") == 0 && ulRecurStartTime == (ULONG)-1)
			return hrSuccess;

		icTime.year = strtol(strUid.substr(32, 4).c_str(), NULL, 16);
		icTime.month = strtol(strUid.substr(36, 2).c_str(), NULL, 16);
		icTime.day = strtol(strUid.substr(38, 2).c_str(), NULL, 16);
		// Although the timestamp is probably 0, it does not seem to matter.
		icTime.hour = ulRecurStartTime / 4096;
		ulRecurStartTime -= icTime.hour * 4096;
		icTime.minute = ulRecurStartTime / 64;
		icTime.second = ulRecurStartTime - icTime.minute * 64;
		
		// set correct date for all_day
		if(bIsSeriesAllDay)
			tRecId = icaltime_as_timet(icTime);
		else 
			tRecId = icaltime_as_timet_with_zone(icTime,lpicTZinfo);
	} else {
		tRecId = FileTimeToUnixTime(lpPropVal->Value.ft.dwHighDateTime, lpPropVal->Value.ft.dwLowDateTime);	
	}
	
	return HrSetTimeProperty(tRecId, bIsSeriesAllDay, lpicTZinfo, strTZid,
	       ICAL_RECURRENCEID_PROPERTY, lpEvent);
}

/**
 * Sets the RRULE and add exceptions to ical data from the mapi message.
 *
 * @param[in]		lpMessage	The source mapi message to be converted to ical data.
 * @param[in,out]	lpicEvent	The icalcomponent to which RRULE is added.
 * @param[in]		lpicTZinfo	The icaltimezone pointer	
 * @param[in]		strTZid		The timezone string ID.
 * @param[out]		lpEventList The list of icalcomponent containing exceptions.
 *
 * @return			MAPI error code
 *
 * @retval			MAPI_E_NOT_FOUND			Recurrencestate blob has errors
 * @retval			MAPI_E_INVALID_PARAMETER	One of the parameters for creating RRULE from recurrence blob is invalid
 */
HRESULT VConverter::HrSetRecurrence(LPMESSAGE lpMessage, icalcomponent *lpicEvent, icaltimezone *lpicTZinfo, const std::string &strTZid, std::list<icalcomponent*> *lpEventList)
{
	ULONG ulRecurrenceStateTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRENCESTATE], PT_BINARY);
	bool bIsAllDay = false;
	memory_ptr<SPropValue> lpSpropArray;
	LPSPropValue lpSPropRecVal = NULL;
	recurrence cRecurrence;
	object_ptr<IStream> lpStream;
	STATSTG sStreamStat;
	ICalRecurrence cICalRecurrence;
	icalcomponent *lpicComp = NULL;
	ULONG ulModCount = 0;
	ULONG ulModifications = 0;
	ULONG cbsize = 0;
	ULONG ulFlag = 0;
	std::list<icalcomponent*> lstExceptions;
	TIMEZONE_STRUCT zone;
	SizedSPropTagArray(6, proptags) =
		{6, {PR_MESSAGE_CLASS_A,
		CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRENCEPATTERN], PT_UNICODE),
		CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRENCESTATE], PT_BINARY),
		CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_ALLDAYEVENT], PT_BOOLEAN),
		CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_STATUS], PT_LONG),
		CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_RECURRSTATE], PT_BINARY)}};

	auto hr = lpMessage->GetProps(proptags, 0, &cbsize, &~lpSpropArray);
	if (FAILED(hr))
		return hr;
	
	if ((PROP_TYPE(lpSpropArray[0].ulPropTag) != PT_ERROR)
		&& (strcasecmp(lpSpropArray[0].Value.lpszA, "IPM.Task") == 0)) {
		ulRecurrenceStateTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_RECURRSTATE], PT_BINARY);
		lpSPropRecVal = &lpSpropArray[5];
		ulFlag = RECURRENCE_STATE_TASKS;

	} else {
		ulRecurrenceStateTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRENCESTATE], PT_BINARY);
		lpSPropRecVal = &lpSpropArray[2];
		ulFlag = RECURRENCE_STATE_CALENDAR;
	}

	// there are no completed recurring task in OL, so return
	if ((PROP_TYPE(lpSpropArray[4].ulPropTag) != PT_ERROR) && lpSpropArray[4].Value.ul == 2)
		return hr;

	if (PROP_TYPE(lpSpropArray[1].ulPropTag) != PT_ERROR)
	{		
		auto lpicProp = icalproperty_new_x(m_converter.convert_to<string>(m_strCharset.c_str(), lpSpropArray[1].Value.lpszW, rawsize(lpSpropArray[1].Value.lpszW), CHARSET_WCHAR).c_str());
		icalproperty_set_x_name(lpicProp, "X-KOPANO-REC-PATTERN");
		icalcomponent_add_property(lpicEvent, lpicProp);
	}

	if ((PROP_TYPE(lpSPropRecVal->ulPropTag) != PT_ERROR)) {
		hr = cRecurrence.HrLoadRecurrenceState(reinterpret_cast<const char *>(lpSPropRecVal->Value.bin.lpb), lpSPropRecVal->Value.bin.cb, ulFlag);
	} else if (lpSPropRecVal->Value.err == MAPI_E_NOT_ENOUGH_MEMORY) {
		// open property and read full blob
		hr = lpMessage->OpenProperty(ulRecurrenceStateTag, &IID_IStream, 0, MAPI_DEFERRED_ERRORS, &~lpStream);
		if (hr != hrSuccess)
			return hr;
		hr = lpStream->Stat(&sStreamStat, 0);
		if (hr != hrSuccess)
			return hr;
		std::unique_ptr<char[]> lpRecurrenceData(new char[sStreamStat.cbSize.LowPart]);
		hr = lpStream->Read(lpRecurrenceData.get(), sStreamStat.cbSize.LowPart, NULL);
		if (hr != hrSuccess)
			return hr;
		hr = cRecurrence.HrLoadRecurrenceState(lpRecurrenceData.get(), sStreamStat.cbSize.LowPart, ulFlag);
	} else {
		// When exception is created in MR, the IsRecurring is set - true by OL
		// but Recurring state is not set in MR.
		return hrSuccess;
	}

	if (FAILED(hr))
		return hr;
	if (PROP_TYPE(lpSpropArray[3].ulPropTag) != PT_ERROR)
		bIsAllDay = (lpSpropArray[3].Value.b == TRUE);

	if (m_iCurrentTimeZone == m_mapTimeZones->end()) {
		hr = HrGetTzStruct("Etc/UTC", &zone);
		if (hr != hrSuccess)
			return hr;
	} else {
		zone = m_iCurrentTimeZone->second;
	}

	// now that we have the recurrence state class, we can create rrules in lpicEvent
	hr = cICalRecurrence.HrCreateICalRecurrence(zone, bIsAllDay, &cRecurrence, lpicEvent);
	if (hr != hrSuccess)
		return hr;

	// all modifications create new event item:
	// RECURRENCE-ID: contains local timezone timestamp of item that is changed
	// other: CREATED, LAST-MODIFIED, DTSTAMP, UID (copy from original)
	// and then exception properties are replaced
	ulModCount = cRecurrence.getModifiedCount();
	for (ULONG i = 0; i < ulModCount; ++i) {
		
		SPropValuePtr  lpMsgProps;
		ULONG ulMsgProps = 0;
		const SPropValue *lpProp = NULL;
		icalproperty_method icMethod = ICAL_METHOD_NONE;
		icalcomp_ptr lpicException;

		ulModifications = cRecurrence.getModifiedFlags(i);

		auto bIsAllDayException = bIsAllDay;
		hr = cICalRecurrence.HrMakeICalException(lpicEvent, &unique_tie(lpicException));
		if (hr != hrSuccess)
			continue;

		auto tNewTime = cRecurrence.getModifiedStartDateTime(i);
		auto tExceptionStart = tNewTime;
		object_ptr<IMessage> lpException;
		hr = HrGetExceptionMessage(lpMessage, tExceptionStart, &~lpException);
		if (hr != hrSuccess)
		{
			hr = hrSuccess;
			continue;
		}
		hr = lpException->GetProps(NULL, MAPI_UNICODE, &ulMsgProps, &~lpMsgProps);
		if (FAILED(hr))
			continue;
		hr = HrSetOrganizerAndAttendees(lpMessage, lpException,
		     ulMsgProps, lpMsgProps, &icMethod, lpicException.get());
		if (hr != hrSuccess)
			continue;

		if (ulModifications & ARO_SUBTYPE)
		{
			auto lpProp = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_ALLDAYEVENT], PT_BOOLEAN));
			if (lpProp)
				bIsAllDayException = (lpProp->Value.b == TRUE);
			auto lpicProp = icalcomponent_get_first_property(lpicException.get(), ICAL_X_PROPERTY);
			while (lpicProp && (strcmp(icalproperty_get_x_name(lpicProp), "X-MICROSOFT-CDO-ALLDAYEVENT") != 0))
				lpicProp = icalcomponent_get_next_property(lpicException.get(), ICAL_X_PROPERTY);
			if (lpicProp) {
				icalcomponent_remove_property(lpicException.get(), lpicProp);
				icalproperty_free(lpicProp);
			}

			auto lpicValue = icalvalue_new_x(bIsAllDayException ? "TRUE" : "FALSE");
			auto lpszTemp = icalvalue_as_ical_string_r(lpicValue);
			lpicProp = icalproperty_new_x(lpszTemp);
			icalmemory_free_buffer(lpszTemp);
			icalproperty_set_x_name(lpicProp, "X-MICROSOFT-CDO-ALLDAYEVENT"); 
			icalcomponent_add_property(lpicException.get(), lpicProp);
			icalvalue_free(lpicValue);
		}
		
		// 1. get new StartDateTime and EndDateTime from exception and make DTSTART and DTEND in sTimeZone
		tNewTime = LocalToUTC(tNewTime, zone);
		hr = HrSetTimeProperty(tNewTime, bIsAllDayException, lpicTZinfo,
		     strTZid, ICAL_DTSTART_PROPERTY, lpicException.get());
		if (hr != hrSuccess)
			continue;
		tNewTime = LocalToUTC(cRecurrence.getModifiedEndDateTime(i), zone);
		hr = HrSetTimeProperty(tNewTime, bIsAllDayException, lpicTZinfo,
		     strTZid, ICAL_DTEND_PROPERTY, lpicException.get());
		if (hr != hrSuccess)
			continue;
		lpProp = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRINGBASE], PT_SYSTIME));
		if (!lpProp)
			lpProp = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_OLDSTART], PT_SYSTIME));

		if (lpProp) {
			tNewTime = FileTimeToUnixTime(lpProp->Value.ft.dwHighDateTime, lpProp->Value.ft.dwLowDateTime);
			hr = HrSetTimeProperty(tNewTime, bIsAllDay, lpicTZinfo,
			     strTZid, ICAL_RECURRENCEID_PROPERTY, lpicException.get());
			if (hr != hrSuccess)
				continue;
		}

		// 2. for each (useful?) bit in ulOverrideFlags, set property
		if (ulModifications & ARO_SUBJECT) {
			// find the previous value, and remove it
			auto lpicProp = icalcomponent_get_first_property(lpicException.get(), ICAL_SUMMARY_PROPERTY);
			if (lpicProp) {
				icalcomponent_remove_property(lpicException.get(), lpicProp);
				icalproperty_free(lpicProp);
			}

			const wstring wstrTmp = cRecurrence.getModifiedSubject(i);
			icalcomponent_add_property(lpicException.get(), icalproperty_new_summary(m_converter.convert_to<string>(m_strCharset.c_str(), wstrTmp, rawsize(wstrTmp), CHARSET_WCHAR).c_str()));
		}

		if (ulModifications & ARO_MEETINGTYPE)
			;// make this in invite, cancel, ... ?

		if (ulModifications & ARO_REMINDERDELTA && !(ulModifications & ARO_REMINDERSET))
			HrUpdateReminderTime(lpicException.get(), cRecurrence.getModifiedReminderDelta(i));

		if (ulModifications & ARO_REMINDERSET) {
			// Outlook is nasty!
			// If you make an exception reminder enable with the DEFAULT 15 minutes alarm,
			// the value is NOT saved in the exception attachment.
			// That's why I don't open the attachment if the value isn't going to be present anyway.
			// Also, it (the default) is present in the main item ... not very logical.
			LONG lRemindBefore = 0;
			time_t ttReminderTime = 0;
			if (ulModifications & ARO_REMINDERDELTA) {
				lpProp = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERMINUTESBEFORESTART], PT_LONG));
				lRemindBefore = lpProp ? lpProp->Value.l : 15;

				lpProp = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REMINDERTIME], PT_LONG));
				if (lpProp)
					FileTimeToUnixTime(lpProp->Value.ft, &ttReminderTime);
			}

			// add new valarm
			// although a previous valarm should not be here, the webaccess always says it's been changed, so we remove the old one too
			lpicComp = icalcomponent_get_first_component(lpicException.get(), ICAL_VALARM_COMPONENT);
			if (lpicComp) {
				icalcomponent_remove_component(lpicException.get(), lpicComp);
				icalcomponent_free(lpicComp);
			}
			lpicComp = NULL;

			if (cRecurrence.getModifiedReminder(i) != 0) {
				hr = HrParseReminder(lRemindBefore, ttReminderTime, false, &lpicComp);
				if (hr == hrSuccess)
					icalcomponent_add_component(lpicException.get(), lpicComp);
			}
		}

		if (ulModifications & ARO_LOCATION) {
			auto lpicProp = icalcomponent_get_first_property(lpicException.get(), ICAL_LOCATION_PROPERTY);
			if (lpicProp) {
				icalcomponent_remove_property(lpicException.get(), lpicProp);
				icalproperty_free(lpicProp);
			}

			const wstring wstrTmp = cRecurrence.getModifiedLocation(i);
			icalcomponent_add_property(lpicException.get(), icalproperty_new_location(m_converter.convert_to<std::string>(m_strCharset.c_str(), wstrTmp, rawsize(wstrTmp), CHARSET_WCHAR).c_str()));
		}

		if (ulModifications & ARO_BUSYSTATUS) {
			// new X-MICROSOFT-CDO-INTENDEDSTATUS and TRANSP
			auto lpicProp = icalcomponent_get_first_property(lpicException.get(), ICAL_TRANSP_PROPERTY);
			if (lpicProp) {
				icalcomponent_remove_property(lpicException.get(), lpicProp);
				icalproperty_free(lpicProp);
			}

			lpicProp = icalcomponent_get_first_property(lpicException.get(), ICAL_X_PROPERTY);
			while (lpicProp && (strcmp(icalproperty_get_x_name(lpicProp), "X-MICROSOFT-CDO-INTENDEDSTATUS") != 0))
				lpicProp = icalcomponent_get_next_property(lpicException.get(), ICAL_X_PROPERTY);
			if (lpicProp) {
				icalcomponent_remove_property(lpicException.get(), lpicProp);
				icalproperty_free(lpicProp);
			}
			HrSetBusyStatus(lpException, cRecurrence.getModifiedBusyStatus(i), lpicException.get());
		}

		if (ulModifications & ARO_ATTACHMENT)
			;// ..?
		if (ulModifications & ARO_APPTCOLOR)
			;// should never happen, according to the specs

		if (ulModifications & ARO_EXCEPTIONAL_BODY) {
			auto lpicProp = icalcomponent_get_first_property(lpicException.get(), ICAL_DESCRIPTION_PROPERTY);
			if (lpicProp) {
				icalcomponent_remove_property(lpicException.get(), lpicProp);
				icalproperty_free(lpicProp);
			}
			lpicProp = NULL;

			if (HrSetBody(lpException, &lpicProp) == hrSuccess)
				icalcomponent_add_property(lpicException.get(), lpicProp);
		}
		lstExceptions.emplace_back(lpicException.release());
	}	

	*lpEventList = std::move(lstExceptions);
	return hr;
}

/**
 * Update the VALARM component from exception reminder
 *
 * @param[in]	lpicEvent	ical component whose reminder is to be updated 
 * @param[in]	lReminder	new reminder time
 * @return		MAPI error code
 * @retval		MAPI_E_NOT_FOUND	no VALARM component found in ical component 
 */
HRESULT VConverter::HrUpdateReminderTime(icalcomponent *lpicEvent, LONG lReminder)
{
	icaltriggertype sittTrigger;

	auto lpicAlarm = icalcomponent_get_first_component(lpicEvent, ICAL_VALARM_COMPONENT);
	if (lpicAlarm == NULL)
		return MAPI_E_NOT_FOUND;

	memset(&sittTrigger, 0, sizeof(icaltriggertype));
	sittTrigger.duration = icaldurationtype_from_int(-1 * lReminder * 60); // set seconds
	auto lpicProp = icalcomponent_get_first_property(lpicAlarm, ICAL_TRIGGER_PROPERTY);
	if (lpicProp) {
		icalcomponent_remove_property(lpicAlarm, lpicProp);
		icalproperty_free(lpicProp);
	}
	icalcomponent_add_property(lpicAlarm, icalproperty_new_trigger(sittTrigger));
	return hrSuccess;
}

/**
 * Returns the exeception mapi message of the corresponding base date
 *
 * @param[in]	lpMessage	Mapi message to be converted to ical
 * @param[in]	tStart		Base date of the exception
 * @param[out]	lppMessage	Returned exception mapi message
 *
 * @return		MAPI error code
 * @retval		MAPI_E_NOT_FOUND	No exception found
 */
HRESULT VConverter::HrGetExceptionMessage(LPMESSAGE lpMessage, time_t tStart, LPMESSAGE *lppMessage)
{
	object_ptr<IMAPITable> lpAttachTable;
	rowset_ptr lpRows;
	const SPropValue *lpPropVal = nullptr;
	object_ptr<IAttach> lpAttach;
	LPMESSAGE lpAttachedMessage = NULL;
	SPropValue sStart = {0};
	SPropValue sMethod = {0};

	sStart.ulPropTag = PR_EXCEPTION_STARTTIME;
	UnixTimeToFileTime(tStart, &sStart.Value.ft);
	sMethod.ulPropTag = PR_ATTACH_METHOD;
	sMethod.Value.ul = ATTACH_EMBEDDED_MSG;
	auto hr = lpMessage->GetAttachmentTable(0, &~lpAttachTable);
	if (hr != hrSuccess)
		return hr;

	// restrict to only exception attachments
	hr = ECAndRestriction(
		ECExistRestriction(sStart.ulPropTag) +
		ECPropertyRestriction(RELOP_EQ, sStart.ulPropTag, &sStart, ECRestriction::Cheap) +
		ECExistRestriction(sMethod.ulPropTag) +
		ECPropertyRestriction(RELOP_EQ, sMethod.ulPropTag, &sMethod, ECRestriction::Cheap)
	).RestrictTable(lpAttachTable, 0);
	if (hr != hrSuccess)
		return hr;

	// should result in 1 attachment
	hr = lpAttachTable->QueryRows(-1, 0, &~lpRows);
	if (hr != hrSuccess)
		return hr;

	if (lpRows->cRows == 0)
		// if this is a cancel message, no exceptions are present, so ignore.
		return MAPI_E_NOT_FOUND;
	lpPropVal = lpRows->aRow[0].cfind(PR_ATTACH_NUM);
	if (lpPropVal == nullptr)
		return MAPI_E_NOT_FOUND;
	hr = lpMessage->OpenAttach(lpPropVal->Value.ul, nullptr, 0, &~lpAttach);
	if (hr != hrSuccess)
		return hr;
	hr = lpAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, 0, (LPUNKNOWN *)&lpAttachedMessage);
	if (hr != hrSuccess)
		return hr;
	*lppMessage = lpAttachedMessage;
	return hrSuccess;
}

/** 
 * Converts the timezone parameter from an ical time property to MAPI
 * properties, and saves this in the given lpIcalItem object.
 * 
 * @todo this is a ical -> mapi conversion util function, so move to block with all ical->mapi code.
 *
 * @param[in] lpicProp The ical time property to finc the timezone in
 * @param[in,out] lpIcalItem This object is modified
 * 
 * @return MAPI error code
 */
HRESULT VConverter::HrAddTimeZone(icalproperty *lpicProp, icalitem *lpIcalItem)
{
	const char *lpszTZID = NULL;
	std::string strTZ;
	SPropValue sPropVal;

	// Take the timezone from DTSTART and set that as the item timezone
	auto lpicTZParam = icalproperty_get_first_parameter(lpicProp, ICAL_TZID_PARAMETER);
	// All day recurring items may not have timezone data.
	if (lpicTZParam == NULL && lpicProp == NULL)
		return hrSuccess;

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TIMEZONE], PT_UNICODE);
	if (lpicTZParam != NULL) {
		strTZ = urlDecode(icalparameter_get_tzid(lpicTZParam));
		lpszTZID = strTZ.c_str();
	} else if (!m_mapTimeZones->empty()) {
		lpszTZID = (m_mapTimeZones->begin()->first).c_str();
	} else {
		return hrSuccess;
	}

	HrCopyString(m_converter, m_strCharset, lpIcalItem->base, lpszTZID, &sPropVal.Value.lpszW);
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// keep found timezone also as current timezone. will be used in recurrence
	m_iCurrentTimeZone = m_mapTimeZones->find(lpszTZID);
	if (m_iCurrentTimeZone == m_mapTimeZones->cend())
		//.. huh? did find a timezone id, but not the actual timezone?? FAIL!
		return MAPI_E_NOT_FOUND;
		
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TIMEZONEDATA], PT_BINARY);
	sPropVal.Value.bin.cb = sizeof(TIMEZONE_STRUCT);

	HRESULT hr = MAPIAllocateMore(sizeof(TIMEZONE_STRUCT), lpIcalItem->base, (void**)&sPropVal.Value.bin.lpb);
	if (hr != hrSuccess)
		return hr;
	memcpy(sPropVal.Value.bin.lpb, &m_iCurrentTimeZone->second, sizeof(TIMEZONE_STRUCT));
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	// save timezone in icalitem
	lpIcalItem->tTZinfo = m_iCurrentTimeZone->second;
	return hrSuccess;
}

/**
 * Returns the Allday Status from the ical data as a boolean.
 * 
 * Checks for "DTSTART" weather it contains a date, and sets the all
 * day status as true.  If that property was not found then checks if
 * "X-MICROSOFT-CDO-ALLDAYEVENT" property to set the all day status.
 *
 * @param[in]	lpicEvent		VEVENT ical component
 * @param[out]	lpblIsAllday	Return variable to for allday status
 * @return		Always returns hrSuccess 
 */ 
HRESULT VConverter::HrRetrieveAlldayStatus(icalcomponent *lpicEvent, bool *lpblIsAllday)
{
	// Note: we do not set bIsAllDay to true when (END-START)%24h == 0
	// If the user forced his ICAL client not to set this to 'true', it really wants an item that is a multiple of 24h, but specify the times too.
	auto icStart = icalcomponent_get_dtstart(lpicEvent);
	if (icStart.is_date)
	{
		*lpblIsAllday = true;
		return hrSuccess;
	}

	// only assume the X header valid when it's a non-floating timestamp.
	// also check is_utc and/or zone pointer in DTSTART/DTEND ?
	auto icEnd = icalcomponent_get_dtend(lpicEvent);
	if (icStart.hour + icStart.minute + icStart.second != 0 ||
	    icEnd.hour + icEnd.minute + icEnd.second != 0) {
		*lpblIsAllday = false;
		return hrSuccess;
	}
	for (auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_X_PROPERTY);
	     lpicProp != nullptr;
	     lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_X_PROPERTY))
		if (strcmp(icalproperty_get_x_name(lpicProp), "X-MICROSOFT-CDO-ALLDAYEVENT") == 0){
			*lpblIsAllday = strcmp(icalproperty_get_x(lpicProp), "TRUE") == 0;
			break;
		}
	return hrSuccess;
}

/**
 * Handles mapi to ical conversion of message with recurrence and
 * exceptions. This is the entrypoint of the class.
 * 
 * @param[in]	lpMessage		Mapi message to be converted to ical
 * @param[out]	lpicMethod		The ical method set in the ical data
 * @param[out]	lpEventList		List of ical components retuned after ical conversion
 * @return		MAPI error code
 */
HRESULT VConverter::HrMAPI2ICal(LPMESSAGE lpMessage, icalproperty_method *lpicMethod, std::list<icalcomponent*> *lpEventList)
{
	std::list<icalcomponent*> lstEvents;
	icalproperty_method icMainMethod = ICAL_METHOD_NONE;
	icalcomp_ptr lpicEvent;
	memory_ptr<SPropValue> lpSpropValArray;
	std::unique_ptr<icaltimezone, icalmapi_delete> lpicTZinfo;
	std::string strTZid;
	SizedSPropTagArray(3, proptags) = {3,
		{CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RECURRING], PT_BOOLEAN),
		CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_ISRECURRING], PT_BOOLEAN),
		CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_ISRECURRING], PT_BOOLEAN)}};

	// handle toplevel
	auto hr = HrMAPI2ICal(lpMessage, &icMainMethod, &unique_tie(lpicTZinfo),
	          &strTZid, &unique_tie(lpicEvent));
	if (hr != hrSuccess)
		return hr;

	ULONG cbSize = 0;
	hr = lpMessage->GetProps(proptags, 0, &cbSize, &~lpSpropValArray);
	if (FAILED(hr))
		return hrSuccess;

	hr = hrSuccess;
	// if recurring, add recurrence. We have to check two props since CDO only sets the second, while Outlook only sets the first :S
	if (((PROP_TYPE(lpSpropValArray[0].ulPropTag) != PT_ERROR) &&
		lpSpropValArray[0].Value.b == TRUE) || 
		((PROP_TYPE(lpSpropValArray[1].ulPropTag) != PT_ERROR) &&
		lpSpropValArray[1].Value.b == TRUE) || 
		((PROP_TYPE(lpSpropValArray[2].ulPropTag) != PT_ERROR) &&
		lpSpropValArray[2].Value.b == TRUE))
	{
		hr = HrSetRecurrence(lpMessage, lpicEvent.get(),
		     lpicTZinfo.get(), strTZid, &lstEvents);
		if (hr != hrSuccess)
			return hr;
	}

	// push the main event in the front, before all exceptions
	lstEvents.emplace_front(lpicEvent.release());
	// end
	*lpicMethod = icMainMethod;
	*lpEventList = std::move(lstEvents);
	return hr;
}

/**
 * The actual MAPI to ical conversion for mapi message excluding
 * recurrence. This function is used to implement both VTodo and
 * VEvent conversion.
 *
 * @param[in]	lpMessage		Mapi message to be converted to ical
 * @param[out]	lpicMethod		The ical method set in the ical data
 * @param[out]	lppicTZinfo		ical timezone
 * @param[out]	lpstrTZid		timezone name
 * @param[out]	lpEvent			ical component containing ical data
 * @return		MAPI error code
 */
HRESULT VConverter::HrMAPI2ICal(LPMESSAGE lpMessage, icalproperty_method *lpicMethod, icaltimezone **lppicTZinfo, std::string *lpstrTZid, icalcomponent *lpEvent)
{
	icalproperty_method icMethod = ICAL_METHOD_NONE;
	icalproperty *lpProp = NULL;
	memory_ptr<SPropValue> lpMsgProps;
	ULONG ulMsgProps = 0;
	TIMEZONE_STRUCT ttTZinfo = {0};
	icaltimezone *lpicTZinfo = NULL;
	ULONG ulCount = 0;	
	std::string strTZid;
	std::string strUid;
	std::wstring wstrBuf;

	auto hr = lpMessage->GetProps(NULL, MAPI_UNICODE, &ulMsgProps, &~lpMsgProps);
	if (FAILED(hr))
		return hr;
	hr = HrFindTimezone(ulMsgProps, lpMsgProps, &strTZid, &ttTZinfo, &lpicTZinfo);
	if (hr != hrSuccess)
		return hr;
	// non-UTC timezones are placed in the map, and converted using HrCreateVTimeZone() in MAPIToICal.cpp

	if(!m_bCensorPrivate) {
		// not an exception, so parent message is the message itself
		hr = HrSetOrganizerAndAttendees(lpMessage, lpMessage, ulMsgProps, lpMsgProps, &icMethod, lpEvent);
		if (hr != hrSuccess)
			return hr;
	}

	// Set show_time_as / TRANSP
	auto lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_BUSYSTATUS], PT_LONG));
	if (!m_bCensorPrivate && lpPropVal)
		HrSetBusyStatus(lpMessage, lpPropVal->Value.ul, lpEvent);
	
	
	hr = HrSetTimeProperties(lpMsgProps, ulMsgProps, lpicTZinfo, strTZid, lpEvent);
	if (hr != hrSuccess)
		return hr;

	// Set RECURRENCE-ID for exception
	hr = HrSetRecurrenceID(lpMsgProps, ulMsgProps, lpicTZinfo, strTZid, lpEvent);
	if (hr != hrSuccess)
		return hr;

	// Set subject / SUMMARY
	if(m_bCensorPrivate) {
		lpProp = icalproperty_new_summary("Private Appointment");
		icalcomponent_add_property(lpEvent, lpProp);
	}
	else {
		lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, PR_SUBJECT_W);
		if (lpPropVal && lpPropVal->Value.lpszW[0] != '\0') {
			lpProp = icalproperty_new_summary(m_converter.convert_to<string>(m_strCharset.c_str(), lpPropVal->Value.lpszW, rawsize(lpPropVal->Value.lpszW), CHARSET_WCHAR).c_str());
			icalcomponent_add_property(lpEvent, lpProp);
		}
	}

	// Set location / LOCATION
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_LOCATION], PT_UNICODE));
	if (!m_bCensorPrivate && lpPropVal && lpPropVal->Value.lpszW[0] != '\0') {
		lpProp = icalproperty_new_location(m_converter.convert_to<string>(m_strCharset.c_str(), lpPropVal->Value.lpszW, rawsize(lpPropVal->Value.lpszW), CHARSET_WCHAR).c_str());
		icalcomponent_add_property(lpEvent, lpProp);
	}

	// Set body / DESCRIPTION
	if(!m_bCensorPrivate) {
		lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, PR_BODY_W);
		if (lpPropVal && lpPropVal->Value.lpszW[0] != '\0') {
			std::wstring strBody;

			// The body is converted as OL2003 does not parse '\r' & '\t' correctly
			// Newer versions also have some issues parsing there chars
			StringTabtoSpaces(lpPropVal->Value.lpszW, &strBody);
			StringCRLFtoLF(strBody, &strBody);

			lpProp = icalproperty_new_description(m_converter.convert_to<string>(m_strCharset.c_str(), lpPropVal->Value.lpszW, rawsize(lpPropVal->Value.lpszW), CHARSET_WCHAR).c_str());
		} else {
			hr = HrSetBody(lpMessage, &lpProp);
		}
		if (hr == hrSuccess)
			icalcomponent_add_property(lpEvent, lpProp);
		hr = hrSuccess;
	}

	// Set priority - use PR_IMPORTANCE or PR_PRIORITY
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, PR_IMPORTANCE);
	if (!m_bCensorPrivate && lpPropVal) {
		lpProp = icalproperty_new_priority(5 - ((lpPropVal->Value.l - 1) * 4));
		icalcomponent_add_property(lpEvent, lpProp);
	} else {
		lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, PR_PRIORITY);
		if (!m_bCensorPrivate && lpPropVal && lpPropVal->Value.l != 0) {
			lpProp = icalproperty_new_priority(5 - (lpPropVal->Value.l * 4));
			icalcomponent_add_property(lpEvent, lpProp);
		}
	}

	// Set keywords / CATEGORIES
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_KEYWORDS], PT_MV_UNICODE));
	if (lpPropVal && lpPropVal->Value.MVszA.cValues > 0) {
		// The categories need to be comma-separated
		wstrBuf.reserve(lpPropVal->Value.MVszW.cValues * 50); // 50 chars per category is a wild guess, but more than enough
		for (ulCount = 0; ulCount < lpPropVal->Value.MVszW.cValues; ++ulCount) {
			if (ulCount)
				wstrBuf += L",";
			wstrBuf += lpPropVal->Value.MVszW.lppszW[ulCount];
		}

		if (!wstrBuf.empty()) {
			lpProp = icalproperty_new_categories(m_converter.convert_to<string>(m_strCharset.c_str(), wstrBuf, rawsize(wstrBuf), CHARSET_WCHAR).c_str());
			icalcomponent_add_property(lpEvent, lpProp);
		}
	}

	// Set url
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_NETSHOWURL], PT_UNICODE));
	if (lpPropVal && lpPropVal->Value.lpszW[0] != '\0') {
		lpProp = icalproperty_new_url(m_converter.convert_to<string>(m_strCharset.c_str(), lpPropVal->Value.lpszW, rawsize(lpPropVal->Value.lpszW), CHARSET_WCHAR).c_str());
		icalcomponent_add_property(lpEvent, lpProp);
	}

	// Set contacts
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_CONTACTS], PT_MV_UNICODE));
	if (lpPropVal) {
		for (ulCount = 0; ulCount < lpPropVal->Value.MVszW.cValues; ++ulCount) {
			lpProp = icalproperty_new_contact(m_converter.convert_to<string>(m_strCharset.c_str(), lpPropVal->Value.MVszW.lppszW[ulCount], rawsize(lpPropVal->Value.MVszW.lppszW[ulCount]), CHARSET_WCHAR).c_str());
			icalcomponent_add_property(lpEvent, lpProp);
		}
	}

	// Set sensivity / CLASS
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, PR_SENSITIVITY);
	if (lpPropVal) {
		switch (lpPropVal->Value.ul) {
		case 1: //Personal
		case 2: //Private
			lpProp = icalproperty_new_class(ICAL_CLASS_PRIVATE);
			break;
		case 3: //CompanyConfidential
			lpProp = icalproperty_new_class(ICAL_CLASS_CONFIDENTIAL);
			break;
		default:
			lpProp = icalproperty_new_class(ICAL_CLASS_PUBLIC);
			break;
		}
		icalcomponent_add_property(lpEvent, lpProp);
	}

	// Set and/or create UID
	// Global Object ID
	//   In Microsoft Office Outlook 2003 Service Pack 1 (SP1) and earlier versions, the Global Object ID is generated when an organizer first sends a meeting request.
	//   Earlier versions of Outlook do not generate a Global Object ID for unsent meetings or for appointments that have no recipients.
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY));
	if (lpPropVal == NULL)
		lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_CLEANID], PT_BINARY));

	// If lpPropVal is 0, the global object id and cleanglobal id haven't been found.
	// The iCal UID is saved into both the global object id and cleanglobal id.
	if (lpPropVal == NULL) {
		SPropValue propUid;

		hr = HrGenerateUid(&strUid);
		if (hr != hrSuccess)
			return hr;

		hr = HrMakeBinaryUID(strUid, lpMsgProps, &propUid); // base is lpMsgProps, which will be freed later
		
		// Set global object id and cleanglobal id.
		// ignore write errors, not really required that these properties are saved
		propUid.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY);
		HrSetOneProp(lpMessage, &propUid);
		
		propUid.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_CLEANID], PT_BINARY);
		HrSetOneProp(lpMessage, &propUid);

		// if we cannot write the message, use PR_ENTRYID to have the same uid every time we return the item
		// otherwise, ignore the error
		hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
		if (hr == E_ACCESSDENIED) {
			lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, PR_ENTRYID);
			if (lpPropVal)
				strUid = bin2hex(lpPropVal->Value.bin);
		}
		hr = hrSuccess;
	} else {
		HrGetICalUidFromBinUid(lpPropVal->Value.bin, &strUid);
	}

	if(IsOutlookUid(strUid))
		strUid.replace(32, 8, "00000000");

	lpProp = icalproperty_new_uid(strUid.c_str());
	icalcomponent_add_property(lpEvent,lpProp);

	hr = HrSetItemSpecifics(ulMsgProps, lpMsgProps, lpEvent);
	if (hr != hrSuccess)
		return hr;

	//Sequence
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_APPTSEQNR], PT_LONG));
	if(lpPropVal)
	{
		lpProp = icalproperty_new_sequence(lpPropVal->Value.ul);
		icalcomponent_add_property(lpEvent, lpProp);
	}

	// Set alarm / VALARM (if alarm is found in lpMessage)
	if(!m_bCensorPrivate)	{
		hr = HrSetVAlarm(ulMsgProps, lpMsgProps, lpEvent);
		if (hr != hrSuccess)
			return hr;
	}

	// Set X-Properties.
	hr = HrSetXHeaders(ulMsgProps, lpMsgProps, lpMessage, lpEvent);
	if (hr != hrSuccess)
		return hr;
	
	// set return values
	if (lpicMethod)
		*lpicMethod = icMethod;

	if (lppicTZinfo)
		*lppicTZinfo = lpicTZinfo;

	if (lpstrTZid)
		*lpstrTZid = std::move(strTZid);
	return hrSuccess;
}

} /* namespace */
