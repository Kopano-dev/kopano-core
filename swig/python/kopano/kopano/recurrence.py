"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import calendar
import datetime
import struct
import sys
import time

from MAPI import (
    MAPI_UNICODE, KEEP_OPEN_READWRITE, MODRECIP_ADD, MODRECIP_MODIFY,
    MSGFLAG_READ, MSGFLAG_UNSENT, ATTACH_EMBEDDED_MSG,
)

from MAPI.Tags import (
    PR_MESSAGE_CLASS_W, PR_ATTACH_FLAGS, PR_ATTACHMENT_FLAGS,
    PR_ATTACHMENT_HIDDEN, PR_ATTACH_METHOD, PR_DISPLAY_NAME_W,
    PR_EXCEPTION_STARTTIME, PR_EXCEPTION_ENDTIME,
    PR_NORMALIZED_SUBJECT_W, PR_ATTACHMENT_LINKID, PR_ICON_INDEX,
    PR_MESSAGE_RECIPIENTS, IID_IMAPITable, PR_RECIPIENT_FLAGS,
    PR_MESSAGE_FLAGS, PR_RECIPIENT_TRACKSTATUS, recipSendable,
    recipExceptionalResponse, recipExceptionalDeleted, recipOrganizer,
    respOrganized, respDeclined,
)

from MAPI.Defs import PpropFindProp

from MAPI.Struct import (
    SPropValue,
)

from MAPI.Time import (
    unixtime,
)

from dateutil.rrule import (
    DAILY, WEEKLY, MONTHLY, MO, TU, TH, FR, WE, SA, SU, rrule, rruleset
)
from datetime import timedelta

from .compat import (
    repr as _repr, benc as _benc, bdec as _bdec,
)
from .errors import (
    NotSupportedError, NotFoundError,
)
from .defs import (
    ARO_SUBJECT, ARO_MEETINGTYPE, ARO_REMINDERDELTA, ARO_REMINDERSET,
    ARO_LOCATION, ARO_BUSYSTATUS, ARO_ATTACHMENT, ARO_SUBTYPE,
    ARO_APPTCOLOR
)

if sys.hexversion >= 0x03000000:
    try:
        from . import utils as _utils
    except ImportError:
        _utils = sys.modules[__package__ + '.utils']
    from . import meetingrequest as _meetingrequest
else:
    import utils as _utils
    import meetingrequest as _meetingrequest

SHORT, LONG = 2, 4

from .pidlid import (
    PidLidSideEffects, PidLidSmartNoAttach,
    PidLidReminderSet, PidLidReminderSignalTime, PidLidReminderDelta,
    PidLidBusyStatus, PidLidExceptionReplaceTime,
    PidLidResponseStatus, PidLidTimeZoneStruct, PidLidAppointmentRecur,
    PidLidLocation, PidLidAppointmentSubType, PidLidAppointmentColor,
    PidLidIntendedBusyStatus, PidLidAppointmentStartWhole,
    PidLidAppointmentEndWhole,
)

FREQ_DAY = 0x200A
FREQ_WEEK = 0x200B
FREQ_MONTH = 0x200C
FREQ_YEAR = 0x200D

# TODO shouldn't we handle pattern 4, 0xC, and what about the Hj stuff?
PATTERN_DAILY = 0
PATTERN_WEEKLY = 1
PATTERN_MONTHLY = 2
PATTERN_MONTHNTH = 3
PATTERN_MONTHEND = 4
PATTERN_HJMONTHLY = 0xA
PATTERN_HJMONTHNTH = 0xB
PATTERN_HJMONTHEND = 0xC

RRULE_WEEKDAYS = {0: SU, 1: MO, 2: TU, 3: WE, 4: TH, 5: FR, 6: SA}

# see MS-OXOCAL, section 2.2.1.44.5, "AppointmentRecurrencePattern Structure"

# TODO hide low-level variables (self._pattern_type etc)
# TODO ability to set attributes in any order

