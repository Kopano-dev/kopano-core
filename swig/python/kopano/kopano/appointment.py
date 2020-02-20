# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""
import sys

from datetime import datetime

from MAPI import (
    PT_SYSTIME, MODRECIP_ADD, PT_LONG, PT_UNICODE, BMR_EQZ
)

from MAPI.Tags import (
    PR_MESSAGE_RECIPIENTS, PR_RESPONSE_REQUESTED, PR_ENTRYID,
    PR_DISPLAY_NAME_W, PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W, PR_RECIPIENT_TYPE,
    PR_RECIPIENT_FLAGS, respOrganized, recipOrganizer
)

from MAPI.Struct import (SPropValue, SOrRestriction, SBitMaskRestriction,
                         SExistRestriction, SNotRestriction)

from .attendee import Attendee
from .errors import NotFoundError, ArgumentError
from .recurrence import Recurrence, Occurrence
from .restriction import Restriction

from .compat import (
    benc as _benc, bdec as _bdec,
)
from .defs import (
    ASF_CANCELED, NR_COLOR, COLOR_NR, FB_STATUS,
    STATUS_FB, ASF_MEETING, RESPONSE_STATUS, STATUS_RESPONSE,
)
from .pidlid import (
    PidLidReminderSet, PidLidReminderDelta, PidLidAppointmentSubType,
    PidLidBusyStatus, PidLidGlobalObjectId, PidLidRecurring,
    PidLidTimeZoneStruct, PidLidTimeZoneDescription, PidLidLocation,
    PidLidAppointmentStateFlags, PidLidAppointmentColor, PidLidResponseStatus,
    PidLidAppointmentStartWhole, PidLidAppointmentEndWhole, PidLidAppointmentReplyName,
    PidLidAppointmentReplyTime,
)

try:
    from . import utils as _utils
except ImportError: # pragma: no cover
    _utils = sys.modules[__package__+'.utils']

from . import timezone as _timezone


