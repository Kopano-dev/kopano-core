"""
Part of the high-level python bindings for Kopano

Copyright 2017 - Kopano and its licensors (see LICENSE file for details)
"""

import datetime
import random
import struct
import sys
import time

import libfreebusy

from MAPI import (
    MAPI_UNICODE, MODRECIP_MODIFY, KEEP_OPEN_READWRITE, RELOP_EQ,
    MODRECIP_ADD, MAPI_TO, MAPI_BCC, SUPPRESS_RECEIPT, MSGFLAG_READ,
    MSGFLAG_UNSENT, MODRECIP_REMOVE, PT_BINARY
)

from MAPI.Tags import (
    PR_RECIPIENT_TRACKSTATUS, PR_MESSAGE_RECIPIENTS,
    PR_MESSAGE_CLASS_W, PR_ENTRYID, PR_SENT_REPRESENTING_ENTRYID, PR_ROWID,
    PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W, PR_SMTP_ADDRESS_W, PR_PROCESSED,
    IID_IMAPITable, IID_IMessage, PR_RECIPIENT_TRACKSTATUS_TIME,
    PR_RCVD_REPRESENTING_NAME_W, PR_DISPLAY_NAME_W, PR_RECIPIENT_ENTRYID,
    PR_RECIPIENT_TYPE, PR_SEND_INTERNET_ENCODING, PR_SEND_RICH_INFO,
    PR_RECIPIENT_DISPLAY_NAME, PR_DISPLAY_TYPE, PR_RECIPIENT_FLAGS,
    PR_OBJECT_TYPE, PR_SEARCH_KEY, PR_PROPOSED_NEWTIME, PR_OWNER_APPT_ID,
    PR_PROPOSED_NEWTIME_START, PR_PROPOSED_NEWTIME_END,
    PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W,
    PR_RECIPIENT_DISPLAY_NAME_W, PR_SENT_REPRESENTING_ADDRTYPE_W,
    PR_SENT_REPRESENTING_SEARCH_KEY, PR_ACCOUNT_W, PR_DISPLAY_TYPE_EX,
    PR_SUBJECT_W, PR_MESSAGE_FLAGS, PR_RESPONSE_REQUESTED,
    recipSendable, recipOrganizer, recipOriginal, respTentative, respAccepted,
    respDeclined,
)

from MAPI.Defs import (
    PpropFindProp,
)

from MAPI.Struct import (
    SPropValue, SPropertyRestriction, MAPIErrorUnknownEntryid,
)

from MAPI.Time import NANOSECS_BETWEEN_EPOCH

from .defs import (
    ASF_MEETING, ASF_RECEIVED, ASF_CANCELED
)

from .compat import repr as _repr
from .errors import Error
from .restriction import Restriction

if sys.hexversion >= 0x03000000:
    try:
        from . import utils as _utils
    except ImportError: # pragma: no cover
        _utils = sys.modules[__package__ + '.utils']

    from . import property_ as _prop
    from . import timezone as _timezone
else: # pragma: no cover
    import utils as _utils
    import property_ as _prop
    import timezone as _timezone

# XXX move all pidlids into separate definition file, plus short description of their meanings

from .pidlid import (
    PidLidReminderDelta, PidLidReminderSet, PidLidCommonStart, PidLidCommonEnd,
    PidLidUseTnef, PidLidAttendeeCriticalChange, PidLidWhere,
    PidLidGlobalObjectId, PidLidIsSilent, PidLidIsRecurring, PidLidIsException,
    PidLidStartRecurrenceDate, PidLidStartRecurrenceTime,
    PidLidEndRecurrenceDate, PidLidEndRecurrenceTime,
    PidLidOwnerCriticalChange, PidLidCleanGlobalObjectId,
    PidLidAppointmentMessageClass, PidLidMeetingType,
    PidLidAppointmentSequence, PidLidAppointmentSequenceTime,
    PidLidAppointmentLastSequence, PidLidBusyStatus, PidLidLocation,
    PidLidAppointmentStartWhole, PidLidAppointmentEndWhole,
    PidLidAppointmentRecur, PidLidAppointmentStateFlags, PidLidResponseStatus,
    PidLidAppointmentReplyTime, PidLidRecurring, PidLidIntendedBusyStatus,
    PidLidExceptionReplaceTime, PidLidFInvited, PidLidAppointmentReplyName,
    PidLidRecurrencePattern, PidLidTimeZoneStruct, PidLidTimeZoneDescription,
    PidLidClipStart, PidLidClipEnd, PidLidToAttendeesString,
    PidLidCcAttendeesString, PidLidAppointmentProposedStartWhole,
    PidLidAppointmentProposedEndWhole, PidLidAppointmentProposedDuration,
    PidLidAppointmentCounterProposal, PidLidSendAsIcal,
)

