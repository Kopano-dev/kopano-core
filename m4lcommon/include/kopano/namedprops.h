/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef NAMEDPROPS_H
#define NAMEDPROPS_H

//MNID_ID properties: 
// In PSETID_Address
#define dispidFileAs						0x8005	//PT_STRING8
#define dispidFileAsSelection				0x8006	//PT_LONG
#define dispidHomeAddress					0x801A	//PT_STRING8
#define dispidBusinessAddress				0x801B	//PT_STRING8
#define dispidOtherAddress					0x801C	//PT_STRING8
#define dispidSelectedMailingAddress		0x8022	//PT_LONG
#define dispidJournal						0x8025	//PT_BOOLEAN
#define dispidABPEmailList					0x8028	//PT_MV_LONG
#define dispidABPArrayType					0x8029	//PT_LONG
#define dispidWebPage						0x802B	//PT_STRING8
#define dispidWorkAddressStreet				0x8045	//PT_STRING8
#define dispidWorkAddressCity				0x8046	//PT_STRING8
#define dispidWorkAddressState				0x8047	//PT_STRING8
#define dispidWorkAddressPostalCode			0x8048	//PT_STRING8
#define dispidWorkAddressCountry			0x8049	//PT_STRING8
#define dispidCheckSum						0x804C	//PT_LONG
#define dispidDLName						0x8053	//PT_STRING8
#define dispidOneOffMembers					0x8054	//PT_MV_BINARY
#define dispidMembers						0x8055	//PT_MV_BINARY
#define dispidInstMsg						0x8062	//PT_STRING8
#define dispidEmail1DisplayName				0x8080	//PT_STRING8
#define dispidEmail1AddressType				0x8082	//PT_STRING8
#define dispidEmail1Address					0x8083	//PT_STRING8
#define dispidEmail1OriginalDisplayName		0x8084	//PT_STRING8
#define dispidEmail1OriginalEntryID			0x8085	//PT_BINARY
#define dispidEmail2DisplayName				0x8090	//PT_STRING8
#define dispidEmail2AddressType				0x8092	//PT_STRING8
#define dispidEmail2Address					0x8093	//PT_STRING8
#define dispidEmail2OriginalDisplayName		0x8094	//PT_STRING8
#define dispidEmail2OriginalEntryID			0x8095	//PT_BINARY
#define dispidEmail3DisplayName				0x80A0	//PT_STRING8
#define dispidEmail3AddressType				0x80A2	//PT_STRING8
#define dispidEmail3Address					0x80A3	//PT_STRING8
#define dispidEmail3OriginalDisplayName		0x80A4	//PT_STRING8
#define dispidEmail3OriginalEntryID			0x80A5	//PT_BINARY
#define dispidFax1DisplayName				0x80B0	//PT_STRING8
#define dispidFax1AddressType				0x80B2	//PT_STRING8
#define dispidFax1Address					0x80B3	//PT_STRING8
#define dispidFax1OriginalDisplayName		0x80B4	//PT_STRING8
#define dispidFax1OriginalEntryID			0x80B5	//PT_BINARY
#define dispidFax2DisplayName				0x80C0	//PT_STRING8
#define dispidFax2AddressType				0x80C2	//PT_STRING8
#define dispidFax2Address					0x80C3	//PT_STRING8
#define dispidFax2OriginalDisplayName		0x80C4	//PT_STRING8
#define dispidFax2OriginalEntryID			0x80C5	//PT_BINARY
#define dispidFax3DisplayName				0x80D0	//PT_STRING8
#define dispidFax3AddressType				0x80D2	//PT_STRING8
#define dispidFax3Address					0x80D3	//PT_STRING8
#define dispidFax3OriginalDisplayName		0x80D4	//PT_STRING8
#define dispidFax3OriginalEntryID			0x80D5	//PT_BINARY
#define dispidInternetFreeBusyAddress		0x80D8	//PT_STRING8

// In PSETID_Task
#define dispidTaskStatus					0x8101	//PT_LONG
#define dispidTaskPercentComplete			0x8102	//PT_DOUBLE
#define dispidTaskTeamTask					0x8103	//PT_BOOLEAN
#define dispidTaskStartDate					0x8104	//PT_SYSTIME
#define dispidTaskDueDate					0x8105	//PT_SYSTIME
#define dispidTaskDateCompleted				0x810f	//PT_SYSTIME
#define dispidTaskActualEffort				0x8110	//PT_LONG
#define dispidTaskEstimatedEffort			0x8111	//PT_LONG
#define dispidTaskState						0x8113	//PT_LONG
#define dispidTaskRecurrenceState			0x8116	//PT_BINARY
#define dispidTaskComplete					0x811c	//PT_BOOLEAN
#define dispidTaskOwner						0x811f	//PT_STRING8
#define dispidTaskDelegator					0x8121	//PT_STRING8
#define dispidTaskOrdinal					0x8123	//PT_LONG
#define dispidTaskIsRecurring				0x8126	//PT_BOOLEAN	
#define dispidTaskRole						0x8127	//PT_STRING8
#define dispidTaskOwnership					0x8129	//PT_LONG
#define dispidTaskDelegationState			0x812A	//PT_LONG

