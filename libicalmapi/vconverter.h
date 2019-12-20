/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ICALMAPI_VCONVERTER_H
#define ICALMAPI_VCONVERTER_H

#include <kopano/zcdefs.h>
#include "vtimezone.h"
#include "icalitem.h"
#include <kopano/RecurrenceState.h>
#include <kopano/charset/convert.h>
#include <mapidefs.h>
#include <libical/ical.h>

namespace KC {

class VConverter {
public:
	/* lpNamedProps must be the GetIDsFromNames() of the array in nameids.h */
	VConverter(LPADRBOOK lpAdrBook, timezone_map *mapTimeZones, LPSPropTagArray lpNamedProps, const std::string& strCharset, bool blCensor, bool bNoRecipients, IMailUser *lpImailUser);
	virtual ~VConverter(void) = default;
	virtual HRESULT HrICal2MAPI(icalcomponent *lpEventRoot /* in */, icalcomponent *lpEvent /* in */, icalitem *lpPrevItem /* in */, icalitem **lppRet /* out */);
	virtual HRESULT HrMAPI2ICal(LPMESSAGE lpMessage /* in */, icalproperty_method *lpicMethod /* out */, std::list<icalcomponent*> *lpEventList /* out */);

protected:
	LPADRBOOK m_lpAdrBook;
	timezone_map *m_mapTimeZones;
	timezone_map_iterator m_iCurrentTimeZone;
	LPSPropTagArray m_lpNamedProps;
	std::string m_strCharset;
	IMailUser *m_lpMailUser;
	bool m_bCensorPrivate;
	bool m_bNoRecipients;

	ULONG m_ulUserStatus;

	convert_context m_converter;

	virtual HRESULT HrGetUID(icalcomponent *lpEvent, std::string *strUid);
	virtual HRESULT HrResolveUser(void *base, std::list<icalrecip> *lplstIcalRecip);
	virtual bool bIsUserLoggedIn(const std::wstring &strUser);

	/* ical -> mapi helper functions */
	virtual HRESULT HrCompareUids(icalitem *lpIcalItem, icalcomponent *lpicEvent);
	virtual HRESULT HrAddUids(icalcomponent *lpicEvent, icalitem *lpIcalItem);
	virtual HRESULT HrHandleExceptionGuid(icalcomponent *lpiEvent, void *base, SPropValue *lpsProp);
	virtual HRESULT HrAddRecurrenceID(icalcomponent *lpiEvent, icalitem *lpIcalItem);
	virtual HRESULT HrAddBaseProperties(icalproperty_method icMethod, icalcomponent *lpicEvent, void *base, bool bIsException, std::list<SPropValue> *lplstMsgProps) = 0; /* pure, must be overloaded */
	virtual HRESULT HrAddStaticProps(icalproperty_method icMethod, icalitem *lpIcalItem);
	virtual HRESULT HrAddSimpleHeaders(icalcomponent *lpicEvent, icalitem *lpIcalItem);
	virtual HRESULT HrAddBusyStatus(icalcomponent *lpicEvent, icalproperty_method icMethod, icalitem *lpIcalItem);
	virtual HRESULT HrAddXHeaders(icalcomponent *lpicEvent, icalitem *lpIcalItem);
	virtual HRESULT HrAddCategories(icalcomponent *lpicEvent, icalitem *lpIcalItem);
	virtual HRESULT HrAddTimes(icalproperty_method icMethod, icalcomponent *lpicEventRoot, icalcomponent *lpicEvent, bool bIsAllday, icalitem *lpIcalItem) = 0; /* pure, must be overloaded */
	virtual HRESULT HrAddOrganizer(icalitem *lpIcalItem, std::list<SPropValue> *lplstMsgProps, const std::wstring &strEmail, const std::wstring &strName, const std::string &strType, ULONG cbEntryID, LPENTRYID lpEntryID);
	virtual HRESULT HrAddRecipients(icalcomponent *lpicEvent, icalitem *lpIcalItem, std::list<SPropValue> *lplstMsgProps, std::list<icalrecip> * lplstIcalRecip);
	virtual HRESULT HrAddReplyRecipients(icalcomponent *lpicEvent, icalitem *lpIcalItem);
	virtual HRESULT HrAddReminder(icalcomponent *lpicEventRoot, icalcomponent *lpicEvent, icalitem *lpIcalItem);
	virtual HRESULT HrAddRecurrence(icalcomponent *lpicEventRoot, icalcomponent *lpicEvent, bool bIsAllday, icalitem *lpIcalItem);
	virtual HRESULT HrAddException(icalcomponent *lpEventRoot, icalcomponent *lpEvent, bool bIsAllday, icalitem *lpPrevItem);
	virtual HRESULT HrAddTimeZone(icalproperty *lpicProp, icalitem *lpIcalItem);
	virtual HRESULT HrRetrieveAlldayStatus(icalcomponent *lpicEvent, bool *blIsAllday);