# all of the above # TODO redundant
PROPTAGS = [
    PidLidReminderDelta, PidLidReminderSet, PidLidCommonStart, PidLidCommonEnd,
    PidLidUseTnef, PidLidAttendeeCriticalChange, PidLidWhere,
    PidLidGlobalObjectId, PidLidIsSilent, PidLidIsRecurring, PidLidIsException,
    PidLidStartRecurrenceDate, PidLidStartRecurrenceTime,
    PidLidEndRecurrenceDate, PidLidEndRecurrenceTime,
    PidLidOwnerCriticalChange, PidLidCleanGlobalObjectId,
    PidLidAppointmentMessageClass, PidLidMeetingType,
    PidLidAppointmentSequence, PidLidAppointmentSequenceTime,
    PidLidAppointmentLastSequence, PidLidBusyStatus, PidLidLocation,
    PidLidAppointmentStartWhole, PidLidAppointmentEndWhole,
    PidLidAppointmentRecur, PidLidAppointmentStateFlags, PidLidResponseStatus,
    PidLidAppointmentReplyTime, PidLidRecurring, PidLidIntendedBusyStatus,
    PidLidExceptionReplaceTime, PidLidFInvited, PidLidAppointmentReplyName,
    PidLidRecurrencePattern, PidLidTimeZoneStruct, PidLidTimeZoneDescription,
    PidLidClipStart, PidLidClipEnd, PidLidToAttendeesString,
    PidLidCcAttendeesString, PidLidAppointmentProposedStartWhole,
    PidLidAppointmentProposedEndWhole, PidLidAppointmentProposedDuration,
    PidLidAppointmentCounterProposal, PidLidSendAsIcal,
]

RECIP_PROPS = [
    PR_ENTRYID,
    PR_DISPLAY_NAME_W,
    PR_EMAIL_ADDRESS_W,
    PR_RECIPIENT_ENTRYID,
    PR_RECIPIENT_TYPE,
    PR_SEND_INTERNET_ENCODING,
    PR_SEND_RICH_INFO,
    PR_RECIPIENT_DISPLAY_NAME, # XXX _W?
    PR_ADDRTYPE_W,
    PR_DISPLAY_TYPE,
    PR_RECIPIENT_TRACKSTATUS,
    PR_RECIPIENT_TRACKSTATUS_TIME,
    PR_RECIPIENT_FLAGS,
    PR_ROWID,
    PR_OBJECT_TYPE,
    PR_SEARCH_KEY,
]

def _organizer_props(cal_item, item):
    has_organizer = False

    table = cal_item.mapiobj.OpenProperty(PR_MESSAGE_RECIPIENTS, IID_IMAPITable, MAPI_UNICODE, 0)
    for row in table.QueryRows(-1, 0):
        recipient_flags = PpropFindProp(row, PR_RECIPIENT_FLAGS)
        if recipient_flags and recipient_flags.Value == (recipOrganizer | recipSendable): # XXX
            has_organizer = True
            break

    if not has_organizer:
        orgprops = [
            SPropValue(PR_ENTRYID, item.prop(PR_SENT_REPRESENTING_ENTRYID).value),
            SPropValue(PR_DISPLAY_NAME_W, item.prop(PR_SENT_REPRESENTING_NAME_W).value),
            SPropValue(PR_EMAIL_ADDRESS_W, item.prop(PR_SENT_REPRESENTING_EMAIL_ADDRESS_W).value),
            SPropValue(PR_RECIPIENT_TYPE, MAPI_TO),
            SPropValue(PR_RECIPIENT_DISPLAY_NAME_W, item.prop(PR_SENT_REPRESENTING_NAME_W).value),
            SPropValue(PR_ADDRTYPE_W, item.prop(PR_SENT_REPRESENTING_ADDRTYPE_W).value), # XXX php
            SPropValue(PR_RECIPIENT_TRACKSTATUS, 0),
            SPropValue(PR_RECIPIENT_FLAGS, (recipOrganizer | recipSendable)),
        ]
        repr_search_key = item.get(PR_SENT_REPRESENTING_SEARCH_KEY) # TODO not in exception message?
        if repr_search_key:
            orgprops.append(SPropValue(PR_SEARCH_KEY, repr_search_key))
        return orgprops

