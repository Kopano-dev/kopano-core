/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <memory>
#include <new>
#include <kopano/platform.h>
#include <kopano/ECRestriction.h>
#include <kopano/hl.hpp>
#include <kopano/memory.hpp>
#include "ICalToMAPI.h"
#include "vconverter.h"
#include "vtimezone.h"
#include "vevent.h"
#include "vtodo.h"
#include "vfreebusy.h"
#include "nameids.h"
#include "icalrecurrence.h"
#include <mapix.h>
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include <libical/ical.h>
#include <algorithm>
#include <vector>
#include <kopano/stringutil.h>
#include <kopano/charset/convert.h>
#include <mapi.h>
#include "icalmem.hpp"

namespace KC {

class ICalToMapiImpl final : public ICalToMapi {
public:
	/*
	    - lpPropObj to lookup named properties
	    - Addressbook (Global AddressBook for looking up users)
	 */
	ICalToMapiImpl(IMAPIProp *lpPropObj, LPADRBOOK lpAdrBook, bool bNoRecipients);
	HRESULT ParseICal(const std::string &ical, const std::string &cset, const std::string &server_tz, IMailUser *, unsigned int flags) override;
	unsigned int GetItemCount() override;
	HRESULT GetItemInfo(unsigned int pos, eIcalType *, time_t *last_mod, SBinary *uid) override;
	HRESULT GetItem(unsigned int pos, unsigned int flags, IMessage *) override;
	HRESULT GetFreeBusyInfo(time_t *start, time_t *end, std::string *uid, std::list<std::string> **users) override;

private:
	void Clean();
	HRESULT SaveAttendeesString(const std::list<icalrecip> *lplstRecip, LPMESSAGE lpMessage);
	HRESULT SaveProps(const std::list<SPropValue> *lpPropList, IMAPIProp *, unsigned int flags = 0);
	HRESULT SaveRecipList(const std::list<icalrecip> *lplstRecip, ULONG ulFlag, LPMESSAGE lpMessage);
	memory_ptr<SPropTagArray> m_lpNamedProps;
	ULONG m_ulErrorCount = 0;
	TIMEZONE_STRUCT ttServerTZ;
	std::string strServerTimeZone;

	/* Contains a list of messages after ParseICal
	 * Use GetItem() to get one of these messages
	 */
	std::vector<std::unique_ptr<icalitem>> m_vMessages;