	/* mapi -> ical helper functions */
	virtual HRESULT HrMAPI2ICal(LPMESSAGE lpMessage, icalproperty_method *lpicMethod, icaltimezone **lppicTZinfo, std::string *lpstrTZid, icalcomponent **lppEvent) = 0; /* pure */
	virtual HRESULT HrMAPI2ICal(LPMESSAGE lpMessage, icalproperty_method *lpicMethod, icaltimezone **lppicTZinfo, std::string *lpstrTZid, icalcomponent *lpEvent);
	virtual HRESULT HrFindTimezone(ULONG ulProps, LPSPropValue lpProps, std::string *lpstrTZid, TIMEZONE_STRUCT *lpTZinfo, icaltimezone **lppicTZinfo);
	virtual HRESULT HrSetTimeProperty(time_t tStamp, bool bDateOnly, icaltimezone *lpicTZinfo, const std::string &strTZid, icalproperty_kind icalkind, icalproperty *lpicProp);
	virtual HRESULT HrSetTimeProperty(time_t tStamp, bool bDateOnly, icaltimezone *lpicTZinfo, const std::string &strTZid, icalproperty_kind icalkind, icalcomponent *lpicEvent);
	virtual HRESULT HrSetOrganizerAndAttendees(LPMESSAGE lpParentMsg /* if exception*/, LPMESSAGE lpMessage, ULONG ulProps, LPSPropValue lpProps, icalproperty_method *lpicMethod, icalcomponent *lpicEvent);
	virtual HRESULT HrSetTimeProperties(LPSPropValue lpMsgProps, ULONG ulMsgProps, icaltimezone *lpicTZinfo, const std::string &strTZid, icalcomponent *lpEvent);
	virtual HRESULT HrSetICalAttendees(LPMESSAGE lpMessage, const std::wstring &strOrganizer, icalcomponent *lpicEvent);
	virtual HRESULT HrSetBusyStatus(LPMESSAGE lpMessage, ULONG ulBusyStatus, icalcomponent *lpicEvent);
	virtual HRESULT HrSetXHeaders(ULONG ulProps, LPSPropValue lpProps, LPMESSAGE lpMessage, icalcomponent *lpicEvent);
	virtual HRESULT HrSetVAlarm(ULONG ulProps, LPSPropValue lpProps, icalcomponent *lpicEvent);
	virtual HRESULT HrSetBody(LPMESSAGE lpMessage, icalproperty **lppicProp);
	virtual HRESULT HrSetItemSpecifics(ULONG ulProps, LPSPropValue lpProps, icalcomponent *lpicEvent);
	virtual HRESULT HrSetRecurrenceID(LPSPropValue lpMsgProps, ULONG ulMsgProps, icaltimezone *lpicTZinfo, const std::string &strTZid, icalcomponent *lpiEvent);
	/* recurrence + exceptions */
	virtual HRESULT HrSetRecurrence(LPMESSAGE lpMessage, icalcomponent *lpicEvent, icaltimezone *lpicTZinfo, const std::string &strTZid, std::list<icalcomponent*> *lpEventList);
	virtual HRESULT HrUpdateReminderTime(icalcomponent *lpicEvent, LONG lReminder);
	virtual HRESULT HrGetExceptionMessage(LPMESSAGE lpMessage, time_t tStart, LPMESSAGE *lppMessage);
	HRESULT resolve_organizer(std::wstring &email, std::wstring &name, std::string &type, unsigned int &cb, ENTRYID **entryid, bool force_mailuser = false);
};

extern HRESULT HrCopyString(convert_context &, const std::string &charset, void *base, const char *src, wchar_t **dst);
extern HRESULT HrCopyString(void *base, const wchar_t *src, wchar_t **dst);

} /* namespace */

#endif