// In PSETID_Appointment
#define dispidSendAsICAL					0x8200	//PT_BOOLEAN
#define dispidAppointmentSequenceNumber		0x8201	//PT_LONG
#define dispidApptSeqTime					0x8202	//PT_SYSTIME
#define dispidBusyStatus					0x8205	//PT_LONG
#define dispidApptAuxFlags					0x8207	//PT_LONG
#define dispidLocation						0x8208	//PT_STRING8
#define dispidApptStartWhole				0x820D	//PT_SYSTIME
#define dispidApptEndWhole					0x820E	//PT_SYSTIME
#define dispidApptDuration					0x8213	//PT_LONG
#define dispidLabel							0x8214	//PT_LONG
#define dispidAllDayEvent					0x8215	//PT_BOOLEAN - official name dispidApptSubType
#define dispidRecurrenceState				0x8216	//PT_BINARY
#define dispidAppointmentStateFlags			0x8217	//PT_LONG - aka PidLidAppointmentStateFlags
#define dispidResponseStatus				0x8218	//PT_LONG
#define dispidRecurring						0x8223	//PT_BOOLEAN
#define dispidIntendedBusyStatus			0x8224	//PT_LONG
#define dispidRecurringBase					0x8228	//PT_SYSTIME
#define dispidRequestSent					0x8229	//PT_BOOLEAN
#define dispidApptReplyName					0x8230	//PT_STRING8
#define dispidRecurrenceType				0x8231	//PT_LONG
#define dispidRecurrencePattern				0x8232	//PT_STRING8
#define dispidTimeZoneData					0x8233	//PT_BINARY - official name dispidTimeZoneStruct
#define dispidTimeZone						0x8234	//PT_STRING8
#define dispidClipStart						0x8235	//PT_SYSTIME
#define dispidClipEnd						0x8236	//PT_SYSTIME
#define dispidAllAttendeesString			0x8238	//PT_STRING8
#define dispidToAttendeesString				0x823B	//PT_STRING8
#define dispidCCAttendeesString				0x823C	//PT_STRING8
#define dispidNetMeetingType				0x8241	//PT_LONG
#define dispidNetMeetingServer				0x8242	//PT_STRING8
#define dispidNetMeetingOrganizerAlias		0x8243	//PT_STRING8
#define dispidNetMeetingAutoStart			0x8244	//PT_BOOLEAN
#define dispidAutoStartWhen					0x8245	//PT_LONG
#define dispidConferenceServerAllowExternal 0x8246	//PT_BOOLEAN
#define dispidNetMeetingDocPathName			0x8247	//PT_STRING8
#define dispidNetShowURL					0x8248	//PT_STRING8
#define dispidConferenceServerPassword		0x8249	//PT_STRING8
// A counter proposal is when the recipient of the request has proposed a new time for the meeting
#define dispidApptCounterProposal			0x8257	//PT_BOOLEAN
#define dispidApptProposedStartWhole		0x8250	//PT_SYSTIME
#define dispidApptProposedEndWhole			0x8251	//PT_SYSTIME
#define dispidApptReplyTime					0x8220	//PT_SYSTIME
#define dispidFExceptionalBody              0x8206  //PT_BOOLEAN
#define dispidFExceptionalAttendees         0x822B  //PT_BOOLEAN

#define dispidOrgMsgId						0x8251  //PT_BINARY
#define dispidZmtVersion					0x8252	//PT_STRING8

// New in OLK 2007
#define dispidApptTZDefStartDisplay			0x825E
#define dispidApptTZDefEndDisplay			0x825F
#define dispidApptTZDefRecur				0x8260

// In PSETID_Meeting
#define dispidMeetingLocation				0x0002	//PT_STRING8 - aka PidLidWhere
#define dispidGlobalObjectID				0x0003	//PT_BINARY
#define dispidIsRecurring					0x0005	//PT_BOOLEAN
#define dispidCleanGlobalObjectID			0x0023	//PT_BINARY
#define dispidMeetingMessageClass			0x0024	//PT_STRING8
#define dispidAttendeeCriticalChange		0x1		//PT_SYSTIME
#define dispidOwnerCriticalChange			0x1a	//PT_SYSTIME
#define dispidOldWhenStartWhole				0x0029	//PT_SYSTIME
#define dispidIsException					0x000A	//PT_BOOLEAN
#define dispidStartRecurrenceDate			0x000D	//PT_LONG
#define dispidStartRecurrenceTime			0x000E	//PT_LONG
#define dispidEndRecurrenceDate				0x000F	//PT_LONG
#define dispidEndRecurrenceTime				0x0010	//PT_LONG
#define dispidDayInterval					0x0011	//PT_I2
#define dispidWeekInterval					0x0012	//PT_I2
#define dispidMonthInterval					0x0013	//PT_I2
#define dispidYearInterval					0x0014	//PT_I2
#define dispidDayOfWeekMask					0x0015	//PT_LONG
#define dispidDayOfMonthMask				0x0016	//PT_LONG
#define dispidMonthOfYearMask				0x0017	//PT_LONG
#define dispidOldRecurrenceType				0x0018	//PT_I2
#define dispidDayOfWeekStart				0x0019	//PT_I2
#define dispidMeetingType                   0x0026  //PT_LONG - aka PidLidMeetingType

