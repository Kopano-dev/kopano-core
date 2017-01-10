"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import datetime

from dateutil.rrule import WEEKLY, DAILY, MONTHLY, MO, TU, TH, FR, WE, SA, SU, rrule, rruleset
from datetime import timedelta

from utils import (
    unpack_short as _unpack_short, unpack_long as _unpack_long,
    unpack_string as _unpack_string, rectime_to_unixtime as _rectime_to_unixtime,
    repr as _repr
)

from .defs import *

class Recurrence:
    def __init__(self, item): # XXX just readable start/end for now
        # TODO: add check if we actually have a recurrence, otherwise we throw a mapi exception which might not be desirable
        self.item = item
        value = item.prop('appointment:33302').value # recurrencestate
        SHORT, LONG = 2, 4
        pos = 5 * SHORT + 3 * LONG 

        self.recurrence_frequency = _unpack_short(value, 2 * SHORT)
        self.patterntype = _unpack_short(value, 3 * SHORT)
        self.calendar_type = _unpack_short(value, 4 * SHORT)
        self.first_datetime = _unpack_long(value, 5 * SHORT)
        self.period = _unpack_long(value , 5 * SHORT + LONG) # 12 for year, coincedence?

        if self.patterntype == 1: # Weekly recurrence
            self.pattern = _unpack_long(value, pos) # WeekDays
            pos += LONG
        if self.patterntype in (2, 4, 10, 12): # Monthly recurrence
            self.pattern = _unpack_long(value, pos) # Day Of Month
            pos += LONG
        elif self.patterntype in (3, 11): # Yearly recurrence
            weekday = _unpack_long(value, pos)
            pos += LONG
            weeknumber = _unpack_long(value, pos)
            pos += LONG

        self.endtype = _unpack_long(value, pos)
        pos += LONG
        self.occurrence_count = _unpack_long(value, pos)
        pos += LONG
        self.first_dow = _unpack_long(value, pos)
        pos += LONG

        # Number of ocurrences which have been removed in a recurrene
        self.delcount = _unpack_long(value, pos)
        pos += LONG
        # XXX: optimize?
        self.del_recurrences = []
        for _ in range(0, self.delcount):
            self.del_recurrences.append(datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos))))
            pos += LONG

        self.modcount = _unpack_long(value, pos)
        pos += LONG
        # XXX: optimize?
        self.mod_recurrences = []
        for _ in range(0, self.modcount):
            self.mod_recurrences.append(datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos))))
            pos += LONG

        self.start = datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos)))
        pos += LONG
        self.end = datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos)))

        pos += 3 * LONG # ulReaderVersion2, ulReaderWriter2
        self.startime_offset = _unpack_long(value, pos) # XXX: type?
        pos += LONG
        self.endtime_offset = _unpack_long(value, pos) # XXX: type?
        pos += LONG

        self.start = datetime.datetime(self.start.year, self.start.month, self.start.day) + datetime.timedelta(minutes=self.startime_offset)
        self.end = datetime.datetime(self.end.year, self.end.month, self.end.day) + datetime.timedelta(minutes=self.endtime_offset)

        # Exceptions
        self.exception_count = _unpack_short(value, pos)
        pos += SHORT

        # FIXME: create class instances.
        self.exceptions = []
        for i in range(0, self.exception_count):
            exception = {}
            # Blegh helper..
            exception['startdatetime'] = datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos)))
            pos += LONG
            exception['enddatetime'] = datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos)))
            pos += LONG
            exception['originalstartdate'] = datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos)))
            pos += LONG
            exception['overrideflags'] = _unpack_short(value, pos)
            pos += SHORT

            # We have modified the subject
            if exception['overrideflags'] & ARO_SUBJECT:
                subject_length1 = _unpack_short(value, pos) # XXX: unused?
                pos += SHORT
                subject_length2 = _unpack_short(value, pos)
                pos += SHORT
                exception['subject'] = _unpack_string(value, pos, subject_length2)
                pos += subject_length2

            # XXX: Changed the meeting type too???
            if exception['overrideflags'] & ARO_MEETINGTYPE:
                exception['meetingtype'] = _unpack_long(value, pos)
                pos += LONG

            if exception['overrideflags'] & ARO_REMINDERDELTA:
                exception['reminderdelta'] = _unpack_long(value, pos) # XXX: datetime?
                pos += LONG

            if exception['overrideflags'] & ARO_REMINDERSET:
                exception['reminderset'] = _unpack_long(value, pos) # XXX: bool?
                pos += LONG

            if exception['overrideflags'] & ARO_LOCATION:
                localation_length1 = _unpack_short(value, pos) # XXX: unused?
                pos += SHORT
                location_length2 = _unpack_short(value, pos)
                pos += SHORT
                exception['location'] = _unpack_string(value, pos, location_length2)
                pos += location_length2

            if exception['overrideflags'] & ARO_BUSYSTATUS:
                exception['busystatus'] = _unpack_long(value, pos)
                pos += LONG

            if exception['overrideflags'] & ARO_ATTACHMENT:
                exception['attachment'] = _unpack_long(value, pos)
                pos += LONG

            if exception['overrideflags'] & ARO_SUBTYPE:
                exception['subtype'] = _unpack_long(value, pos)
                pos += LONG

            if exception['overrideflags'] & ARO_APPTCOLOR:
                exception['color'] = _unpack_long(value, pos)
                pos += LONG

            self.exceptions.append(exception)


        # FIXME: move to class Item? XXX also some of these properties do not seem to exist when syncing over Z-push
