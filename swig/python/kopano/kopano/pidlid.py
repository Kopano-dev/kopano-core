"""
Part of the high-level python bindings for Kopano

Copyright 2018 - Kopano and its licensors (see LICENSE file for details)
"""

#TODO PT_STRING8 -> PT_UNICODE

PidLidEmail1AddressType = 'PT_UNICODE:PSETID_Address:0x8082'
PidLidEmail1DisplayName = 'PT_UNICODE:PSETID_Address:0x8080'
PidLidEmail1EmailAddress = 'PT_UNICODE:PSETID_Address:0x8083'
PidLidEmail1OriginalEntryId = 'PT_BINARY:PSETID_Address:0x8085'
PidLidEmail2AddressType = 'PT_UNICODE:PSETID_Address:0x8092'
PidLidEmail2DisplayName = 'PT_UNICODE:PSETID_Address:0x8090'
PidLidEmail2EmailAddress = 'PT_UNICODE:PSETID_Address:0x8093'
PidLidEmail2OriginalEntryId = 'PT_BINARY:PSETID_Address:0x8095'
PidLidEmail3AddressType = 'PT_UNICODE:PSETID_Address:0x80A2'
PidLidEmail3DisplayName = 'PT_UNICODE:PSETID_Address:0x80A0'
PidLidEmail3EmailAddress = 'PT_UNICODE:PSETID_Address:0x80A3'
PidLidEmail3OriginalEntryId = 'PT_BINARY:PSETID_Address:0x80A5'
#PidLidEmail1OriginalDisplayName = 'PT_UNICODE:PSETID_Address:0x8084'
PidLidReminderDelta = "PT_LONG:PSETID_Common:0x8501"
PidLidReminderSet = "PT_BOOLEAN:PSETID_Common:0x8503"
PidLidCommonStart = "PT_SYSTIME:PSETID_Common:0x8516"
PidLidCommonEnd ="PT_SYSTIME:PSETID_Common:0x8517"
PidLidUseTnef = "PT_BOOLEAN:PSETID_Common:0x8582"
PidLidAttendeeCriticalChange = "PT_SYSTIME:PSETID_Meeting:0x1"
PidLidWhere = "PT_STRING8:PSETID_Meeting:0x2"
PidLidGlobalObjectId = "PT_BINARY:PSETID_Meeting:0x3"
PidLidIsSilent = "PT_LONG:PSETID_Meeting:0x4"
PidLidIsRecurring = "PT_BOOLEAN:PSETID_Meeting:0x5"
PidLidIsException = "PT_BOOLEAN:PSETID_Meeting:0xA"
PidLidStartRecurrenceDate = "PT_LONG:PSETID_Meeting:0xD"
PidLidStartRecurrenceTime = "PT_LONG:PSETID_Meeting:0xE"
PidLidEndRecurrenceDate = "PT_LONG:PSETID_Meeting:0xF"
PidLidEndRecurrenceTime = "PT_LONG:PSETID_Meeting:0x10"
PidLidOwnerCriticalChange = "PT_SYSTIME:PSETID_Meeting:0x1A"
PidLidCleanGlobalObjectId = "PT_BINARY:PSETID_Meeting:0x23"
PidLidAppointmentMessageClass = "PT_STRING8:PSETID_Meeting:0x24"
PidLidMeetingType = "PT_LONG:PSETID_Meeting:0x26"
PidLidAppointmentSequence = "PT_LONG:PSETID_Appointment:0x8201"
PidLidAppointmentSequenceTime = "PT_LONG:PSETID_Appointment:0x8202"
PidLidAppointmentLastSequence = "PT_LONG:PSETID_Appointment:0x8203"
PidLidBusyStatus = "PT_LONG:PSETID_Appointment:0x8205"
PidLidLocation = "PT_UNICODE:PSETID_Appointment:0x8208"
PidLidAppointmentStartWhole = "PT_SYSTIME:PSETID_Appointment:0x820D"
PidLidAppointmentEndWhole = "PT_SYSTIME:PSETID_Appointment:0x820E"
PidLidAppointmentRecur = "PT_BINARY:PSETID_Appointment:0x8216"
PidLidAppointmentStateFlags = "PT_LONG:PSETID_Appointment:0x8217"
PidLidResponseStatus = "PT_LONG:PSETID_Appointment:0x8218"
PidLidSendAsIcal = "PT_BOOLEAN:PSETID_Appointment:0x8200" # XXX undocumented?
PidLidAppointmentReplyTime = "PT_SYSTIME:PSETID_Appointment:0x8220"
PidLidRecurring = "PT_BOOLEAN:PSETID_Appointment:0x8223"
PidLidIntendedBusyStatus = "PT_LONG:PSETID_Appointment:0x8224"
PidLidExceptionReplaceTime = "PT_SYSTIME:PSETID_Appointment:0x8228"
PidLidFInvited = "PT_BOOLEAN:PSETID_Appointment:0x8229"
PidLidAppointmentReplyName = "PT_STRING8:PSETID_Appointment:0x8230"
PidLidRecurrencePattern = "PT_STRING8:PSETID_Appointment:0x8232"
PidLidTimeZoneStruct = "PT_BINARY:PSETID_Appointment:0x8233"
PidLidTimeZoneDescription = "PT_STRING8:PSETID_Appointment:0x8234"
PidLidClipStart = "PT_SYSTIME:PSETID_Appointment:0x8235"
PidLidClipEnd = "PT_SYSTIME:PSETID_Appointment:0x8236"
PidLidToAttendeesString = "PT_STRING8:PSETID_Appointment:0x823B"
PidLidCcAttendeesString = "PT_STRING8:PSETID_Appointment:0x823C"
PidLidAppointmentProposedStartWhole = "PT_SYSTIME:PSETID_Appointment:0x8250"
PidLidAppointmentProposedEndWhole = "PT_SYSTIME:PSETID_Appointment:0x8251"
PidLidAppointmentProposedDuration = "PT_LONG:PSETID_Appointment:0x8256"
PidLidAppointmentCounterProposal = "PT_BOOLEAN:PSETID_Appointment:0x8257"
PidLidSideEffects = "PT_LONG:common:0x8510"
PidLidSmartNoAttach = "PT_BOOLEAN:common:0x8514"
PidLidReminderSignalTime = "PT_SYSTIME:common:0x8560"
PidLidAppointmentSubType = "PT_BOOLEAN:PSETID_Appointment:0x8215"
PidLidAppointmentColor = "PT_LONG:PSETID_Appointment:0x8214"
PidLidYomiFirstName = 'PT_UNICODE:PSETID_Address:0x802C'
PidLidYomiLastName = 'PT_UNICODE:PSETID_Address:0x802D'
PidLidYomiCompanyName = 'PT_UNICODE:PSETID_Address:0x802E'
PidLidFileUnder = 'PT_UNICODE:PSETID_Address:0x8005'
PidLidInstantMessagingAddress = 'PT_UNICODE:PSETID_Address:0x8062'
PidLidWorkAddressStreet = 'PT_UNICODE:PSETID_Address:0x8045'
PidLidWorkAddressCity = 'PT_UNICODE:PSETID_Address:0x8046'
PidLidWorkAddressState = 'PT_UNICODE:PSETID_Address:0x8047'
PidLidWorkAddressPostalCode = 'PT_UNICODE:PSETID_Address:0x8048'
PidLidWorkAddressCountry = 'PT_UNICODE:PSETID_Address:0x8049'