class Recurrence(object):
    """Recurrence class"""

    def __init__(self, item, parse=True):
        # XXX add check if we actually have a recurrence, otherwise we throw a mapi exception which might not be desirable

        self.item = item
        self._tz = item.get(PidLidTimeZoneStruct)

        if parse:
            self._parse()

    @property
    def pattern(self):
        if self._recur_frequency == FREQ_YEAR:
            return {
                PATTERN_MONTHLY: 'yearly',
                PATTERN_MONTHNTH: 'yearly_rel',
            }[self._pattern_type]
        else:
            return {
                PATTERN_DAILY: 'daily',
                PATTERN_WEEKLY: 'weekly',
                PATTERN_MONTHLY: 'monthly',
                PATTERN_MONTHNTH: 'monthly_rel',
            }[self._pattern_type]

    @pattern.setter
    def pattern(self, value):
        if value == 'daily': # TODO use mapping
            self._recur_frequency = FREQ_DAY
            self._pattern_type = PATTERN_DAILY
        elif value == 'weekly':
            self._recur_frequency = FREQ_WEEK
            self._pattern_type = PATTERN_WEEKLY
        elif value == 'monthly':
            self._recur_frequency = FREQ_MONTH
            self._pattern_type = PATTERN_MONTHLY
        elif value == 'monthly_rel':
            self._recur_frequency = FREQ_MONTH
            self._pattern_type = PATTERN_MONTHNTH
        elif value == 'yearly':
            self._recur_frequency = FREQ_YEAR
            self._pattern_type = PATTERN_MONTHLY
        elif value == 'yearly_rel':
            self._recur_frequency = FREQ_YEAR
            self._pattern_type = PATTERN_MONTHNTH

    @property
    def weekdays(self):
        if self._pattern_type in (PATTERN_WEEKLY, PATTERN_MONTHNTH, PATTERN_HJMONTHNTH):
            weekdays = {0: 'sunday', 1: 'monday', 2: 'tuesday', 3: 'wednesday', 4: 'thursday', 5: 'friday', 6: 'saturday'}
            days = []
            for index, week in weekdays.items():
                if (self._pattern_type_specific[0] >> index ) & 1:
                    days.append(week)
            return days

    @weekdays.setter
    def weekdays(self, value):
        if self._pattern_type in (PATTERN_WEEKLY, PATTERN_MONTHNTH, PATTERN_HJMONTHNTH):
            weekdays = {'sunday': 0, 'monday': 1, 'tuesday': 2, 'wednesday': 3, 'thursday': 4, 'friday': 5, 'saturday': 6}
            pts = 0
            for weekday in value:
                pts |= (1 << weekdays[weekday])
            self._pattern_type_specific = [pts]

            if self._pattern_type in (PATTERN_MONTHNTH, PATTERN_HJMONTHNTH):
                self._pattern_type_specific.append(5) # TODO

        # TODO fill in

    @property
    def first_weekday(self):
        weekdays = {0: 'sunday', 1: 'monday', 2: 'tuesday', 3: 'wednesday', 4: 'thursday', 5: 'friday', 6: 'saturday'}
        return weekdays[self._first_dow]

    @property
    def month(self):
        if self._recur_frequency == FREQ_YEAR:
            return self._start.month # TODO isn't this stored explicitly!?

    @property
    def monthday(self):
        if self._pattern_type in (PATTERN_MONTHLY, PATTERN_HJMONTHLY):
            return self._pattern_type_specific[0]

    @monthday.setter
    def monthday(self, value):
        if self._pattern_type in (PATTERN_MONTHLY, PATTERN_HJMONTHLY):
            self._pattern_type_specific = [value]

    @property
    def index(self):
        if self._pattern_type in (PATTERN_MONTHNTH, PATTERN_HJMONTHNTH):
            return {
                1: u'first',
                2: u'second',
                3: u'third',
                4: u'fourth',
                5: u'last',
            }[self._pattern_type_specific[1]]

    @property
    def interval(self):
        if self._recur_frequency == FREQ_YEAR:
            return self._period // 12
        elif self._pattern_type == PATTERN_DAILY:
            return self._period // (24 * 60)
        else:
            return self._period

    @interval.setter
    def interval(self, value):
        if self._pattern_type == PATTERN_DAILY:
            self._period = value * (24 * 60)
        else:
            self._period = value
        # TODO fill in

    @property
    def range_type(self):
        if self._end_type == 0x2021:
            return 'end_date'
        elif self._end_type == 0x2022:
            return 'occurrence_count'
        elif self._end_type in (0x2023, 0xFFFFFFFF):
            return 'no_end'

    @range_type.setter
    def range_type(self, value):
        if value == 'end_date':
            self._end_type = 0x2021
        elif value == 'occurrence_count':
            self._end_type = 0x2022
        elif value == 'no_end':
            self._end_type = 0x2023

    def occurrences(self, start=None, end=None): # XXX fit-to-period
        tz = self.item.get(PidLidTimeZoneStruct)
        tzinfo = self.item.tzinfo

        recurrences = self.recurrences
        if start and end:
            recurrences = recurrences.between(_utils._from_gmt(start, tz), _utils._from_gmt(end, tz))

        start_exc_ext = {}
        for exc, ext in zip(self._exceptions, self._extended_exceptions):
            start_exc_ext[exc['start_datetime']] = exc, ext

        for d in recurrences:
            startdatetime_val = _utils.unixtime_to_rectime(calendar.timegm(d.timetuple()))

            subject = self.item.subject
            location = self.item.location
            exception = False
            if startdatetime_val in start_exc_ext:
                exc, ext = start_exc_ext[startdatetime_val]
                minutes = exc['end_datetime'] - startdatetime_val
                subject = ext.get('subject', subject)
                location = ext.get('location', location)
                basedate_val = exc['original_start_date']
                exception = True
            else:
                minutes = self._endtime_offset - self._starttime_offset
                basedate_val = startdatetime_val

            d = d.replace(tzinfo=tzinfo).astimezone().replace(tzinfo=None)

            occ = Occurrence(self.item, d, d + datetime.timedelta(minutes=minutes), subject, location, basedate_val=basedate_val, exception=exception)
            if (not start or occ.end > start) and (not end or occ.start < end):
                yield occ

    def occurrence(self, entryid):
        entryid = _bdec(entryid)
        pos = 2 + _utils.unpack_short(entryid, 0)
        basedate_val = _utils.unpack_long(entryid, pos)

        for exc in self._exceptions: # TODO subject etc
            if exc['original_start_date'] in (basedate_val, basedate_val - self._starttime_offset): # TODO pick one
                start = datetime.datetime.fromtimestamp(_utils.rectime_to_unixtime(exc['start_datetime']))
                start = _utils._to_gmt(start, self._tz)
                break
        else:
            # TODO check that date is (still) valid
            start = datetime.datetime.fromtimestamp(_utils.rectime_to_unixtime(basedate_val))
            start = _utils._to_gmt(start, self._tz)

        return Occurrence(
            self.item,
            start,
            start + datetime.timedelta(minutes=self._endtime_offset - self._starttime_offset),
            basedate_val=basedate_val,
        )

    @staticmethod
    def _init(item):
        rec = Recurrence(item, parse=False)

        rec._reader_version = 0x3004
        rec._writer_version = 0x3004
        rec._recur_frequency = 0
        rec._pattern_type = 0
        rec._calendar_type = 0
        rec._first_datetime = 0
        rec._period = 0
        rec._sliding_flag = 0
        rec._pattern_type_specific = []
        rec._end_type = 0
        rec._occurrence_count = 0
        rec._first_dow = 0
        rec.deleted_instance_count = 0
        rec._deleted_instance_dates = []
        rec._modified_instance_count = 0
        rec._modified_instance_dates = []
        rec._start_date = 0
        rec._end_date = 0
        rec._exceptions = []
        rec._extended_exceptions = []

        rec._starttime_offset = 0
        rec._endtime_offset = 0
        rec._update_offsets(save=False)

        rec._save()

    def _update_offsets(self, save=True):
        item = self.item
        start = item.start
        if start:
            self._starttime_offset = start.hour * 60 + start.minute
        end = item.end
        if end:
            self._endtime_offset = end.hour * 60 + end.minute
        if save:
            self._save()

    def _parse(self):
        # AppointmentRecurrencePattern
        value = self.item.prop(PidLidAppointmentRecur).value

        # RecurrencePattern
        self._reader_version = _utils.unpack_short(value, 0)
        self._writer_version = _utils.unpack_short(value, SHORT)

        self._recur_frequency = _utils.unpack_short(value, 2 * SHORT)
        self._pattern_type = _utils.unpack_short(value, 3 * SHORT)
        self._calendar_type = _utils.unpack_short(value, 4 * SHORT)
        self._first_datetime = _utils.unpack_long(value, 5 * SHORT)
        self._period = _utils.unpack_long(value, 5 * SHORT + LONG)
        self._sliding_flag = _utils.unpack_long(value, 5 * SHORT + 2 * LONG)

        pos = 5 * SHORT + 3 * LONG

        self._pattern_type_specific = []
        if self._pattern_type != PATTERN_DAILY:
            self._pattern_type_specific.append(_utils.unpack_long(value, pos))
            pos += LONG
            if self._pattern_type in (PATTERN_MONTHNTH, PATTERN_HJMONTHNTH):
                self._pattern_type_specific.append(_utils.unpack_long(value, pos))
                pos += LONG

        self._end_type = _utils.unpack_long(value, pos)
        pos += LONG

        self._occurrence_count = _utils.unpack_long(value, pos)
        pos += LONG
        self._first_dow = _utils.unpack_long(value, pos)
        pos += LONG

        self.deleted_instance_count = _utils.unpack_long(value, pos)
        pos += LONG
        self._deleted_instance_dates = []
        for _ in range(0, self.deleted_instance_count):
            self._deleted_instance_dates.append(_utils.unpack_long(value, pos))
            pos += LONG

        self._modified_instance_count = _utils.unpack_long(value, pos)
        pos += LONG

        self._modified_instance_dates = []
        for _ in range(0, self._modified_instance_count):
            self._modified_instance_dates.append(_utils.unpack_long(value, pos))
            pos += LONG

        self._start_date = _utils.unpack_long(value, pos)
        pos += LONG
        self._end_date = _utils.unpack_long(value, pos)
        pos += LONG

        # AppointmentRecurrencePattern
        self._reader_version2 = _utils.unpack_long(value, pos)
        pos += LONG
        self._writer_version2 = _utils.unpack_long(value, pos)
        pos += LONG
        self._starttime_offset = _utils.unpack_long(value, pos)
        pos += LONG
        self._endtime_offset = _utils.unpack_long(value, pos)
        pos += LONG
        self.exception_count = _utils.unpack_short(value, pos) # TODO rename to _exception_count?
        pos += SHORT

        # ExceptionInfo
        self._exceptions = []
        for i in range(0, self._modified_instance_count): # using modcount, as PHP seems to not update exception_count? equal according to docs
            exception = {}

            val = _utils.unpack_long(value, pos)
            exception['start_datetime'] = val
            pos += LONG
            val = _utils.unpack_long(value, pos)
            exception['end_datetime'] = val
            pos += LONG
            val = _utils.unpack_long(value, pos)
            exception['original_start_date'] = val
            pos += LONG
            exception['override_flags'] = _utils.unpack_short(value, pos)
            pos += SHORT

            # We have modified the subject
            if exception['override_flags'] & ARO_SUBJECT:
                pos += SHORT
                subject_length2 = _utils.unpack_short(value, pos)
                pos += SHORT
                exception['subject'] = value[pos:pos + subject_length2]
                pos += subject_length2

            if exception['override_flags'] & ARO_MEETINGTYPE:
                exception['meeting_type'] = _utils.unpack_long(value, pos)
                pos += LONG

            if exception['override_flags'] & ARO_REMINDERDELTA:
                exception['reminder_delta'] = _utils.unpack_long(value, pos) # XXX: datetime?
                pos += LONG

            if exception['override_flags'] & ARO_REMINDERSET:
                exception['reminder_set'] = _utils.unpack_long(value, pos) # XXX: bool?
                pos += LONG

            if exception['override_flags'] & ARO_LOCATION:
                pos += SHORT
                location_length2 = _utils.unpack_short(value, pos)
                pos += SHORT
                exception['location'] = value[pos:pos + location_length2]
                pos += location_length2

            if exception['override_flags'] & ARO_BUSYSTATUS:
                exception['busy_status'] = _utils.unpack_long(value, pos)
                pos += LONG

            if exception['override_flags'] & ARO_ATTACHMENT:
                exception['attachment'] = _utils.unpack_long(value, pos)
                pos += LONG

            if exception['override_flags'] & ARO_SUBTYPE:
                exception['sub_type'] = _utils.unpack_long(value, pos)
                pos += LONG

            if exception['override_flags'] & ARO_APPTCOLOR:
                exception['appointment_color'] = _utils.unpack_long(value, pos)
                pos += LONG

            self._exceptions.append(exception)

        # ReservedBlock1Size
        pos += _utils.unpack_long(value, pos) + LONG

        # ExtendedException
        self._extended_exceptions = []
        for exception in self._exceptions:
            extended_exception = {}

            # ChangeHighlight
            if self._writer_version2 >= 0x3009:
                change_highlight_size = _utils.unpack_long(value, pos)
                pos += LONG
                change_highlight_value = _utils.unpack_long(value, pos)
                extended_exception['change_highlight'] = change_highlight_value
                pos += change_highlight_size

            # ReservedBlockEE1
            pos += _utils.unpack_long(value, pos) + LONG

            # StartDateTime, EndDateTime, OriginalStartDate
            if exception['override_flags'] & ARO_SUBJECT or exception['override_flags'] & ARO_LOCATION:
                extended_exception['start_datetime'] = _utils.unpack_long(value, pos)
                pos += LONG
                extended_exception['end_datetime'] = _utils.unpack_long(value, pos)
                pos += LONG
                extended_exception['original_start_date'] = _utils.unpack_long(value, pos)
                pos += LONG

            # WideCharSubject
            if exception['override_flags'] & ARO_SUBJECT:
                length = _utils.unpack_short(value, pos)
                pos += SHORT
                extended_exception['subject'] = value[pos:pos + 2 * length].decode('utf-16-le')
                pos += 2 * length

            # WideCharLocation
            if exception['override_flags'] & ARO_LOCATION:
                length = _utils.unpack_short(value, pos)
                pos += SHORT
                extended_exception['location'] = value[pos:pos + 2 * length].decode('utf-16-le')
                pos += 2 * length

            # ReservedBlockEE2
            if exception['override_flags'] & ARO_SUBJECT or exception['override_flags'] & ARO_LOCATION:
                pos += _utils.unpack_long(value, pos) + LONG

            self._extended_exceptions.append(extended_exception)

    def _save(self):
        # AppointmentRecurrencePattern

        # RecurrencePattern
        data = struct.pack(
            '<HHHHH', self._reader_version, self._writer_version,
            self._recur_frequency, self._pattern_type, self._calendar_type
        )

        data += struct.pack('<III', self._first_datetime, self._period, self._sliding_flag)

        for i in self._pattern_type_specific:
            data += struct.pack('<I', i)

        data += struct.pack('<I', self._end_type)

        if self._end_type == 0x2021: # stop after date
            occurrence_count = 0xa # TODO is this really needed (default occ count from webapp!)
        elif self._end_type == 0x2022: # stop after N occurrences
            occurrence_count = self._occurrence_count
        else:
            occurrence_count = 0
        data += struct.pack('<I', occurrence_count)

        data += struct.pack('<I', self._first_dow)

        data += struct.pack('<I', self.deleted_instance_count)
        for val in self._deleted_instance_dates:
            data += struct.pack('<I', val)

        data += struct.pack('<I', self._modified_instance_count)
        for val in self._modified_instance_dates:
            data += struct.pack('<I', val)

        data += struct.pack('<II', self._start_date, self._end_date)

        data += struct.pack('<II', 0x3006, 0x3008)
        data += struct.pack('<II', self._starttime_offset, self._endtime_offset)

        # ExceptionInfo
        data += struct.pack('<H', self._modified_instance_count)

        for exception in self._exceptions:
            data += struct.pack('<I', exception['start_datetime'])
            data += struct.pack('<I', exception['end_datetime'])
            data += struct.pack('<I', exception['original_start_date'])
            data += struct.pack('<H', exception['override_flags'])

            if exception['override_flags'] & ARO_SUBJECT:
                subject = exception['subject']
                data += struct.pack('<H', len(subject) + 1)
                data += struct.pack('<H', len(subject))
                data += subject

            if exception['override_flags'] & ARO_MEETINGTYPE:
                data += struct.pack('<I', exception['meeting_type'])

            if exception['override_flags'] & ARO_REMINDERDELTA:
                data += struct.pack('<I', exception['reminder_delta'])

            if exception['override_flags'] & ARO_REMINDERSET:
                data += struct.pack('<I', exception['reminder_set'])

            if exception['override_flags'] & ARO_LOCATION:
                location = exception['location']
                data += struct.pack('<H', len(location) + 1)
                data += struct.pack('<H', len(location))
                data += location

            if exception['override_flags'] & ARO_BUSYSTATUS:
                data += struct.pack('<I', exception['busy_status'])

            if exception['override_flags'] & ARO_ATTACHMENT:
                data += struct.pack('<I', exception['attachment'])

            if exception['override_flags'] & ARO_SUBTYPE:
                data += struct.pack('<I', exception['sub_type'])

            if exception['override_flags'] & ARO_APPTCOLOR:
                data += struct.pack('<I', exception['appointment_color'])

        # ReservedBlock1Size
        data += struct.pack('<I', 0)

        # ExtendedException
        for exception, extended_exception in zip(self._exceptions, self._extended_exceptions):
            data += struct.pack('<I', 0)

            overrideflags = exception['override_flags']

            if overrideflags & ARO_SUBJECT or overrideflags & ARO_LOCATION:
                data += struct.pack('<I', extended_exception['start_datetime'])
                data += struct.pack('<I', extended_exception['end_datetime'])
                data += struct.pack('<I', extended_exception['original_start_date'])

            if overrideflags & ARO_SUBJECT:
                subject = extended_exception['subject']
                data += struct.pack('<H', len(subject))
                data += subject.encode('utf-16-le')

            if overrideflags & ARO_LOCATION:
                location = extended_exception['location']
                data += struct.pack('<H', len(location))
                data += location.encode('utf-16-le')

            if overrideflags & ARO_SUBJECT or overrideflags & ARO_LOCATION:
                data += struct.pack('<I', 0)

        # ReservedBlock2Size
        data += struct.pack('<I', 0)

        self.item[PidLidAppointmentRecur] = data

    @property
    def _start(self):
        # local to recurrence timezone!
        return datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(self._start_date)) + datetime.timedelta(minutes=self._starttime_offset)

    @property
    def start(self):
        """ Start of recurrence range """
        tz_start = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(self._start_date))
        return tz_start.replace(tzinfo=self.item.tzinfo).astimezone().replace(tzinfo=None)

    @_start.setter # TODO start.setter
    def _start(self, value):
        self._start_date = _utils.unixtime_to_rectime(time.mktime(value.date().timetuple()))

    @property # TODO end.setter
    def _end(self):
        # local to recurrence timezone!
        return datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(self._end_date)) + datetime.timedelta(minutes=self._endtime_offset)

    @property
    def end(self):
        """ End of recurrence range """
        tz_end = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(self._end_date))
        return tz_end.replace(tzinfo=self.item.tzinfo).astimezone().replace(tzinfo=None)

    @_end.setter
    def _end(self, value):
        self._end_date = _utils.unixtime_to_rectime(time.mktime(value.date().timetuple()))

    # TODO functionality below here should be refactored or not visible

    @property
    def recurrences(self): # TODO rename to _recurrences and/or rrule?
        rule = rruleset()

        if self._pattern_type == PATTERN_DAILY:
            rule.rrule(rrule(DAILY, dtstart=self._start, until=self._end, interval=self._period // (24 * 60)))

        if self._pattern_type == PATTERN_WEEKLY:
            byweekday = () # Set
            for index, week in RRULE_WEEKDAYS.items():
                if (self._pattern_type_specific[0] >> index ) & 1:
                    byweekday += (week,)
            # FIXME: add one day, so that we don't miss the last recurrence, since the end date is for example 11-3-2015 on 1:00
            # But the recurrence is on 8:00 that day and we should include it.
            rule.rrule(rrule(WEEKLY, wkst=self._start.weekday(), dtstart=self._start, until=self._end + timedelta(days=1), byweekday=byweekday, interval=self._period))

        elif self._pattern_type == PATTERN_MONTHLY:
            # X Day of every Y month(s)
            # The Xnd Y (day) of every Z Month(s)
            rule.rrule(rrule(MONTHLY, dtstart=self._start, until=self._end, bymonthday=self._pattern_type_specific[0], interval=self._period))
            # self._pattern_type_specific[0] is either day of month or

        elif self._pattern_type == PATTERN_MONTHNTH:
            byweekday = () # Set
            for index, week in RRULE_WEEKDAYS.items():
                if (self._pattern_type_specific[0] >> index ) & 1:
                    if self._pattern_type_specific[1] == 5:
                        byweekday += (week(-1),) # last week of month
                    else:
                        byweekday += (week(self._pattern_type_specific[1]),)
            # Yearly, the last XX of YY
            rule.rrule(rrule(MONTHLY, dtstart=self._start, until=self._end, interval=self._period, byweekday=byweekday))

        elif self._pattern_type != PATTERN_DAILY: # XXX check 0
            raise NotSupportedError('Unsupported recurrence pattern: %d' % self._pattern_type)

        # add exceptions
        exc_starts = set()
        for exception in self._exceptions:
            exc_start = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(exception['start_datetime']))
            rule.rdate(exc_start)
            exc_starts.add(exc_start)

        # Remove deleted ocurrences (skip added exceptions)
        for del_date_val in self._deleted_instance_dates:
            del_date = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(del_date_val))
            del_date = datetime.datetime(del_date.year, del_date.month, del_date.day, self._start.hour, self._start.minute)
            if del_date not in exc_starts:
                rule.exdate(del_date)

        return rule

    def _exception_message(self, basedate):
        for message in self.item.items():
            replacetime = message.get(PidLidExceptionReplaceTime)
            if replacetime and replacetime.date() == basedate.date():
                return message

    def _is_exception(self, basedate):
        return self._exception_message(basedate) is not None

    def _update_exception(self, cal_item, item, basedate_val, exception, extended_exception, copytags=None, create=False): # XXX kill copytags, create args, just pass all properties as in php
        tz = item.get(PidLidTimeZoneStruct)

        # TODO get start/end from cal_item if not in item?
        startdate = item.get(PidLidAppointmentStartWhole)
        if startdate is not None:
            startdate_val = _utils.unixtime_to_rectime(time.mktime(_utils._from_gmt(startdate, tz).timetuple()))
            exception['start_datetime'] = startdate_val

        enddate = item.get(PidLidAppointmentEndWhole)
        if enddate is not None:
            enddate_val = _utils.unixtime_to_rectime(time.mktime(_utils._from_gmt(enddate, tz).timetuple()))
            exception['end_datetime'] = enddate_val

        exception['original_start_date'] = basedate_val # TODO why set again?
        exception['override_flags'] = 0

        extended = False
        subject = item.get(PR_NORMALIZED_SUBJECT_W)
        if subject is not None:
            exception['override_flags'] |= ARO_SUBJECT
            exception['subject'] = subject.encode('cp1252', 'replace')
            extended = True
            extended_exception['subject'] = subject

        # skip ARO_MEETINGTYPE (like php)

        reminder_delta = item.get(PidLidReminderDelta)
        if reminder_delta is not None:
            exception['override_flags'] |= ARO_REMINDERDELTA
            exception['reminder_delta'] = reminder_delta

        reminder_set = item.get(PidLidReminderSet)
        if reminder_set is not None:
            exception['override_flags'] |= ARO_REMINDERSET
            exception['reminder_set'] = reminder_set

        location = item.get(PidLidLocation)
        if location is not None:
            exception['override_flags'] |= ARO_LOCATION
            exception['location'] = location.encode('cp1252', 'replace')
            extended = True
            extended_exception['location'] = location

        busy_status = item.get(PidLidBusyStatus)
        if busy_status is not None:
            exception['override_flags'] |= ARO_BUSYSTATUS
            exception['busy_status'] = busy_status

        # skip ARO_ATTACHMENT (like php)

        # XXX php doesn't set the following by accident? ("alldayevent" not in copytags..)
        if not copytags or not create:
            sub_type = item.get(PidLidAppointmentSubType)
            if sub_type is not None:
                exception['override_flags'] |= ARO_SUBTYPE
                exception['sub_type'] = sub_type

        appointment_color = item.get(PidLidAppointmentColor)
        if appointment_color is not None:
            exception['override_flags'] |= ARO_APPTCOLOR
            exception['appointment_color'] = appointment_color

        if extended:
            extended_exception['start_datetime'] = startdate_val
            extended_exception['end_datetime'] = enddate_val
            extended_exception['original_start_date'] = basedate_val

    def _update_calitem(self):
        tz = self.item.get(PidLidTimeZoneStruct)
        tzinfo = self.item.tzinfo
        cal_item = self.item

        cal_item[PidLidSideEffects] = 3441 # XXX spec, check php
        cal_item[PidLidSmartNoAttach] = True

        # reminder
        if cal_item.get(PidLidReminderSet) and cal_item.get(PidLidReminderDelta):
            next_date = self.recurrences.after(datetime.datetime.now(tzinfo).replace(tzinfo=None))
            if next_date:
                next_date = next_date.replace(tzinfo=tzinfo).astimezone().replace(tzinfo=None)
                dueby = next_date - datetime.timedelta(minutes=cal_item.get(PidLidReminderDelta))
                cal_item[PidLidReminderSignalTime] = dueby
            else:
                cal_item[PidLidReminderSet] = False
                cal_item[PidLidReminderSignalTime] = datetime.datetime.fromtimestamp(0x7ff00000)

    def _update_embedded(self, basedate, message, item, copytags=None, create=False):
        basetime = basedate + datetime.timedelta(minutes=self._starttime_offset)
        cal_item = self.item

        if copytags:
            props = item.mapiobj.GetProps(copytags, 0)
        else: # XXX remove?
            props = [p.mapiobj for p in item.props() if p.proptag != PR_ICON_INDEX]

        message.mapiobj.SetProps(props)

        message[PR_MESSAGE_CLASS_W] = u'IPM.OLE.CLASS.{00061055-0000-0000-C000-000000000046}'
        message[PidLidExceptionReplaceTime] = basetime

        intended_busystatus = cal_item.get(PidLidIntendedBusyStatus) # XXX tentative? merge with modify_exc?
        if intended_busystatus is not None:
            message[PidLidBusyStatus] = intended_busystatus

        sub_type = cal_item.get(PidLidAppointmentSubType)
        if sub_type is not None:
            message[PidLidAppointmentSubType] = sub_type

        props = [
            SPropValue(PR_ATTACHMENT_FLAGS, 2), # XXX cannot find spec
            SPropValue(PR_ATTACHMENT_HIDDEN, True),
            SPropValue(PR_ATTACHMENT_LINKID, 0),
            SPropValue(PR_ATTACH_FLAGS, 0),
            SPropValue(PR_ATTACH_METHOD, ATTACH_EMBEDDED_MSG),
            SPropValue(PR_DISPLAY_NAME_W, u'Exception'),
        ]

        start = message.get(PidLidAppointmentStartWhole)
        if start is not None:
            start_local = unixtime(time.mktime(_utils._from_gmt(start, self._tz).timetuple())) # XXX why local??
            props.append(SPropValue(PR_EXCEPTION_STARTTIME, start_local))

        end = message.prop(PidLidAppointmentEndWhole).value
        if end is not None:
            end_local = unixtime(time.mktime(_utils._from_gmt(end, self._tz).timetuple())) # XXX why local??
            props.append(SPropValue(PR_EXCEPTION_ENDTIME, end_local))

        message._attobj.SetProps(props)
        if not create: # XXX php bug?
            props = props[:-2]
        message.mapiobj.SetProps(props)

        message.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
        message._attobj.SaveChanges(KEEP_OPEN_READWRITE)

    def _create_exception(self, basedate, item, copytags=None, merge=False):
        tz = item.get(PidLidTimeZoneStruct)
        cal_item = self.item

        # create embedded item
        message_flags = MSGFLAG_READ
        if item.get(PR_MESSAGE_FLAGS) == 0: # XXX wut/php compat
            message_flags |= MSGFLAG_UNSENT
        message = cal_item.create_item(message_flags, hidden=True)

        self._update_embedded(basedate, message, item, copytags, create=True)

        message[PidLidResponseStatus] = respDeclined | respOrganized # XXX php bug for merge case?
        if copytags:
            message[PidLidBusyStatus] = 0

        # copy over recipients (XXX check php delta stuff..)
        table = item.mapiobj.OpenProperty(PR_MESSAGE_RECIPIENTS, IID_IMAPITable, MAPI_UNICODE, 0)
        table.SetColumns(_meetingrequest.RECIP_PROPS, 0)
        recips = list(table.QueryRows(-1, 0))

        for recip in recips:
            flags = PpropFindProp(recip, PR_RECIPIENT_FLAGS)
            if not flags:
                recip.append(SPropValue(PR_RECIPIENT_FLAGS, recipExceptionalResponse | recipSendable))

        if copytags:
            for recip in recips:
                recip.append(SPropValue(PR_RECIPIENT_FLAGS, recipExceptionalDeleted | recipSendable))
                recip.append(SPropValue(PR_RECIPIENT_TRACKSTATUS, 0))

        organiser = _meetingrequest._organizer_props(message, item)
        if organiser and not merge: # XXX merge -> initialize?
            recips.insert(0, organiser)

        message.mapiobj.ModifyRecipients(MODRECIP_ADD, recips)

        message.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
        message._attobj.SaveChanges(KEEP_OPEN_READWRITE)
        cal_item.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

        # XXX attachments?

        # update blob
        self.deleted_instance_count += 1
        deldate = _utils._from_gmt(basedate, tz)
        deldate_val = _utils.unixtime_to_rectime(time.mktime(deldate.timetuple()))
        self._deleted_instance_dates.append(deldate_val)
        self._deleted_instance_dates.sort()

        self._modified_instance_count += 1
        moddate = message.prop(PidLidAppointmentStartWhole).value
        daystart = moddate - datetime.timedelta(hours=moddate.hour, minutes=moddate.minute) # XXX different approach in php? seconds?
        localdaystart = _utils._from_gmt(daystart, tz)
        moddate_val = _utils.unixtime_to_rectime(time.mktime(localdaystart.timetuple()))
        self._modified_instance_dates.append(moddate_val)
        self._modified_instance_dates.sort()

        exception = {}
        extended_exception = {}
        self._update_exception(cal_item, message, deldate_val, exception, extended_exception, copytags, create=True)
        self._exceptions.append(exception) # no evidence of sorting
        self._extended_exceptions.append(extended_exception)

        self._save()

        # update calitem
        self._update_calitem()

    def _modify_exception(self, basedate, item, copytags=None): # XXX 'item' too MR specific
        tz = item.get(PidLidTimeZoneStruct)
        cal_item = self.item

        # update embedded item
        for message in cal_item.items(): # XXX no cal_item? to helper
            replacetime = message.get(PidLidExceptionReplaceTime)
            if replacetime and replacetime.date() == basedate.date():
                self._update_embedded(basedate, message, item, copytags)

                icon_index = item.get(PR_ICON_INDEX)
                if not copytags and icon_index is not None:
                    message[PR_ICON_INDEX] = icon_index

                message._attobj.SaveChanges(KEEP_OPEN_READWRITE)
                break
        else:
            return # XXX exception

        if copytags: # XXX bug in php code? (setallrecipients, !empty..)
            message[PidLidBusyStatus] = 0

            table = message.mapiobj.OpenProperty(PR_MESSAGE_RECIPIENTS, IID_IMAPITable, MAPI_UNICODE, 0)
            table.SetColumns(_meetingrequest.RECIP_PROPS, 0)

            recips = list(table.QueryRows(-1, 0))
            for recip in recips:
                flags = PpropFindProp(recip, PR_RECIPIENT_FLAGS)
                if flags and flags.Value != (recipOrganizer | recipSendable):
                    flags.Value = recipExceptionalDeleted | recipSendable
                    trackstatus = PpropFindProp(recip, PR_RECIPIENT_TRACKSTATUS)
                    if not trackstatus:
                        recip.append(SPropValue(PR_RECIPIENT_TRACKSTATUS, 0))

            message.mapiobj.ModifyRecipients(MODRECIP_MODIFY, recips)

            message.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
            message._attobj.SaveChanges(KEEP_OPEN_READWRITE)
            cal_item.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

        # update blob
        basedate_val = _utils.unixtime_to_rectime(time.mktime(_utils._from_gmt(basedate, tz).timetuple()))

        startdate = _utils._from_gmt(message.prop(PidLidAppointmentStartWhole).value, tz)
        startdate_val = _utils.unixtime_to_rectime(time.mktime(startdate.timetuple()))

        for i, exception in enumerate(self._exceptions):
            if exception['original_start_date'] == basedate_val: # TODO offset, as below?
                current_startdate_val = exception['start_datetime'] - self._starttime_offset

                for j, val in enumerate(self._modified_instance_dates):
                    if val == current_startdate_val:
                        self._modified_instance_dates[j] = startdate_val - self._starttime_offset
                        self._modified_instance_dates.sort()
                        break

                extended_exception = self._extended_exceptions[i]
                self._update_exception(cal_item, message, basedate_val, exception, extended_exception, copytags, create=False)

        self._save()

        # update calitem
        self._update_calitem()

    def _create_exception2(self, basedate):
        # TODO merge with create_exception

        tz = self.item.get(PidLidTimeZoneStruct)
        cal_item = self.item

        # create embedded item
        message_flags = MSGFLAG_READ
        # UNUSED?
        message = cal_item.create_item(message_flags, hidden=True)

        basedate = _utils._from_gmt(basedate, tz)
        basedate_val = _utils.unixtime_to_rectime(time.mktime(basedate.timetuple())) - self._starttime_offset

        # update blob
        self.deleted_instance_count += 1
        self._deleted_instance_dates.append(basedate_val)
        self._deleted_instance_dates.sort()

        self._modified_instance_count += 1
        self._modified_instance_dates.append(basedate_val)
        self._modified_instance_dates.sort()

        exception = {
            'start_datetime': basedate_val + self._starttime_offset,
            'end_datetime': basedate_val + self._starttime_offset + 30, # TODO
            'original_start_date': basedate_val,
            'override_flags': 0,
        }
        self._exceptions.append(exception) # no evidence of sorting
        self._extended_exceptions.append({})

        self._save()

        # update calitem
        self._update_calitem()

    def _modify_exception2(self, basedate, subject=None, start=None, end=None, location=None):
        # TODO merge with modify_exception
        tz = self.item.get(PidLidTimeZoneStruct)
        cal_item = self.item

        # update embedded item
        for message in self.item.items(): # XXX no cal_item? to helper
            replacetime = message.get(PidLidExceptionReplaceTime)
            if replacetime and replacetime.date() == basedate.date():
                if subject is not None:
                    message.subject = subject
                # TODO set other args
                message._attobj.SaveChanges(KEEP_OPEN_READWRITE)
                break

        # update blob
        basedate_val = _utils.unixtime_to_rectime(time.mktime(_utils._from_gmt(basedate, tz).timetuple()))

        for i, exception in enumerate(self._exceptions):
            if exception['original_start_date'] in (basedate_val, basedate_val - self._starttime_offset): # TODO pick one
                extended_exception = self._extended_exceptions[i]
                break

        self._update_exception(cal_item, message, basedate_val, exception, extended_exception)
        self._save()

        # update calitem
        self._update_calitem()

    def _delete_exception(self, basedate, item, copytags):
        tz = item.get(PidLidTimeZoneStruct)

        basedate2 = _utils._from_gmt(basedate, tz)
        basedate_val = _utils.unixtime_to_rectime(time.mktime(basedate2.timetuple()))

        if self._is_exception(basedate):
            self._modify_exception(basedate, item, copytags)

            for i, exc in enumerate(self._exceptions):
                if exc['original_start_date'] == basedate_val:
                    break

            self._modified_instance_count -= 1
            self._modified_instance_dates = [m for m in self._modified_instance_dates if m != exc['start_datetime']]

            del self._exceptions[i]
            del self._extended_exceptions[i]

        else:
            self.deleted_instance_count += 1

            self._deleted_instance_dates.append(basedate_val)
            self._deleted_instance_dates.sort()

        self._save()
        self._update_calitem()

    def __unicode__(self):
        return u'Recurrence()'

    def __repr__(self):
        return _repr(self)