#        self.clipend = item.prop('appointment:33334').value
#        self.clipstart = item.prop('appointment:33333').value
        self.recurrence_pattern = item.prop('appointment:33330').value
#        self.invited = item.prop('appointment:33321').value

        # FIXME; doesn't dateutil have a list of this?
        rrule_weekdays = {0: SU, 1: MO, 2: TU, 3: WE, 4: TH, 5: FR, 6: SA} # FIXME: remove above

        # FIXME: add DAILY, patterntype == 0
        # FIXME: merge exception details with normal appointment data to recurrence.occurences() (Class occurence)
        if self.patterntype == 1: # WEEKLY
            byweekday = () # Set
            for index, week in rrule_weekdays.items():
                if (self.pattern >> index ) & 1:
                    byweekday += (week,)
            # Setup our rule
            rule = rruleset()
            # FIXME: add one day, so that we don't miss the last recurrence, since the end date is for example 11-3-2015 on 1:00
            # But the recurrence is on 8:00 that day and we should include it.
            rule.rrule(rrule(WEEKLY, dtstart=self.start, until=self.end + timedelta(days=1), byweekday=byweekday))

            self.recurrences = rule
            #self.recurrences = rrule(WEEKLY, dtstart=self.start, until=self.end, byweekday=byweekday)
        elif self.patterntype == 2: # MONTHLY
            # X Day of every Y month(s)
            # The Xnd Y (day) of every Z Month(s)
            self.recurrences = rrule(MONTHLY, dtstart=self.start, until=self.end, bymonthday=self.pattern, interval=self.period)
            # self.pattern is either day of month or 
        elif self.patterntype == 3: # MONTHY, YEARLY
            byweekday = () # Set
            for index, week in rrule_weekdays.items():
                if (weekday >> index ) & 1:
                    byweekday += (week(weeknumber),)
            # Yearly, the last XX of YY
            self.recurrences = rrule(MONTHLY, dtstart=self.start, until=self.end, interval=self.period, byweekday=byweekday)

        # Remove deleted ocurrences
        for del_date in self.del_recurrences:
            # XXX: Somehow rule.rdate does not work in combination with rule.exdate
            del_date = datetime.datetime(del_date.year, del_date.month, del_date.day, self.start.hour, self.start.minute)
            if not del_date in self.mod_recurrences:
                rule.exdate(del_date)

        # add exceptions
        for exception in self.exceptions:
            rule.rdate(exception['startdatetime'])


    def __unicode__(self):
        return u'Recurrence(start=%s - end=%s)' % (self.start, self.end)

    def __repr__(self):
        return _repr(self)


class Occurrence(object):
    def __init__(self, item, start, end):
        self.item = item
        self.start = start
        self.end = end

    def __getattr__(self, x):
        return getattr(self.item, x)

    def __unicode__(self):
        return u'Occurrence(%s)' % self.subject

    def __repr__(self):
        return _repr(self)