	// freebusy information
	bool m_bHaveFreeBusy = false;
	time_t m_tFbStart = 0;
	time_t m_tFbEnd = 0;
	std::string m_strUID;
	std::list<std::string> m_lstUsers;
};

/** 
 * Create a class implementing the ICalToMapi "interface".
 * 
 * @param[in]  lpPropObj MAPI object used to find named properties
 * @param[in]  lpAdrBook MAPI Addressbook
 * @param[in]  bNoRecipients Skip recipients from ical. Used for DAgent, which uses the mail recipients
 * @param[out] lppICalToMapi The ICalToMapi class
 */
HRESULT CreateICalToMapi(IMAPIProp *lpPropObj, LPADRBOOK lpAdrBook, bool bNoRecipients, ICalToMapi **lppICalToMapi)
{
	if (lpPropObj == nullptr || lppICalToMapi == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	*lppICalToMapi = new(std::nothrow) ICalToMapiImpl(lpPropObj, lpAdrBook, bNoRecipients);
	if (*lppICalToMapi == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	return hrSuccess;
}

/** 
 * Init ICalToMapi class
 * 
 * @param[in] lpPropObj passed to super class
 * @param[in] lpAdrBook passed to super class
 * @param[in] bNoRecipients passed to super class
 */
ICalToMapiImpl::ICalToMapiImpl(IMAPIProp *lpPropObj, LPADRBOOK lpAdrBook, bool bNoRecipients) : ICalToMapi(lpPropObj, lpAdrBook, bNoRecipients)
{
	memset(&ttServerTZ, 0, sizeof(TIMEZONE_STRUCT));
}

/** 
 * Frees and resets all used memory of the ICalToMapi class. The class
 * can be reused for another conversion after this call. Named
 * properties are kept, since you cannot switch items from a store.
 */
void ICalToMapiImpl::Clean()
{
	m_ulErrorCount = 0;
	m_vMessages.clear();
	m_bHaveFreeBusy = false;
	m_lstUsers.clear();
	m_tFbStart = 0;
	m_tFbEnd = 0;
	m_strUID.clear();
}

/** 
 * Parses an ICal string (with a certain charset) and converts the
 * data in memory. The real MAPI object can be retrieved using
 * GetItem().
 * 
 * @param[in] strIcal The ICal data to parse
 * @param[in] strCharset The charset of strIcal (usually UTF-8)
 * @param[in] strServerTZparam ID of default timezone to use if ICal data didn't specify
 * @param[in] lpMailUser IMailUser object of the current user (CalDav: the user logged in, DAgent: the user being delivered for)
 * @param[in] ulFlags Conversion flags - currently unused
 * 
 * @return MAPI error code
 */
HRESULT ICalToMapiImpl::ParseICal(const std::string& strIcal, const std::string& strCharset, const std::string& strServerTZparam, IMailUser *lpMailUser, ULONG ulFlags)
{
	TIMEZONE_STRUCT ttTimeZone = {0};
	timezone_map tzMap;
	std::string strTZID;
	icalitem *item = nullptr, *previtem = nullptr;

	Clean();
	if (m_lpNamedProps == NULL) {
		auto hr = HrLookupNames(m_lpPropObj, &~m_lpNamedProps);
		if (hr != hrSuccess)
			return hr;
	}

	icalerror_clear_errno();
	icalcomp_ptr lpicCalendar(icalparser_parse_string(strIcal.c_str()));

	if (lpicCalendar == NULL || icalerrno != ICAL_NO_ERROR) {
		switch (icalerrno) {
		case ICAL_BADARG_ERROR:
		case ICAL_USAGE_ERROR:
			return MAPI_E_INVALID_PARAMETER;
		case ICAL_NEWFAILED_ERROR:
		case ICAL_ALLOCATION_ERROR:
			return MAPI_E_NOT_ENOUGH_MEMORY;
		case ICAL_MALFORMEDDATA_ERROR:
			return MAPI_E_CORRUPT_DATA;
		case ICAL_FILE_ERROR:
			return MAPI_E_DISK_ERROR;
		case ICAL_UNIMPLEMENTED_ERROR:
			return E_NOTIMPL;
		case ICAL_UNKNOWN_ERROR:
		case ICAL_PARSE_ERROR:
		case ICAL_INTERNAL_ERROR:
		case ICAL_NO_ERROR:
			return MAPI_E_CALL_FAILED;
		}
		return hrSuccess;
	}

	if (icalcomponent_isa(lpicCalendar.get()) != ICAL_VCALENDAR_COMPONENT &&
	    icalcomponent_isa(lpicCalendar.get()) != ICAL_XROOT_COMPONENT)
		return MAPI_E_INVALID_OBJECT;

	m_ulErrorCount = icalcomponent_count_errors(lpicCalendar.get());

	/* Find all timezones, place in map. */
	for (auto lpicComponent = icalcomponent_get_first_component(lpicCalendar.get(), ICAL_VTIMEZONE_COMPONENT);
	     lpicComponent != nullptr;
	     lpicComponent = icalcomponent_get_next_component(lpicCalendar.get(), ICAL_VTIMEZONE_COMPONENT))
	{
		auto hr = HrParseVTimeZone(lpicComponent, &strTZID, &ttTimeZone);
		if (hr != hrSuccess)
			/* log warning? */ ;
		else
			tzMap[strTZID] = ttTimeZone;
	}

	// ICal file did not send any timezone information
	if (tzMap.empty() && strServerTimeZone.empty() &&
	    // find timezone from given server timezone
	    HrGetTzStruct(strServerTZparam, &ttServerTZ) == hrSuccess) {
		strServerTimeZone = strServerTZparam;
		tzMap[strServerTZparam] = ttServerTZ;
	}

	// find all "messages" vevent, vtodo, vjournal, ...?
	for (auto lpicComponent = icalcomponent_get_first_component(lpicCalendar.get(), ICAL_ANY_COMPONENT);
	     lpicComponent != nullptr;
	     lpicComponent = icalcomponent_get_next_component(lpicCalendar.get(), ICAL_ANY_COMPONENT))
	{
		std::unique_ptr<VConverter> lpVEC;
		auto type = icalcomponent_isa(lpicComponent);
		switch (type) {
		case ICAL_VEVENT_COMPONENT:
			static_assert(std::is_polymorphic<VEventConverter>::value, "VEventConverter needs to be polymorphic for unique_ptr to work");
			lpVEC.reset(new VEventConverter(m_lpAdrBook, &tzMap, m_lpNamedProps, strCharset, false, m_bNoRecipients, lpMailUser));
			break;
		case ICAL_VTODO_COMPONENT:
			static_assert(std::is_polymorphic<VTodoConverter>::value, "VTodoConverter needs to be polymorphic for unique_ptr to work");
			lpVEC.reset(new VTodoConverter(m_lpAdrBook, &tzMap, m_lpNamedProps, strCharset, false, m_bNoRecipients, lpMailUser));
			break;
		case ICAL_VFREEBUSY_COMPONENT:
			break;
		case ICAL_VJOURNAL_COMPONENT:
		default:
			continue;
		};

		HRESULT hr = hrSuccess;
		switch (type) {
		case ICAL_VFREEBUSY_COMPONENT:
			hr = HrGetFbInfo(lpicComponent, &m_tFbStart, &m_tFbEnd, &m_strUID, &m_lstUsers);
			if (hr == hrSuccess)
				m_bHaveFreeBusy = true;
			break;
		case ICAL_VEVENT_COMPONENT:
		case ICAL_VTODO_COMPONENT:
			hr = lpVEC->HrICal2MAPI(lpicCalendar.get(), lpicComponent, previtem, &item);
			break;
		default:
			break;
		};

		if (hr == hrSuccess && item != nullptr && previtem != item) {
			// previtem is equal to item when item was only updated (e.g. vevent exception)
			m_vMessages.emplace_back(item);
			previtem = item;
		}
	}

	// TODO: sort m_vMessages on sBinGuid in icalitem struct, so caldav server can use optimized algorithm for finding the same items in MAPI
	// seems this happens quite fast .. don't know what's wrong with exchange's ical
// 	if (m_ulErrorCount != 0)
// 		hr = MAPI_W_ERRORS_RETURNED;
	return hrSuccess;
}

/** 
 * Returns the number of items parsed
 * 
 * @return Max number +1 to pass in GetItem and GetItemInfo ulPosition parameter.
 */
ULONG ICalToMapiImpl::GetItemCount()
{
	return m_vMessages.size();
}

/** 
 * Get some information about an ical item at a certain position.
 * 
 * @param[in]  ulPosition The position of the ical item (flat list)
 * @param[out] lpType Type of the object, VEVENT or VTODO. (VJOURNAL currently unsupported), nullptr if not interested
 * @param[out] lptLastModified Last modification timestamp of the object, nullptr if not interested
 * @param[out] lpUid UID of the object, nullptr if not interested
 * 
 * @return MAPI error code
 * @retval MAPI_E_INVALID_PARAMETER ulPosition out of range, or all return values NULL
 */
HRESULT ICalToMapiImpl::GetItemInfo(ULONG ulPosition, eIcalType *lpType, time_t *lptLastModified, SBinary *lpUid)
{
	if (ulPosition >= m_vMessages.size())
		return MAPI_E_INVALID_PARAMETER;
	if (lpType == NULL && lptLastModified == NULL && lpUid == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (lpType)
		*lpType = m_vMessages[ulPosition]->eType;
	if (lptLastModified)
		*lptLastModified = m_vMessages[ulPosition]->tLastModified;
	if (lpUid)
		*lpUid = m_vMessages[ulPosition]->sBinGuid.Value.bin;
	return hrSuccess;
}

/** 
 * Get information of the freebusy data in the parsed ical. All parameters are optional.
 * 
 * @param[out] lptstart The start time of the freebusy data
 * @param[out] lptend  The end time of the freebusy data
 * @param[out] lpstrUID The UID of the freebusy data
 * @param[out] lplstUsers A list of the email addresses of users in freebusy data, @note: internal data returned!
 * 
 * @return MAPI error code
 */
HRESULT ICalToMapiImpl::GetFreeBusyInfo(time_t *lptstart, time_t *lptend, std::string *lpstrUID, std::list<std::string> **lplstUsers)
{
	if (!m_bHaveFreeBusy)
		return MAPI_E_NOT_FOUND;
	if (lptend)
		*lptend = m_tFbEnd;
	if (lptstart != NULL)
		*lptstart = m_tFbStart;
	if (lpstrUID)
		*lpstrUID = m_strUID;
	if (lplstUsers)
		*lplstUsers = &m_lstUsers;
	return hrSuccess;
}

/**
 * Sets mapi properties in Imessage object from the icalitem.
 *
 * @param[in]		ulPosition		specifies the message that is to be retrieved
 * @param[in]		ulFlags			conversion flags
 * @arg @c IC2M_NO_RECIPIENTS skip recipients in conversion from ICal to MAPI
 * @arg @c IC2M_APPEND_ONLY	do not delete properties in lpMessage that are not present in ICal, but possebly are in lpMessage
 * @param[in,out]	lpMessage		IMessage in which properties has to be set
 *
 * @return			MAPI error code
 * @retval			MAPI_E_INVALID_PARAMETER	invalid position set in ulPosition or NULL IMessage parameter
 */
HRESULT ICalToMapiImpl::GetItem(ULONG ulPosition, ULONG ulFlags, LPMESSAGE lpMessage)
{
	HRESULT hr = hrSuccess;
	ICalRecurrence cRec;
	ULONG ulANr = 0;
	memory_ptr<SPropTagArray> lpsPTA;
	object_ptr<IMAPITable> lpAttachTable;
	rowset_ptr lpRows;
	SPropValue sStart = {0}, sMethod = {0};

	if (ulPosition >= m_vMessages.size() || lpMessage == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	auto iItem = m_vMessages.begin() + ulPosition;
	auto lpItem = iItem->get();

	if ((ulFlags & IC2M_APPEND_ONLY) == 0 && !lpItem->lstDelPropTags.empty()) {
		hr = MAPIAllocateBuffer(CbNewSPropTagArray(lpItem->lstDelPropTags.size()), &~lpsPTA);
		if (hr != hrSuccess)
			return hr;
		std::copy(lpItem->lstDelPropTags.begin(), lpItem->lstDelPropTags.end(), lpsPTA->aulPropTag);
		lpsPTA->cValues = lpItem->lstDelPropTags.size();
		hr = lpMessage->DeleteProps(lpsPTA, NULL);
		if (hr != hrSuccess)
			return hr;
	}

	hr = SaveProps(&lpItem->lstMsgProps, lpMessage, ulFlags);
	if (hr != hrSuccess)
		return hr;
	if (!(ulFlags & IC2M_NO_RECIPIENTS))
		hr = SaveRecipList(&(lpItem->lstRecips), ulFlags, lpMessage);
	if (hr != hrSuccess)
		return hr;
	hr = SaveAttendeesString(&(lpItem->lstRecips), lpMessage);
	if (hr != hrSuccess)
		return hr;
	// remove all exception attachments from message, if any
	hr = lpMessage->GetAttachmentTable(0, &~lpAttachTable);
	if (hr != hrSuccess)
		goto next;

	sStart.ulPropTag = PR_EXCEPTION_STARTTIME;
	// 1-1-4501 00:00
	sStart.Value.ft.dwLowDateTime = 0xA3DD4000;
	sStart.Value.ft.dwHighDateTime = 0x0CB34557;
	sMethod.ulPropTag = PR_ATTACH_METHOD;
	sMethod.Value.ul = ATTACH_EMBEDDED_MSG;

	// restrict to only exception attachments
	// (((!pr_exception_starttime) or (pr_exception_starttime != 1-1-4501)) and (have method) and (method == 5))
	// (AND( OR( NOT(pr_exception_start) (pr_exception_start!=time)) (have method)(method==5)))
	hr = ECAndRestriction(
		ECOrRestriction(
			ECNotRestriction(ECExistRestriction(sStart.ulPropTag)) +
			ECPropertyRestriction(RELOP_NE, sStart.ulPropTag, &sStart, ECRestriction::Cheap)
		) +
		ECExistRestriction(sMethod.ulPropTag) +
		ECPropertyRestriction(RELOP_EQ, sMethod.ulPropTag, &sMethod, ECRestriction::Cheap)
	).RestrictTable(lpAttachTable, 0);
	if (hr != hrSuccess)
		return hr;
	hr = lpAttachTable->QueryRows(-1, 0, &~lpRows);
	if (hr != hrSuccess)
		return hr;

	for (ULONG i = 0; i < lpRows->cRows; ++i) {
		auto lpPropVal = lpRows[i].cfind(PR_ATTACH_NUM);
		if (lpPropVal == NULL)
			continue;
		hr = lpMessage->DeleteAttach(lpPropVal->Value.ul, 0, NULL, 0);
		if (hr != hrSuccess)
			return hr;
	}

next:
	// add recurring properties & add exceptions
	if (lpItem->lpRecurrence == nullptr)
		return hrSuccess;
	hr = cRec.HrMakeMAPIRecurrence(lpItem->lpRecurrence.get(), m_lpNamedProps, lpMessage);
	// TODO: log error if any?
	// check if all exceptions are valid
	for (const auto &ex : lpItem->lstExceptionAttachments)
		if (!cRec.HrValidateOccurrence(lpItem, ex))
			return MAPI_E_INVALID_OBJECT;
	for (const auto &ex : lpItem->lstExceptionAttachments) {
		object_ptr<IAttach> lpAttach;
		object_ptr<IMessage> lpExMsg;

		hr = lpMessage->CreateAttach(nullptr, 0, &ulANr, &~lpAttach);
		if (hr != hrSuccess)
			return hr;
		hr = lpAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY, &~lpExMsg);
		if (hr != hrSuccess)
			return hr;
		if (!(ulFlags & IC2M_NO_RECIPIENTS))
			hr = SaveRecipList(&ex.lstRecips, ulFlags, lpExMsg);
		if (hr != hrSuccess)
			return hr;
		hr = SaveAttendeesString(&ex.lstRecips, lpExMsg);
		if (hr != hrSuccess)
			return hr;
		hr = SaveProps(&ex.lstMsgProps, lpExMsg);
		if (hr != hrSuccess)
			return hr;
		hr = SaveProps(&ex.lstAttachProps, lpAttach);
		if (hr != hrSuccess)
			return hr;
		hr = lpExMsg->SaveChanges(0);
		if (hr != hrSuccess)
			return hr;
		hr = lpAttach->SaveChanges(0);
		if (hr != hrSuccess)
			return hr;
	}
	return hr;
}

/** 
 * Helper function for GetItem. Saves all properties converted from
 * ICal to MAPI in the MAPI object. Does not call SaveChanges.
 * 
 * @param[in] lpPropList list of properties to save in lpMapiProp
 * @param[in] lpMapiProp The MAPI object to save properties in
 * 
 * @return MAPI error code
 */
HRESULT ICalToMapiImpl::SaveProps(const std::list<SPropValue> *lpPropList,
    LPMAPIPROP lpMapiProp, unsigned int flags)
{
	memory_ptr<SPropValue> lpsPropVals;

	// all props to message
	auto hr = MAPIAllocateBuffer(lpPropList->size() * sizeof(SPropValue), &~lpsPropVals);
	if (hr != hrSuccess)
		return hr;

	// @todo: add exclude list or something? might set props the caller doesn't want (see vevent::HrAddTimes())
	int i = 0;
	for (const auto &prop : *lpPropList) {
		if (flags & IC2M_NO_BODY &&
		    PROP_ID(prop.ulPropTag) == PROP_ID(PR_BODY))
			continue;
		lpsPropVals[i++] = prop;
	}
	return lpMapiProp->SetProps(i, lpsPropVals, NULL);
}

/**
 * Helper function for GetItem. Converts recipient list to MAPI
 * recipients. Always replaces the complete recipient list in
 * lpMessage.
 * 
 * @param[in] lplstRecip List of parsed ical recipients from a certain item
 * @param[in] lpMessage MAPI message to modify recipient table for
 * 
 * @return MAPI error code
 */
HRESULT ICalToMapiImpl::SaveRecipList(const std::list<icalrecip> *lplstRecip,
    ULONG ulFlag, LPMESSAGE lpMessage)
{
	adrlist_ptr lpRecipients;
	std::string strSearch;
	ULONG i = 0;
	convert_context converter;

	auto hr = MAPIAllocateBuffer(CbNewADRLIST(lplstRecip->size()), &~lpRecipients);
	if (hr != hrSuccess)
		return hr;

	lpRecipients->cEntries = 0;
	for (const auto &recip : *lplstRecip) {
		// iRecip->ulRecipientType
		// strEmail
		// strName
		// cbEntryID, lpEntryID
		if ((ulFlag & IC2M_NO_ORGANIZER) && recip.ulRecipientType == MAPI_ORIG)
			continue;
		hr = MAPIAllocateBuffer(sizeof(SPropValue) * 10,
		     reinterpret_cast<void **>(&lpRecipients->aEntries[i].rgPropVals));
		if (hr != hrSuccess)
			return hr;
		lpRecipients->aEntries[i].cValues = 10;

		const auto &rg = lpRecipients->aEntries[i].rgPropVals;
		rg[0].ulPropTag = PR_RECIPIENT_TYPE;
		rg[0].Value.ul = recip.ulRecipientType;
		rg[1].ulPropTag = PR_DISPLAY_NAME_W;
		rg[1].Value.lpszW = const_cast<wchar_t *>(recip.strName.c_str());
		rg[2].ulPropTag = PR_SMTP_ADDRESS_W;
		rg[2].Value.lpszW = const_cast<wchar_t *>(recip.strEmail.c_str());
		rg[3].ulPropTag = PR_ENTRYID;
		rg[3].Value.bin.cb = recip.cbEntryID;
		hr = KAllocCopy(recip.lpEntryID, recip.cbEntryID, reinterpret_cast<void **>(&rg[3].Value.bin.lpb), rg);
		if (hr != hrSuccess)
			return hr;
		rg[4].ulPropTag = PR_ADDRTYPE_W;
		rg[4].Value.lpszW = const_cast<wchar_t *>(L"SMTP");
		strSearch = strToUpper("SMTP:" + converter.convert_to<std::string>(recip.strEmail));
		rg[5].ulPropTag = PR_SEARCH_KEY;
		rg[5].Value.bin.cb = strSearch.size() + 1;
		hr = KAllocCopy(strSearch.c_str(), strSearch.size() + 1, reinterpret_cast<void **>(&rg[5].Value.bin.lpb), rg);
		if (hr != hrSuccess)
			return hr;
		rg[6].ulPropTag = PR_EMAIL_ADDRESS_W;
		rg[6].Value.lpszW = const_cast<wchar_t *>(recip.strEmail.c_str());
		rg[7].ulPropTag = PR_DISPLAY_TYPE;
		rg[7].Value.ul = DT_MAILUSER;
		rg[8].ulPropTag = PR_RECIPIENT_FLAGS;
		rg[8].Value.ul = recip.ulRecipientType == MAPI_ORIG? 3 : 1;
		rg[9].ulPropTag = PR_RECIPIENT_TRACKSTATUS;
		rg[9].Value.ul = recip.ulTrackStatus;
		++lpRecipients->cEntries;
		++i;
	}

	// flag 0: remove old recipient table, and add the new list
	return lpMessage->ModifyRecipients(0, lpRecipients);	
}

/**
 * Save attendees strings in the message
 *
 * @param[in] 		lplstRecip	Pointer to a recipient list.
 * @param[in,out]   lpMessage	Pointer to the new MAPI message.
 *
 * @return	Returns always S_OK except when there is an error.
 *
 * @note  The properties dispidNonSendableCC, dispidNonSendableBCC are not set
 *
 */
HRESULT ICalToMapiImpl::SaveAttendeesString(const std::list<icalrecip> *lplstRecip, LPMESSAGE lpMessage)
{
	std::wstring strAllAttendees, strToAttendees, strCCAttendees;
	KPropbuffer<3> pv;

	// Create attendees string
	for (const auto &recip : *lplstRecip) {
		if (recip.ulRecipientType == MAPI_ORIG)
			continue;
		if (recip.ulRecipientType == MAPI_TO) {
			if (!strToAttendees.empty())
				strToAttendees += L"; ";
			strToAttendees += recip.strName;
		} else if (recip.ulRecipientType == MAPI_CC) {
			if (!strCCAttendees.empty())
				strCCAttendees += L"; ";
			strCCAttendees += recip.strName;
		}
		if (!strAllAttendees.empty())
			strAllAttendees += L"; ";
		strAllAttendees += recip.strName;
	}

	pv.set(0, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TOATTENDEESSTRING], PT_UNICODE), std::move(strToAttendees));
	pv.set(1, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_CCATTENDEESSTRING], PT_UNICODE), std::move(strCCAttendees));
	pv.set(2, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_ALLATTENDEESSTRING], PT_UNICODE), std::move(strAllAttendees));
	return lpMessage->SetProps(3, pv.get(), nullptr);
}

} /* namespace */
