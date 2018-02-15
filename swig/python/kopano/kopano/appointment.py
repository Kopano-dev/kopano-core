"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)
"""
import datetime

from MAPI import (
    PT_SYSTIME,
)

from MAPI.Tags import (
    PR_MESSAGE_RECIPIENTS, PR_RESPONSE_REQUESTED,
)

from .attendee import Attendee
from .errors import NotFoundError
from .recurrence import Recurrence, Occurrence

from .compat import (
    benc as _benc, bdec as _bdec,
)
from .pidlid import (
    PidLidReminderSet, PidLidReminderDelta, PidLidAppointmentSubType,
    PidLidBusyStatus, PidLidGlobalObjectId,
)
from . import utils as _utils

class Appointment(object):
    """Appointment mixin class"""

    @property
    def all_day(self):
        return self.get(PidLidAppointmentSubType, False)

    @property
    def show_as(self):
        try:
            return {
                0: u'free',
                1: u'tentative',
                2: u'busy',
                3: u'out_of_office',
                4: u'working_elsewhere'
            }[self[PidLidBusyStatus]]
        except NotFoundError:
            return u'unknown'

    @property
    def start(self): # XXX optimize, guid
        try:
            return self.prop('common:34070').value
        except NotFoundError:
            pass

    @start.setter
    def start(self, val):
        # XXX check if exists?
        self.create_prop('common:34070', val, PT_SYSTIME)
        self.create_prop('appointment:33293', val, PT_SYSTIME)

    @property
    def end(self): # XXX optimize, guid
        try:
            return self.prop('common:34071').value
        except NotFoundError:
            pass

    @end.setter
    def end(self, val):
        # XXX check if exists?
        self.create_prop('common:34071', val, PT_SYSTIME)
        self.create_prop('appointment:33294', val, PT_SYSTIME)

    @property
    def location(self):
        try:
            return self.prop('appointment:33288').value
        except NotFoundError:
            pass

    @property
    def recurring(self):
        try:
            return self.prop('appointment:33315').value
        except NotFoundError:
            return False

    @property
    def recurrence(self):
        return Recurrence(self)

    def occurrences(self, start=None, end=None):
        if self.recurring:
            for occ in self.recurrence.occurrences(start=start, end=end):
                yield occ
        else:
            if (not start or self.end > start) and \
               (not end or self.start < end):
                start = max(self.start, start) if start else self.start
                end = min(self.end, end) if end else self.end
                yield Occurrence(self, start, end)

    def occurrence(self, id_=None):
        if self.recurring:
            if isinstance(id_, datetime.datetime):
                # TODO optimize
                for occ in self.occurrences():
                    if occ.start == id_:
                        return occ
                        break
                else:
                    raise NotFoundError('no occurrence for date: %s' % id_)
            else:
                return self.recurrence.occurrence(id_)
        else:
            # TODO check if matches args
            return Occurrence(self)

    @property
    def reminder(self):
        """Is reminder set."""
        return self.get(PidLidReminderSet, False)

    @property
    def reminder_minutes(self):
        """Reminder minutes before appointment."""
        return self.get(PidLidReminderDelta)

    @property
    def rrule(self): # XXX including timezone!
        if self.recurring: # XXX rrule for non-recurring makes sense?
            return self.recurrence.recurrences

    # XXX rrule setter!

    # TODO merge with item.recipients?
    def attendees(self):
        for row in self.table(PR_MESSAGE_RECIPIENTS):
            yield Attendee(self.server, row)

    @property
    def response_requested(self):
        return self.get(PR_RESPONSE_REQUESTED, False)

    @property
    def icaluid(self):
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