class Occurrence(object):
    """Occurrence class"""

    def __init__(self, item, start=None, end=None, subject=None, location=None, basedate_val=None, exception=False):
        self.item = item
        self._start = start
        self._end = end
        self._subject = subject
        self._location = location
        self._basedate_val = basedate_val
        self.exception = exception

    @property
    def start(self):
        return self._start or self.item.start

    @start.setter
    def start(self, value):
        self._update(start=value)
        self._start = value

    @property
    def end(self):
        return self._end or self.item.end

    @end.setter
    def end(self, value):
        self._update(end=value)
        self._end = value

    @property
    def location(self):
        return self._location or self.item.location

    @location.setter
    def location(self, value):
        self._update(location=value)
        self._location = value

    @property
    def subject(self):
        return self._subject or self.item.subject

    @subject.setter
    def subject(self, value):
        self._update(subject=value)
        self._subject = value

    def _update(self, **kwargs):
        if self.item.recurring:
            rec = self.item.recurrence
            basedate = datetime.datetime.fromtimestamp(_utils.rectime_to_unixtime(self._basedate_val))
            basedate = _utils._to_gmt(basedate, rec._tz)

            if rec._is_exception(basedate):
                rec._modify_exception2(basedate, **kwargs)
            else:
                rec._create_exception2(basedate)
                rec._modify_exception2(basedate, **kwargs)
        else:
            for (k, v) in kwargs.items():
                setattr(self.item, k, v)

    @property
    def entryid(self):
        return self._entryid()

    def _entryid(self, event=False):
        # cal item entryid plus basedate (zero if not recurring)
        flag = b'\x01' if event else b''
        eid = self.item._entryid or _bdec(self.item.entryid)
        basedate_val = self._basedate_val or 0

        return _benc(
            flag +
            _utils.pack_short(len(eid)) +
            eid +
            _utils.pack_long(basedate_val)
        )

    @property
    def eventid(self):
        # msgraph has both appointments and expanded appointments under
        # /events, so we need an identier which can be used for both.
        return self._entryid(True)

    def __getattr__(self, x):
        return getattr(self.item, x)

    def __unicode__(self):
        return u'Occurrence(%s)' % self.subject

    def __repr__(self):
        return _repr(self)