def _copytags(mapiobj):
    copytags = [_prop._name_to_proptag(tag, mapiobj)[0] for tag in PROPTAGS]
    copytags.extend([
        PR_SUBJECT_W,
        PR_SENT_REPRESENTING_NAME_W,
        PR_SENT_REPRESENTING_ADDRTYPE_W,
        PR_SENT_REPRESENTING_EMAIL_ADDRESS_W,
        PR_SENT_REPRESENTING_ENTRYID,
        PR_SENT_REPRESENTING_SEARCH_KEY,
        PR_RCVD_REPRESENTING_NAME_W,
    ])
    return copytags

# TODO: in MAPI.Time?
def mapi_time(t):
    return int(t) * 10000000 + NANOSECS_BETWEEN_EPOCH

def _generate_goid():
    """
    Generate a meeting request Global Object ID.

    The Global Object ID is a MAPI property that any MAPI client uses to
    correlate meeting updates and responses with a particular meeting on
    the calendar. The Global Object ID is the same across all copies of the
    item.

    The Global Object ID consists of the following data:

    * byte array id (16 bytes) - identifiers the BLOB as a GLOBAL Object ID.
    * year (YH + YL) - original year of the instance represented by the
    exception. The value is in big-endian format.
    * M (byte) - original month of the instance represented by the exception.
    * D (byte) - original day of the instance represented by the exception.
    * Creation time - 8 byte date
    * X - reversed byte array of size 8.
    * size - LONG, the length of the data field.
    * data - a byte array (16 bytes) that uniquely identifers the meeting object.
    """

    # byte array id
    goid = b'\x04\x00\x00\x00\x82\x00\xe0\x00t\xc5\xb7\x10\x1a\x82\xe0\x08'
    # YEARHIGH, YEARLOW, MONTH, DATE
    goid += struct.pack('>H2B', 0, 0, 0)
    # Creation time, lowdatetime, highdatetime
    now = mapi_time(time.time())
    goid += struct.pack('II', now & 0xffffffff, now >> 32)
    # Reserved, 8 zeros
    goid += struct.pack('L', 0)
    # data size
    goid += struct.pack('I', 16)
    # Unique data
    for _ in range(0, 16):
        goid += struct.pack('B', random.getrandbits(8))
    return goid

def _create_meetingrequest(item, cancel=False):
    # TODO Update the calendar item, for tracking status
    # TODO Set the body of the message like WebApp / OL does.
    # TODO Whitelist properties?

    item2 = item.copy(item.store.outbox)

    # Set meeting request props
    item2.message_class = 'IPM.Schedule.Meeting.Request'
    stateflags = ASF_MEETING | ASF_RECEIVED
    if cancel:
        stateflags |= ASF_CANCELED
    item2[PidLidAppointmentStateFlags] = stateflags

    goid = item.get(PidLidCleanGlobalObjectId)
    if goid is None:
        goid = _generate_goid()

        item2.create_prop(PidLidCleanGlobalObjectId, goid, PT_BINARY)
        item2.create_prop(PidLidGlobalObjectId, goid, PT_BINARY) # TODO add basedate

        # update appointment
        item[PidLidCleanGlobalObjectId] = goid

    return item2