//In PSETID_Kopano_CalDav
#define dispidMozLastAck					0x0001	//PT_SYSTIME	X-MOZ-LAST-ACK 
#define dispidMozGen						0x0002	//PT_LONG		X-MOZ-GENERATION
#define dispidMozSnoozeSuffixTime			0x0003	//PT_SYSTIME	X-MOZ-SNOOZE-TIME suffix 
#define dispidMozSendInvite					0x0004	//PT_BOOLEAN	X-MOZ-SEND-INVITATIONS
#define dispidApptTsRef						0x0025	//PT_STRING8	Timestamp used as ID by Caldav
#define dispidFldID							0x0026	//PT_STRING8	FolderID used by Caldav

//In PSETID_KC (general KC properties)
#define dispidAutoProcess                   0x0001  //PT_LONG

// In PSETID_Common
#define dispidReminderMinutesBeforeStart	0x8501	//PT_LONG
#define dispidReminderTime					0x8502	//PT_SYSTIME
#define dispidReminderSet					0x8503	//PT_BOOLEAN
#define dispidPrivate						0x8506	//PT_BOOLEAN
#define dispidNoAging						0x850E	//PT_BOOLEAN
#define dispidFormStorage					0x850F
#define dispidSideEffect					0x8510	//PT_LONG
#define dispidRemoteStatus					0x8511	//PT_LONG
#define dispidPageDirStream					0x8513
#define dispidSmartNoAttach					0x8514	//PT_BOOLEAN
#define dispidCommonStart					0x8516	//PT_SYSTIME
#define dispidCommonEnd						0x8517	//PT_SYSTIME

//On Appointments needed for deleting an occurrence of a recurring item in outlook
//On Tasks is set to 1 on the send/assigned tasks, otherwise set to 0.
#define dispidCommonAssign					0x8518	//PT_LONG

#define dispidFormPropStream				0x851B
#define dispidRequest						0x8530
#define dispidCompanies						0x8539
#define dispidPropDefStream					0x8540
#define dispidScriptStream					0x8541
#define dispidCustomFlag					0x8542
#define dispidContacts						0x853A
#define dispidOutlookInternalVersion		0x8552	//PT_LONG
#define dispidOutlookVersion				0x8554	//PT_STRING8
#define dispidReminderNextTime				0x8560
#define dispidHeaderItem					0x8578
#define dispidInetAcctName					0x8580
#define dispidInetAcctStamp					0x8581

// In PSETID_Log (also known as Journal)
#define dispidLogType						0x8700
#define dispidLogStart						0x8706
#define dispidLogDuration					0x8707
#define dispidLogEnd						0x8708


// Values for dispidSideEffect, from [MS-OXCMSG].pdf, par 2.2.1.1.6
// Additional processing is required on the Message object when deleting.
#define seOpenToDelete		0x0001
// No UI is associated with the Message object.
#define seNoFrame			0x0008
// Additional processing is required on the Message object when moving
// or copying to a Folder object with a PidTagContainerClass of
// "IPF.Note". For more details about the PidTagContainerClass property,
// see [MS-OXOSFLD] section 2.2.5.
#define seCoerceToInbox		0x0010
// Additional processing is required on the Message object when
// copying to another folder.
#define seOpenToCopy		0x0020
// Additional processing is required on the Message object when moving
// to another folder.
#define seOpenToMove		0x0040
// Additional processing is required on the Message object when
// displaying verbs to the end-user.
#define seOpenForCtxMenu	0x0100
// Cannot undo delete operation, MUST NOT be set unless "0x0001" is set.
#define seCannotUndoDelete	0x0400
// Cannot undo copy operation, MUST NOT be set unless "0x0020" is set.
#define seCannotUndoCopy	0x0800
// Cannot undo move operation, MUST NOT be set unless "0x0040" is set.
#define seCannotUndoMove	0x1000
// The Message object contains end-user script.
#define seHasScript			0x2000
// Additional processing is required to permanently delete the Message object.
#define seOpenToPermDelete	0x4000


/**
 * Values for PidLidResponseStatus
 */
// No response is required for this object. This is the case for
// Appointment objects and Meeting Response objects.
#define respNone		0x00000000
// This Meeting object belongs to the organizer.
#define respOrganized	0x00000001
// This value on the attendee's Meeting object indicates that the
// attendee has tentatively accepted the Meeting Request object.
#define respTentative	0x00000002
// This value on the attendee's Meeting object indicates that the
// attendee has accepted the Meeting Request object.
#define respAccepted	0x00000003
// This value on the attendee's Meeting object indicates that the
// attendee has declined the Meeting Request object.
#define respDeclined	0x00000004
// This value on the attendee's Meeting object indicates that the
// attendee has not yet responded. This value is on the Meeting Request
// object, Meeting Update object, and Meeting Cancellation object.
#define respNotResponded 0x00000005


/* PS_EC_IMAP named PropTags */
#define dispidIMAPEnvelope		0x0001

#endif
