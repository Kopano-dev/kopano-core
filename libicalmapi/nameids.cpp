/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include "nameids.h"
#include <mapix.h>

namespace KC {

const wchar_t *nmStringNames[SIZE_NAMEDPROPS] = {
	L"Keywords", NULL
};

MAPINAMEID mnNamedProps[SIZE_NAMEDPROPS] = {
// lpwstrName:L"Keywords" may work, but gives a compile warning: ISO C++ does not allow designated initializers
	{const_cast<GUID *>(&PS_PUBLIC_STRINGS), MNID_STRING, {0}},
// ID names
#define PS const_cast<GUID *>(&PSETID_Meeting)
	{PS, MNID_ID, {dispidMeetingLocation}},
	{PS, MNID_ID, {dispidGlobalObjectID}},
	{PS, MNID_ID, {dispidIsRecurring}},
	{PS, MNID_ID, {dispidCleanGlobalObjectID}},
	{PS, MNID_ID, {dispidOwnerCriticalChange}},
	{PS, MNID_ID, {dispidAttendeeCriticalChange}},
	{PS, MNID_ID, {dispidOldWhenStartWhole}},
	{PS, MNID_ID, {dispidIsException}},
	{PS, MNID_ID, {dispidStartRecurrenceTime}},
	{PS, MNID_ID, {dispidEndRecurrenceTime}},
#undef PS
#define PS const_cast<GUID *>(&PSETID_Kopano_CalDav)
	{PS, MNID_ID, {dispidMozGen}}, /* X-MOZ-GENERATION */
	{PS, MNID_ID, {dispidMozLastAck}}, /* X-MOZ-LAST-ACK */
	{PS, MNID_ID, {dispidMozSnoozeSuffixTime}}, /* X-MOZ-SNOOZE-TIME suffix */
	{PS, MNID_ID, {dispidMozSendInvite}}, /* X-MOZ-SEND-INVITATIONS */
	{PS, MNID_ID, {dispidApptTsRef}},
	{PS, MNID_ID, {dispidFldID}},
#undef PS
#define PS const_cast<GUID *>(&PSETID_Appointment)
	{PS, MNID_ID, {dispidSendAsICAL}},
	{PS, MNID_ID, {dispidAppointmentSequenceNumber}},
	{PS, MNID_ID, {dispidApptSeqTime}},
	{PS, MNID_ID, {dispidBusyStatus}},
	{PS, MNID_ID, {dispidApptAuxFlags}},
	{PS, MNID_ID, {dispidLocation}},
	{PS, MNID_ID, {dispidLabel}},
	{PS, MNID_ID, {dispidApptStartWhole}},
	{PS, MNID_ID, {dispidApptEndWhole}},
	{PS, MNID_ID, {dispidApptDuration}},
	{PS, MNID_ID, {dispidAllDayEvent}},
	{PS, MNID_ID, {dispidRecurrenceState}},
	{PS, MNID_ID, {dispidAppointmentStateFlags}},
	{PS, MNID_ID, {dispidResponseStatus}},
	{PS, MNID_ID, {dispidRecurring}},
	{PS, MNID_ID, {dispidIntendedBusyStatus}},
	{PS, MNID_ID, {dispidRecurringBase}},
	{PS, MNID_ID, {dispidRequestSent}},		// aka PidLidFInvited
	{PS, MNID_ID, {dispidApptReplyName}},
	{PS, MNID_ID, {dispidRecurrenceType}},
	{PS, MNID_ID, {dispidRecurrencePattern}},
	{PS, MNID_ID, {dispidTimeZoneData}},
	{PS, MNID_ID, {dispidTimeZone}},
	{PS, MNID_ID, {dispidClipStart}},
	{PS, MNID_ID, {dispidClipEnd}},
	{PS, MNID_ID, {dispidAllAttendeesString}}, /* AllAttendees (exluding self, ';' separated) */
	{PS, MNID_ID, {dispidToAttendeesString}}, /* RequiredAttendees (including self) */
	{PS, MNID_ID, {dispidCCAttendeesString}}, /* OptionalAttendees */
	{PS, MNID_ID, {dispidNetMeetingType}},
	{PS, MNID_ID, {dispidNetMeetingServer}},
	{PS, MNID_ID, {dispidNetMeetingOrganizerAlias}},
	{PS, MNID_ID, {dispidNetMeetingAutoStart}},
	{PS, MNID_ID, {dispidAutoStartWhen}},
	{PS, MNID_ID, {dispidConferenceServerAllowExternal}},
	{PS, MNID_ID, {dispidNetMeetingDocPathName}},
	{PS, MNID_ID, {dispidNetShowURL}},
	{PS, MNID_ID, {dispidConferenceServerPassword}},
	{PS, MNID_ID, {dispidApptReplyTime}},
	{PS, MNID_ID, {dispidApptCounterProposal}},
	{PS, MNID_ID, {dispidApptProposedStartWhole}},
	{PS, MNID_ID, {dispidApptProposedEndWhole}},
#undef PS
#define PS const_cast<GUID *>(&PSETID_Common)
	{PS, MNID_ID, {dispidReminderMinutesBeforeStart}},
	{PS, MNID_ID, {dispidReminderTime}},
	{PS, MNID_ID, {dispidReminderSet}},
	{PS, MNID_ID, {dispidPrivate}},
	{PS, MNID_ID, {dispidNoAging}},
	{PS, MNID_ID, {dispidSideEffect}},
	{PS, MNID_ID, {dispidRemoteStatus}},
	{PS, MNID_ID, {dispidCommonStart}},
	{PS, MNID_ID, {dispidCommonEnd}},
	{PS, MNID_ID, {dispidCommonAssign}},
	{PS, MNID_ID, {dispidContacts}},
	{PS, MNID_ID, {dispidOutlookInternalVersion}},
	{PS, MNID_ID, {dispidOutlookVersion}},
	{PS, MNID_ID, {dispidReminderNextTime}},
	{PS, MNID_ID, {dispidSmartNoAttach}},	
#undef PS
#define PS const_cast<GUID *>(&PSETID_Task)
	{PS, MNID_ID, {dispidTaskStatus}},
	{PS, MNID_ID, {dispidTaskComplete}},
	{PS, MNID_ID, {dispidTaskPercentComplete}},
	{PS, MNID_ID, {dispidTaskStartDate}},
	{PS, MNID_ID, {dispidTaskDueDate}},
	{PS, MNID_ID, {dispidTaskRecurrenceState}},
	{PS, MNID_ID, {dispidTaskIsRecurring}},
	{PS, MNID_ID, {dispidTaskDateCompleted}}
#undef PS
};

/** 
 * Lookup all required named properties actual IDs for a given MAPI object.
 * 
 * @param[in]  lpPropObj Call GetIDsFromNames on this object.
 * @param[out] lppNamedProps Array of all named properties used in mapi ical conversions.
 * 
 * @return MAPI error code
 */
HRESULT HrLookupNames(IMAPIProp *lpPropObj, LPSPropTagArray *lppNamedProps)
{
	memory_ptr<MAPINAMEID *> lppNameIds;
	LPSPropTagArray lpNamedProps = NULL;

	auto hr = MAPIAllocateBuffer(sizeof(MAPINAMEID *) * SIZE_NAMEDPROPS, &~lppNameIds);
	if (hr != hrSuccess)
		return hr;

	for (int i = 0; i < SIZE_NAMEDPROPS; ++i) {
		hr = KAllocCopy(&mnNamedProps[i], sizeof(MAPINAMEID), reinterpret_cast<void **>(&lppNameIds[i]), lppNameIds);
		if (hr != hrSuccess)
			return hr;
		if (mnNamedProps[i].ulKind == MNID_STRING && nmStringNames[i])
			lppNameIds[i]->Kind.lpwstrName = const_cast<wchar_t *>(nmStringNames[i]);
	}

	hr = lpPropObj->GetIDsFromNames(SIZE_NAMEDPROPS, lppNameIds, MAPI_CREATE, &lpNamedProps);
	if (FAILED(hr))
		return hr;
	*lppNamedProps = lpNamedProps;
	return hrSuccess;
}

} /* namespace */