class MeetingRequest(object):
    """MeetingRequest class"""

    def __init__(self, item):
        self.item = item

    @property
    def calendar(self):
        """ Respective :class:`calendar <Folder>` (possibly in delegator store) """

        delegator = self.item.get_prop(PR_RCVD_REPRESENTING_NAME_W)
        if delegator:
            store = self.item.server.user(delegator.value).store
        else:
            store = self.item.store # XXX pass to __init__

        return store.calendar

    @property
    def calendar_item(self):
        """ Global calendar item :class:`item <Item>` (possibly in delegator store) """

        goid = self.item.get_prop(PidLidCleanGlobalObjectId)
        if goid is not None:
            restriction = Restriction(SPropertyRestriction(
                RELOP_EQ, goid.proptag, SPropValue(goid.proptag, goid.mapiobj.Value))
            )
            return next(self.calendar.items(restriction), None)

    @property
    def basedate(self):
        """ Exception date """

        blob = self.item.get(PidLidGlobalObjectId)
        if blob is not None:
            y, m, d = struct.unpack_from('>HBB', blob, 16)
            if (y, m, d) != (0, 0, 0):
                return _timezone._to_utc(datetime.datetime(y, m, d), self.item.tzinfo)

    @property
    def update_counter(self):
        """ Update counter """

        return self.item.get(PidLidAppointmentSequence)

    @property
    def track_status(self):
        """ Track status """

        return {
            'IPM.Schedule.Meeting.Resp.Tent': respTentative,
            'IPM.Schedule.Meeting.Resp.Pos': respAccepted,
            'IPM.Schedule.Meeting.Resp.Neg': respDeclined,
        }.get(self.item.message_class)

    @property
    def processed(self):
        """ Has the request/response been processed """
        return self.item.get(PR_PROCESSED, False)

    @processed.setter
    def processed(self, value):
        self.item[PR_PROCESSED] = value

    def _check_processed(self):
        if self.processed:
            return True
        else:
            self.processed = True

    @property
    def response_requested(self):
        """ Is a response requested """
        return self.item.get(PR_RESPONSE_REQUESTED, False)

    @property
    def is_request(self):
        """ Is it an actual request """
        return self.item.message_class == 'IPM.Schedule.Meeting.Request'

    @property
    def is_response(self):
        """ Is it a response """
        return self.item.message_class.startswith('IPM.Schedule.Meeting.Resp.')

    @property
    def is_cancellation(self):
        """ Is it a cancellation """
        return self.item.message_class == 'IPM.Schedule.Meeting.Canceled'

    def _respond(self, subject_prefix, message_class, message=None):
        response = self.item.copy(self.item.store.outbox)
        response.message_class = message_class

        response.subject = subject_prefix + ': ' + self.item.subject
        if message:
            response.text = message
        response.to = self.item.server.user(email=self.item.from_.email) # XXX
        response.from_ = self.item.store.user # XXX slow?

        response.send()

    def _init_calitem(self, cal_item, tentative, merge=False): # XXX what about updating existing
        cal_item.message_class = 'IPM.Appointment'

        # busystatus
        intended_busystatus = self.item.get(PidLidIntendedBusyStatus)
        if intended_busystatus is not None:
            if tentative and intended_busystatus != libfreebusy.fbFree: # XXX
                busystatus = libfreebusy.fbTentative
            else:
                busystatus = intended_busystatus
            cal_item[PidLidBusyStatus] = busystatus

        # add organizer as recipient
        organizer_props = _organizer_props(cal_item, self.item)
        if organizer_props and not merge:
            table = cal_item.mapiobj.OpenProperty(PR_MESSAGE_RECIPIENTS, IID_IMAPITable, MAPI_UNICODE, 0)
            table.SetColumns(RECIP_PROPS, 0)
            rows = table.QueryRows(-1, 0)
            if tentative: # XXX php-compat: php checks 'move' flag, should we
                cal_item.mapiobj.ModifyRecipients(MODRECIP_REMOVE, rows)
                cal_item.mapiobj.ModifyRecipients(MODRECIP_ADD, [organizer_props] + rows)
            else:
                cal_item.mapiobj.ModifyRecipients(MODRECIP_ADD, [organizer_props])
            _utils._save(cal_item.mapiobj)

    def accept(self, tentative=False, response=True, add_bcc=False):
        """ Accept meeting request

        :param tentative: accept tentatively?
        :param response: send response message?
        """

        if not self.is_request:
            raise Error('item is not a meeting request')
        if self._check_processed():
            return

        calendar = self.calendar
        cal_item = self.calendar_item
        basedate = self.basedate

        self.item.mapiobj.SetReadFlag(SUPPRESS_RECEIPT)
        merge = False

        if basedate:
            # update existing recurrence
            if cal_item and cal_item.recurring:
                recurrence = cal_item.recurrence
                if recurrence._is_exception(basedate):
                    recurrence._modify_exception(basedate, self.item)
                else:
                    recurrence._create_exception(basedate, self.item)

            # otherwise replace calendar item
            else:
                if cal_item:
                    calendar.delete(cal_item)
                cal_item = self.item.copy(calendar)
                self._init_calitem(cal_item, tentative)

        else:
            # preserve categories # TODO preserve everything unrelated?
            categories = cal_item.categories if cal_item else []

            # remove existing recurrence
            if cal_item and cal_item.recurring:
                mr_counter = self.update_counter
                cal_counter = cal_item.meetingrequest.update_counter
                if mr_counter is not None and cal_counter is not None and \
                   mr_counter <= cal_counter:
                    raise Error('trying to accept out-of-date meeting request')
                calendar.delete(cal_item)

            # determine existing exceptions
            goid = self.item.get_prop(PidLidCleanGlobalObjectId)
            if goid is not None:
                restriction = Restriction(SPropertyRestriction(
                    RELOP_EQ, goid.proptag, SPropValue(goid.proptag, goid.mapiobj.Value))
                )
                existing_items = list(calendar.items(restriction))
                existing_items.sort(key=lambda i: i.get(PidLidAppointmentStartWhole)) # XXX check php
            else:
                existing_items = []

            # create new recurrence
            cal_item = self.item.copy(calendar)
            if categories:
                cal_item.categories = categories

            # merge existing exceptions
            if cal_item.recurring and existing_items:
                merge = True
                rec = cal_item.recurrence
                for item in existing_items:
                    rec._create_exception(item.meetingrequest.basedate, item, merge=True)

            calendar.delete(existing_items)

            self._init_calitem(cal_item, tentative, merge)

            # XXX why filter recipients?
            if merge or not add_bcc:
                table = cal_item.mapiobj.OpenProperty(PR_MESSAGE_RECIPIENTS, IID_IMAPITable, MAPI_UNICODE, 0)
                table.SetColumns(RECIP_PROPS, 0)
                rows = table.QueryRows(-1, 0)
                cal_item.mapiobj.ModifyRecipients(MODRECIP_MODIFY, rows)
                _utils._save(cal_item.mapiobj)

        # add self as BCC for ZCP-9901 XXX still relevant?
        proptags = [
            PR_ACCOUNT_W,
            PR_ADDRTYPE_W,
            PR_DISPLAY_NAME_W,
            PR_DISPLAY_TYPE,
            PR_DISPLAY_TYPE_EX,
            PR_EMAIL_ADDRESS_W,
            PR_ENTRYID,
            PR_OBJECT_TYPE,
            PR_SEARCH_KEY,
            PR_SMTP_ADDRESS_W,
        ]
        user = self.item.store.user
        props = user.mapiobj.GetProps(proptags, 0)
        props.extend([
            SPropValue(PR_RECIPIENT_ENTRYID, props[6].Value),
            SPropValue(PR_RECIPIENT_FLAGS, (recipOriginal | recipSendable)),
            SPropValue(PR_RECIPIENT_TRACKSTATUS, 0),
            SPropValue(PR_RECIPIENT_TYPE, MAPI_BCC),
        ])
        if add_bcc and not merge:
            cal_item.mapiobj.ModifyRecipients(MODRECIP_ADD, [props])
            _utils._save(cal_item.mapiobj)

        # send response
        if response:
            if tentative:
                message_class = 'IPM.Schedule.Meeting.Resp.Tent'
            else:
                message_class = 'IPM.Schedule.Meeting.Resp.Pos'
            self._respond('Accepted', message_class)

    def decline(self, message=None, response=True):
        """ Decline meeting request

        :param response: send response message?
        """

        message_class = 'IPM.Schedule.Meeting.Resp.Neg'

        if response:
            self._respond('Declined', message_class, message)

    def process_cancellation(self, delete=False):
        """ Process meeting request cancellation

        :param delete: delete appointment from calendar
        """

        if not self.is_cancellation:
            raise Error('item is not a meeting request cancellation')

        cal_item = self.calendar_item
        if not cal_item:
            return

        basedate = self.basedate

        if basedate:
            if cal_item.recurring:
                recurrence = cal_item.recurrence
                copytags = _copytags(cal_item.mapiobj)

                if delete:
                    recurrence._delete_exception(basedate, self.item, copytags)
                else:
                    if recurrence._is_exception(basedate):
                        recurrence._modify_exception(basedate, self.item, copytags)
                    else:
                        recurrence._create_exception(basedate, self.item, copytags)

                    message = recurrence._exception_message(basedate)
                    message[PidLidBusyStatus] = libfreebusy.fbFree
                    message[PR_MESSAGE_FLAGS] = MSGFLAG_UNSENT | MSGFLAG_READ

                    _utils._save(message._attobj)

        else:
            if delete:
                self.calendar.delete(cal_item)
            else:
                self.item.mapiobj.CopyTo([], [], 0, None, IID_IMessage, cal_item.mapiobj, 0)
                cal_item.mapiobj.SetProps([SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Appointment')])

        if cal_item:
            _utils._save(cal_item.mapiobj)

    def process_response(self):
        """ Process meeting request response """

        if not self.is_response:
            raise Error('item is not a meeting request response')
        if self._check_processed():
            return

        cal_item = self.calendar_item
        basedate = self.basedate

        # modify calendar item or embedded message (in case of exception)
        attach = None
        message = None
        if basedate:
            if cal_item.recurring:
                recurrence = cal_item.recurrence

                recurrence._update_calitem() # XXX via create/modify exception

                if recurrence._is_exception(basedate):
                    message = recurrence._exception_message(basedate)

                    owner_appt_id = self.item.get(PR_OWNER_APPT_ID)
                    if owner_appt_id is not None:
                        message[PR_OWNER_APPT_ID] = owner_appt_id
                    attach = message._attobj
        else:
            message = cal_item
        if not message:
            return

        # update recipient track status
        table = message.mapiobj.OpenProperty(PR_MESSAGE_RECIPIENTS, IID_IMAPITable, MAPI_UNICODE, 0)
        table.SetColumns(RECIP_PROPS, 0) # XXX things seem to get lost without this

        rows = table.QueryRows(-1, 0)
        for row in rows:
            if self._compare_ab_entryids(PpropFindProp(row, PR_ENTRYID).Value,
                                         self.item.prop(PR_SENT_REPRESENTING_ENTRYID).value):
                trackstatus = PpropFindProp(row, PR_RECIPIENT_TRACKSTATUS)
                trackstatus_time = PpropFindProp(row, PR_RECIPIENT_TRACKSTATUS_TIME)

                attendee_crit_change = self.item.get_prop(PidLidAttendeeCriticalChange)
                if trackstatus_time and attendee_crit_change and \
                   attendee_crit_change.mapiobj.Value <= trackstatus_time.Value:
                    continue

                if trackstatus_time:
                    trackstatus_time.Value = attendee_crit_change.mapiobj.Value
                else:
                    row.append(SPropValue(PR_RECIPIENT_TRACKSTATUS_TIME,
                                          attendee_crit_change.mapiobj.Value))

                if trackstatus:
                    trackstatus.Value = self.track_status
                else:
                    row.append(SPropValue(PR_RECIPIENT_TRACKSTATUS, self.track_status))

                if self.item.get(PidLidAppointmentCounterProposal) is True:
                    row.extend([
                        SPropValue(PR_PROPOSED_NEWTIME, True),
                        SPropValue(PR_PROPOSED_NEWTIME_START, self.item.prop(PidLidAppointmentProposedStartWhole).mapiobj.Value),
                        SPropValue(PR_PROPOSED_NEWTIME_END, self.item.prop(PidLidAppointmentProposedEndWhole).mapiobj.Value),
                    ])

        message.mapiobj.ModifyRecipients(MODRECIP_MODIFY, rows)

        # counter proposal
        if self.item.get(PidLidAppointmentCounterProposal):
            message[PidLidAppointmentCounterProposal] = True

        # save all the things
        _utils._save(message.mapiobj)

        if attach:
            _utils._save(attach)
            _utils._save(cal_item.mapiobj)

    def _compare_ab_entryids(self, entryid1, entryid2): # XXX shorten?
        smtp1 = self._get_smtp_address(entryid1)
        smtp2 = self._get_smtp_address(entryid2)
        return smtp1 == smtp2

    def _get_smtp_address(self, entryid):
        ab = self.item.server.ab
        try:
            abitem = ab.OpenEntry(entryid, None, 0)
        except MAPIErrorUnknownEntryid:
            return u''

        props = abitem.GetProps([PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W, PR_SMTP_ADDRESS_W], 0)
        if props[0].Value == u'SMTP':
            return props[1].Value
        else:
            return props[2].Value

    def __unicode__(self):
        return u'MeetingRequest()'

    def __repr__(self):
        return _repr(self)