class Appointment(object):
    """Appointment mixin class

    Appointment-specific functionality, mixed into the :class:`Item`
    class.
    """

    @property
    def all_day(self):
        """Appointment is all-day."""
        proptag = self.store._pidlid_proptag(PidLidAppointmentSubType)
        return self._get_fast(proptag)

    @all_day.setter
    def all_day(self, value):
        self[PidLidAppointmentSubType] = value

    @property
    def busy_status(self): # TODO setter
        """Appointment busy status (free, tentative, busy, out_of_office,
        working_elsewhere or unknown)"""
        try:
            return {
                0: 'free',
                1: 'tentative',
                2: 'busy',
                3: 'out_of_office',
                4: 'working_elsewhere'
            }[self[PidLidBusyStatus]]
        except NotFoundError:
            return 'unknown'

    @property
    def show_as(self): # TODO deprecate?
        return self.busy_status

    @property
    def start(self):
        """Appointment start."""
        return self._get_fast(self.store._pidlid_proptag(PidLidAppointmentStartWhole))

    @start.setter
    def start(self, val): # TODO update/invalidate cache
        # TODO check if exists?
        self.create_prop('common:34070', val, PT_SYSTIME) # props identical
        self.create_prop('appointment:33293', val, PT_SYSTIME)
        if self.recurring:
            self.recurrence._update_offsets()

    @property
    def end(self):
        """Appointment end."""
        return self._get_fast(self.store._pidlid_proptag(PidLidAppointmentEndWhole))

    @end.setter
    def end(self, val): # TODO update/invalidate cache
        # TODO check if exists?
        self.create_prop('common:34071', val, PT_SYSTIME) # props identical
        self.create_prop('appointment:33294', val, PT_SYSTIME)
        if self.recurring:
            self.recurrence._update_offsets()

    @property
    def location(self):
        """Appointment location."""
        return self.get(PidLidLocation)

    @location.setter
    def location(self, value):
        self[PidLidLocation] = value

    @property
    def recurring(self):
        """Appointment is recurring."""
        return self._get_fast(self.store._pidlid_proptag(PidLidRecurring))

    @recurring.setter
    def recurring(self, value): # TODO update/invalidate cache
        # TODO cleanup on False?
        if value and not self.recurring:
            Recurrence._init(self)
        self[PidLidRecurring] = value

    @property
    def busystatus(self): # TODO deprecate and use busy_status instead?
        proptag = self.store._pidlid_proptag(PidLidBusyStatus)
        return FB_STATUS.get(self._get_fast(proptag))

    @busystatus.setter
    def busystatus(self, val):
        try:
            # props are identical
            self.create_prop('appointment:33285', STATUS_FB[val], PT_LONG)
        except KeyError:
            raise ArgumentError('invalid busy status: %s' % val)


    @property
    def recurrence(self):
        """Appointment :class:`recurrence <Recurrence>`."""
        try:
            return Recurrence(self)
        except NotFoundError:
            pass

    def occurrences(self, start=None, end=None):
        """Appointment :class:`occurrences <Occurrence>` (expanding
        recurring appointments).

        :param start: start from given date (optional)
        :param end: end at given date (optional)
        """

        recurrence = self.recurrence
        if self.recurring and recurrence:
            for occ in self.recurrence.occurrences(start=start, end=end):
                yield occ
        else:
            if (not start or self.end > start) and \
               (not end or self.start < end):
                start = max(self.start, start) if start else self.start
                end = min(self.end, end) if end else self.end
                yield Occurrence(self, start, end)

        if not recurrence and self.recurring:
            self.server.log.warn("Item '%s' has no recurrence ", self.entryid)
        if recurrence and not self.recurring:
            self.server.log.warn("Item '%s' has a recurrence but is not recurring", self.entryid)


    def occurrence(self, id_=None):
        if self.recurring:
            return self.recurrence.occurrence(id_)
        else:
            # TODO check if matches args
            return Occurrence(self)

    @property
    def reminder(self):
        """Is appointment reminder set."""
        return self.get(PidLidReminderSet, False)

    @reminder.setter
    def reminder(self, value): # TODO move to item, because common?
        self[PidLidReminderSet] = value

    @property
    def reminder_minutes(self):
        """Reminder minutes before appointment."""
        return self.get(PidLidReminderDelta)

    @reminder_minutes.setter
    def reminder_minutes(self, value):
        self[PidLidReminderDelta] = value

    @property
    def rrule(self): # TODO including timezone!
        if self.recurring: # TODO rrule for non-recurring makes sense?
            return self.recurrence.recurrences

    # TODO rrule setter!

    def attendees(self):
        """Appointment :class:`attendees <Attendee>`."""

        # Filter out organizer from all recipients
        restriction = Restriction(SOrRestriction([SNotRestriction(SExistRestriction(PR_RECIPIENT_FLAGS)),
                                                  SBitMaskRestriction(BMR_EQZ, PR_RECIPIENT_FLAGS, recipOrganizer)]))
        for row in self.table(PR_MESSAGE_RECIPIENTS, restriction=restriction):
            yield Attendee(self.server, row)

    def create_attendee(self, type_, address):
        """Create appointment :class:`attendee <Attendee>`.

        :param type_: attendee type (required, optional or resource)
        :param address: attendee address (str or :class:`address <Address>`)
        """
        # TODO move to Attendee class
        # TODO return Attendee instance

        reciptype = {
            'required': 1,
            'optional': 2,
            'resource': 3
        }[type_]

        table = self.table(PR_MESSAGE_RECIPIENTS)
        names = []
        pr_addrtype, pr_dispname, pr_email, pr_entryid = \
            self._addr_props(address)
        names.append([
            SPropValue(PR_RECIPIENT_TYPE, reciptype),
            SPropValue(PR_DISPLAY_NAME_W, pr_dispname),
            SPropValue(PR_ADDRTYPE_W, str(pr_addrtype)),
            SPropValue(PR_EMAIL_ADDRESS_W, str(pr_email)),
            SPropValue(PR_ENTRYID, pr_entryid),
        ])
        self.mapiobj.ModifyRecipients(MODRECIP_ADD, names)

        self[PidLidAppointmentStateFlags] = ASF_MEETING
        self[PidLidResponseStatus] = respOrganized # TODO delegation?

        _utils._save(self.mapiobj)

    @property
    def response_requested(self):
        """Is appointment response requested."""
        return self.get(PR_RESPONSE_REQUESTED, False)

    @property
    def response_status(self):
        try:
            return RESPONSE_STATUS.get(self[PidLidResponseStatus])
        except NotFoundError:
            return 'None'

    @response_status.setter
    def response_status(self, val):
        try:
            # props are identical
            self.create_prop('appointment:33304', STATUS_RESPONSE[val], PT_LONG)
        except KeyError:
            raise ArgumentError('invalid response status: %s' % val)

    @property
    def icaluid(self):
        """Appointment iCal UID."""
        try:
            return _benc(self[PidLidGlobalObjectId])
        except NotFoundError:
            pass

    @property
    def eventid(self):
        # msgraph has both appointments and expanded appointments under
        # /events, so we need an identier which can be used for both.
        eid = _bdec(self.entryid)
        return _benc(b'\x00' + _utils.pack_short(len(eid)) + eid)

    @property
    def tzinfo(self):
        """Appointment timezone as datetime compatible tzinfo object."""
        tzdata = self._get_fast(self.store._pidlid_proptag(PidLidTimeZoneStruct))
        if tzdata:
            return _timezone.MAPITimezone(tzdata)

    @property
    def timezone(self):
        """Appointment timezone description."""
        return self.get(PidLidTimeZoneDescription)

    @timezone.setter
    def timezone(self, value):
        self[PidLidTimeZoneDescription] = str(value)
        self[PidLidTimeZoneStruct] = _timezone._timezone_struct(value)

    @property
    def replytime(self):
        return self.get(PidLidAppointmentReplyTime)

    @replytime.setter
    def replytime(self, value):
        self[PidLidAppointmentReplyTime] = value

    @property
    def replyname(self):
        return self.get(PidLidAppointmentReplyName)

    @replyname.setter
    def replyname(self, value):
        self[PidLidAppointmentReplyName] = value

    def accept(self, comment=None, tentative=False, respond=True, subject_prefix=None):
        if tentative:
            self.busystatus = 'tentative'
            self.response_status = 'TentativelyAccepted'
        else:
            self.busystatus = 'busy'
            self.response_status = 'Accepted'

        self.replytime = datetime.now()
        self.replyname = self.store.user.fullname

        if respond:
            if tentative:
                if not subject_prefix:
                    subject_prefix = 'Tentatively acccepted'
                message_class = 'IPM.Schedule.Meeting.Resp.Tent'
                self._respond(subject_prefix, message_class, comment)
            else:
                if not subject_prefix:
                    subject_prefix = 'Accept'
                message_class = 'IPM.Schedule.Meeting.Resp.Pos'
                self._respond(subject_prefix, message_class, comment)

    def decline(self, comment=None, respond=True):
        self.response_status = 'Declined'
        self.replytime = datetime.now()
        self.replyname = self.store.user.fullname

        if respond:
            message_class = 'IPM.Schedule.Meeting.Resp.Neg'
            self._respond('Declined', message_class, comment)

    # TODO merge with meetingrequest version
    def _respond(self, subject_prefix, message_class, comment=None):
        response = self.copy(self.store.outbox)
        response.message_class = message_class

        response.subject = subject_prefix + ': ' + self.subject
        if comment:
            response.text = comment

        if not self.from_.email:
            self.server.log.debug("Item '%s' has no organiser cannot send meeting request response", self.entryid)
            return

        response.from_ = self.store.user # TODO slow?

        try:
            response.to = self.server.user(email=self.from_.email)
        except NotFoundError:
            response.to = self.from_.email

        response.send()

    def cancel(self):
        self[PidLidAppointmentStateFlags] |= ASF_CANCELED

    @property
    def canceled(self):
        """Is appointment canceled."""
        return bool(self[PidLidAppointmentStateFlags] & ASF_CANCELED)

    @property
    def color(self): # property used by old clients
        """Appointment color (old clients)."""
        nr = self.get(PidLidAppointmentColor, 0)
        if nr != 0:
            return NR_COLOR[nr]

    @color.setter
    def color(self, value):
        try:
            self[PidLidAppointmentColor] = COLOR_NR[value]
        except KeyError:
            raise ArgumentError('invalid color: %r' % value)

    @property
    def onlinemeetingurl(self):
        try:
            return self.prop('public:OnlineMeetingExternalLink', proptype=PT_UNICODE).value
        except NotFoundError:
            return ''

    @onlinemeetingurl.setter
    def onlinemeetingurl(self, value):
        if not isinstance(value, str):
            raise ArgumentError('meeting url must be a string')
        self.create_prop('public:OnlineMeetingExternalLink', value, proptype=PT_UNICODE)
