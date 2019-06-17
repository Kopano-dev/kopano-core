# SPDX-License-Identifier: AGPL-3.0-only
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
    PR_EXCEPTION_STARTTIME, PR_EXCEPTION_ENDTIME, PR_SUBJECT_W,
    PR_NORMALIZED_SUBJECT_W, PR_ATTACHMENT_LINKID, PR_ICON_INDEX,
    PR_MESSAGE_RECIPIENTS, IID_IMAPITable, PR_RECIPIENT_FLAGS,
    PR_MESSAGE_FLAGS, PR_RECIPIENT_TRACKSTATUS,
    recipSendable, recipExceptionalResponse, recipExceptionalDeleted,
    recipOrganizer, respOrganized, respDeclined,
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
import dateutil.tz

from .compat import (
    repr as _repr, benc as _benc, bdec as _bdec, fake_unicode as _unicode
)
from .errors import (
    NotSupportedError, NotFoundError, ArgumentError,
)
from .defs import (
    ARO_SUBJECT, ARO_MEETINGTYPE, ARO_REMINDERDELTA, ARO_REMINDERSET,
    ARO_LOCATION, ARO_BUSYSTATUS, ARO_ATTACHMENT, ARO_SUBTYPE,
    ARO_APPTCOLOR, ASF_CANCELED, FB_STATUS, STATUS_FB
)

LOCAL = dateutil.tz.tzlocal()

from .attendee import Attendee

if sys.hexversion >= 0x03000000:
    try:
        from . import utils as _utils
    except ImportError: # pragma: no cover
        _utils = sys.modules[__package__ + '.utils']

    from . import meetingrequest as _meetingrequest
    from . import timezone as _timezone
else: # pragma: no cover
    import utils as _utils
    import meetingrequest as _meetingrequest
    import timezone as _timezone

SHORT, LONG = 2, 4

from .pidlid import (
    PidLidSideEffects, PidLidSmartNoAttach,
    PidLidReminderSet, PidLidReminderSignalTime, PidLidReminderDelta,
    PidLidBusyStatus, PidLidExceptionReplaceTime,
    PidLidResponseStatus, PidLidTimeZoneStruct, PidLidAppointmentRecur,
    PidLidLocation, PidLidAppointmentSubType, PidLidAppointmentColor,
    PidLidIntendedBusyStatus, PidLidAppointmentStartWhole,
    PidLidAppointmentEndWhole, PidLidIsRecurring, PidLidClipStart,
    PidLidClipEnd, PidLidAppointmentStateFlags,
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
        self._tzinfo = item.tzinfo

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
        else:
            raise ArgumentError('invalid recurrence pattern: %s' % value) # TODO add more such checks

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
            self._pattern_type_specific[0] = pts

        # TODO fill in

    @property
    def first_weekday(self):
        weekdays = {0: 'sunday', 1: 'monday', 2: 'tuesday', 3: 'wednesday', 4: 'thursday', 5: 'friday', 6: 'saturday'}
        return weekdays[self._first_dow]

    @property
    def month(self):
        if self._recur_frequency == FREQ_YEAR:
            return self.start.month # TODO remove dependency on self.start being set

    @month.setter
    def month(self, value):
        if self._recur_frequency == FREQ_YEAR:
            self.start = self.start.replace(month=value)

    @property
    def monthday(self):
        if self._pattern_type in (PATTERN_MONTHLY, PATTERN_HJMONTHLY):
            return self._pattern_type_specific[0]

    @monthday.setter
    def monthday(self, value):
        if self._pattern_type in (PATTERN_MONTHLY, PATTERN_HJMONTHLY):
            self._pattern_type_specific[0] = value

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

    @index.setter
    def index(self, value):
        try:
            self._pattern_type_specific[1] = {
                u'first': 1,
                u'second': 2,
                u'third': 3,
                u'fourth': 4,
                u'last': 5,
            }[_unicode(value)]
        except KeyError:
            raise ArgumentError('invalid recurrence index: %s' % value)

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
        if self._recur_frequency == FREQ_YEAR:
            self._period = value*12
        elif self._pattern_type == PATTERN_DAILY:
            self._period = value * (24 * 60)
        else:
            self._period = value

    @property
    def range_type(self):
        if self._end_type == 0x2021:
            return 'end_date'
        elif self._end_type == 0x2022:
            return 'count'
        elif self._end_type in (0x2023, 0xFFFFFFFF):
            return 'forever'

    @range_type.setter
    def range_type(self, value):
        if value == 'end_date':
            self._end_type = 0x2021
        elif value == 'count':
            self._end_type = 0x2022
        elif value == 'forever':
            self._end_type = 0x2023
        else:
            raise ArgumentError('invalid recurrence range type: %s' % value)

    @property
    def count(self):
        return self._occurrence_count

    @count.setter
    def count(self, value):
        self._occurrence_count = value

    def occurrences(self, start=None, end=None): # XXX fit-to-period
        recurrences = self.recurrences
        if start and end:
            start = _timezone._tz2(start, LOCAL, self._tzinfo)
            end = _timezone._tz2(end, LOCAL, self._tzinfo)
            recurrences = recurrences.between(start, end)

        start_exc_ext = {}
        for exc, ext in zip(self._exceptions, self._extended_exceptions):
            start_exc_ext[exc['start_datetime']] = exc, ext

        for d in recurrences:
            startdatetime_val = _utils.unixtime_to_rectime(calendar.timegm(d.timetuple()))

            subject = self.item.subject
            location = self.item.location
            busystatus = self.item.busystatus
            exception = False
            if startdatetime_val in start_exc_ext:
                exc, ext = start_exc_ext[startdatetime_val]
                minutes = exc['end_datetime'] - startdatetime_val
                subject = ext.get('subject', subject)
                location = ext.get('location', location)
                basedate_val = exc['original_start_date']
                if 'busy_status' in exc:
                    busystatus = FB_STATUS[exc['busy_status']]
                exception = True
            else:
                minutes = self._endtime_offset - self._starttime_offset
                basedate_val = startdatetime_val

            # NOTE(longsleep): Start and end of occurrences comes in as event timezone. Pyko expects
            # local time so start and end is converted to LOCAL.
            if self.item.tzinfo:
                d = _timezone._tz2(d, self.item.tzinfo, _timezone.LOCAL)
            e = d + datetime.timedelta(minutes=minutes)

            occ = Occurrence(self.item, d, e, subject, location, busystatus=busystatus, basedate_val=basedate_val, exception=exception)
            if (not start or occ.end > start) and (not end or occ.start < end):
                yield occ

    def occurrence(self, entryid):
        entryid = _bdec(entryid)
        pos = 2 + _utils.unpack_short(entryid, 0)
        basedate_val = _utils.unpack_long(entryid, pos)

        start = end = subject = location = None

        for exc, ext in zip(self._exceptions, self._extended_exceptions):
            if exc['original_start_date'] in (basedate_val, basedate_val - self._starttime_offset): # TODO pick one
                start = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(exc['start_datetime']))
                end = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(exc['end_datetime']))
                if exc['override_flags'] & ARO_SUBJECT:
                    subject = ext.get('subject')
                if exc['override_flags'] & ARO_LOCATION:
                    location = ext.get('location')
                break
        else:
            # TODO check that date is (still) valid
            start = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(basedate_val))
            end = start + datetime.timedelta(minutes=self._endtime_offset - self._starttime_offset)

        # NOTE(longsleep): Start and end of occurrences comes in as event timezone. Pyko expects
        # local time so start and end is converted to LOCAL.
        if self.item.tzinfo:
            start = _timezone._tz2(start, self.item.tzinfo, _timezone.LOCAL)
            end = _timezone._tz2(end, self.item.tzinfo, _timezone.LOCAL)

        return Occurrence(self.item, start, end, subject, location, basedate_val=basedate_val)

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
        rec._pattern_type_specific = [0, 0]
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
        rec._has_extended = True

        rec._starttime_offset = 0
        rec._endtime_offset = 0
        rec._update_offsets(save=False)

        rec._save()

    def _update_offsets(self, save=True):
        item = self.item
        tzinfo = item.tzinfo
        start = item.start
        if start:
            if tzinfo:
                # Convert to item timezone to ensure the offset is correct.
                start = _timezone._tz2(start, LOCAL, tzinfo)
            self._starttime_offset = start.hour * 60 + start.minute
        end = item.end
        if end:
            if tzinfo:
                # Convert to item timezone to ensure the offset is correct.
                end = _timezone._tz2(end, LOCAL, tzinfo)
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

        self._pattern_type_specific = [0, 0]
        if self._pattern_type != PATTERN_DAILY:
            self._pattern_type_specific[0] = _utils.unpack_long(value, pos)
            pos += LONG
            if self._pattern_type in (PATTERN_MONTHNTH, PATTERN_HJMONTHNTH):
                self._pattern_type_specific[1] = _utils.unpack_long(value, pos)
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

        # according to the specs, the number of exceptions and extended exceptions
        # should always be equal, but some clients apparently do not respect this
        self._has_extended = True
        if pos==len(value):
            self._extended_exceptions = [{} for ext in self._exceptions]
            self._has_extended = False
            return

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

        if self._pattern_type != PATTERN_DAILY:
            data += struct.pack('<I', self._pattern_type_specific[0])
            if self._pattern_type in (PATTERN_MONTHNTH, PATTERN_HJMONTHNTH):
                data += struct.pack('<I', self._pattern_type_specific[1])

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

        if self._has_extended:
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

    # TODO add timezone-awareness flag to pyko..
    @property
    def start(self):
        """ Start of recurrence range (within recurrence timezone) """
        return datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(self._start_date))

    @start.setter
    def start(self, value):
        self._start_date = _utils.unixtime_to_rectime(calendar.timegm(value.timetuple()))

        self.item[PidLidClipStart] = self.start

    @property
    def end(self):
        """ End of recurrence range (within recurrence timezone) """
        return datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(self._end_date))

    @end.setter
    def end(self, value):
        self._end_date = _utils.unixtime_to_rectime(calendar.timegm(value.timetuple()))

        self.item[PidLidClipEnd] = self.end

    # TODO functionality below here should be refactored or not visible

    @property
    def recurrences(self): # TODO rename to _recurrences and/or rrule?
        rule = rruleset()

        start = self.start + datetime.timedelta(minutes=self._starttime_offset)
        if self.range_type == u'forever':
            end = None
        else:
            end = self.end + datetime.timedelta(minutes=self._endtime_offset)
            # FIXME: add one day, so that we don't miss the last recurrence, since the end date is for example 11-3-2015 on 1:00
            # But the recurrence is on 8:00 that day and we should include it.
            if self._pattern_type == PATTERN_WEEKLY:
                end += datetime.timedelta(days=1)

        # TODO for occurrence count?

        if self._pattern_type == PATTERN_DAILY:
            rule.rrule(rrule(DAILY, dtstart=start, until=end, interval=self._period // (24 * 60)))

        elif self._pattern_type == PATTERN_WEEKLY:
            byweekday = () # Set
            for index, week in RRULE_WEEKDAYS.items():
                if (self._pattern_type_specific[0] >> index ) & 1:
                    byweekday += (week,)
            rule.rrule(rrule(WEEKLY, wkst=start.weekday(), dtstart=start, until=end, byweekday=byweekday, interval=self._period))

        elif self._pattern_type == PATTERN_MONTHLY:
            # X Day of every Y month(s)
            # The Xnd Y (day) of every Z Month(s)
            rule.rrule(rrule(MONTHLY, dtstart=start, until=end, bymonthday=self._pattern_type_specific[0], interval=self._period))
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
            rule.rrule(rrule(MONTHLY, dtstart=start, until=end, interval=self._period, byweekday=byweekday))

        # add exceptions
        exc_starts = set()
        for exception in self._exceptions:
            exc_start = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(exception['start_datetime']))
            rule.rdate(exc_start)
            exc_starts.add(exc_start)

        # Remove deleted ocurrences (skip added exceptions)
        for del_date_val in self._deleted_instance_dates:
            del_date = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(del_date_val))
            del_date = datetime.datetime(del_date.year, del_date.month, del_date.day, self._starttime_offset//60, self._starttime_offset%60)
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

    def _update_exception(self, cal_item, item, basedate_val, exception, extended_exception, copytags=None, create=False, orig_item=None, **kwargs): # XXX kill copytags, create args, just pass all properties as in php
        # TODO get start/end from cal_item if not in item?

        startdate = kwargs.get('start')
        if orig_item or (startdate is None and 'start_datetime' not in exception):
            startdate = item.get(PidLidAppointmentStartWhole)

        if startdate is not None:
            startdate_val = _utils.unixtime_to_rectime(time.mktime(_timezone._from_utc(startdate, self._tzinfo).timetuple()))
            exception['start_datetime'] = startdate_val

        enddate = kwargs.get('end')
        if orig_item or (enddate is None and 'end_datetime' not in exception):
            enddate = item.get(PidLidAppointmentEndWhole)

        if enddate is not None:
            enddate_val = _utils.unixtime_to_rectime(time.mktime(_timezone._from_utc(enddate, self._tzinfo).timetuple()))
            exception['end_datetime'] = enddate_val

        exception['original_start_date'] = basedate_val # TODO why set again?
        exception['override_flags'] = 0

        extended = False

        subject = kwargs.get('subject')
        if orig_item or (subject is None and 'subject' not in exception):
            subject = item.get(PR_NORMALIZED_SUBJECT_W)
        if subject is not None or 'subject' in exception:
            exception['override_flags'] |= ARO_SUBJECT
        if subject is not None:
            exception['subject'] = subject.encode('cp1252', 'replace')
            extended = True
            extended_exception['subject'] = subject

        location = kwargs.get('location')
        if orig_item or (location is None and 'location' not in exception):
            location = item.get(PidLidLocation)
        if location is not None or 'location' in exception:
            exception['override_flags'] |= ARO_LOCATION
        if location is not None:
            exception['location'] = location.encode('cp1252', 'replace')
            extended = True
            extended_exception['location'] = location

        # skip ARO_MEETINGTYPE (like php)

        reminder_delta = item.get(PidLidReminderDelta)
        if reminder_delta is not None:
            exception['override_flags'] |= ARO_REMINDERDELTA
            exception['reminder_delta'] = reminder_delta

        reminder_set = item.get(PidLidReminderSet)
        if reminder_set is not None:
            exception['override_flags'] |= ARO_REMINDERSET
            exception['reminder_set'] = reminder_set

        busy_status = kwargs.get('busystatus') or item.busystatus
        if busy_status is not None:
            exception['override_flags'] |= ARO_BUSYSTATUS
            exception['busy_status'] = STATUS_FB[busy_status]

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
            if startdate:
                extended_exception['start_datetime'] = startdate_val
            if enddate:
                extended_exception['end_datetime'] = enddate_val
            extended_exception['original_start_date'] = basedate_val

    def _update_calitem(self):
        cal_item = self.item

        cal_item[PidLidSideEffects] = 3441 # XXX spec, check php
        cal_item[PidLidSmartNoAttach] = True

        # reminder
        if cal_item.get(PidLidReminderSet) and cal_item.get(PidLidReminderDelta):
            next_date = self.recurrences.after(datetime.datetime.now(self._tzinfo).replace(tzinfo=None))
            if next_date:
                next_date = _timezone._to_utc(next_date, self._tzinfo)
                dueby = next_date - datetime.timedelta(minutes=cal_item.get(PidLidReminderDelta))
                cal_item[PidLidReminderSignalTime] = dueby
            else:
                cal_item[PidLidReminderSet] = False
                cal_item[PidLidReminderSignalTime] = datetime.datetime.fromtimestamp(0x7ff00000)

    def _update_embedded(self, basedate, message, item=None, copytags=None, create=False, **kwargs):
        basetime = basedate + datetime.timedelta(minutes=self._starttime_offset)
        cal_item = self.item

        item2 = item or cal_item
        if copytags:
            props = item2.mapiobj.GetProps(copytags, 0)
            message.mapiobj.SetProps(props)
        elif not kwargs:
            props = [p.mapiobj for p in item2.props() if p.proptag != PR_ICON_INDEX]
            message.mapiobj.SetProps(props)
            message.recurring = False

        if not item:
            if kwargs:
                if 'start' in kwargs:
                    message[PidLidAppointmentStartWhole] = kwargs['start']
                if 'end' in kwargs:
                    message[PidLidAppointmentEndWhole] = kwargs['end']
            else:
                message[PidLidAppointmentStartWhole] = basedate + datetime.timedelta(minutes=self._starttime_offset)
                message[PidLidAppointmentEndWhole] = basedate + datetime.timedelta(minutes=self._endtime_offset)

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

        if 'subject' in kwargs:
            props.append(SPropValue(PR_SUBJECT_W, kwargs['subject']))

        if 'location' in kwargs:
            message[PidLidLocation] = kwargs['location']

        if 'color' in kwargs:
            message.color = kwargs['color']

        if item is None: # TODO pick up kwargs['start/end']
            start = basetime
            end = basetime + datetime.timedelta(minutes=self._endtime_offset-self._starttime_offset)
        else:
            start = message.get(PidLidAppointmentStartWhole)
            end = message.prop(PidLidAppointmentEndWhole).value

        if start is not None:
            start_local = unixtime(time.mktime(_timezone._from_utc(start, self._tzinfo).timetuple())) # XXX why local??
            props.append(SPropValue(PR_EXCEPTION_STARTTIME, start_local))

        if end is not None:
            end_local = unixtime(time.mktime(_timezone._from_utc(end, self._tzinfo).timetuple())) # XXX why local??
            props.append(SPropValue(PR_EXCEPTION_ENDTIME, end_local))

        message._attobj.SetProps(props)
        if not create: # XXX php bug?
            props = props[:-2]
        message.mapiobj.SetProps(props)

        if 'canceled' in kwargs:
            message[PidLidAppointmentStateFlags] |= ASF_CANCELED

        _utils._save(message.mapiobj)
        _utils._save(message._attobj)

    def _create_exception(self, basedate, item=None, copytags=None, merge=False, recips_from=None):
        cal_item = self.item

        # create embedded item
        message_flags = MSGFLAG_READ
        if item and item.get(PR_MESSAGE_FLAGS) == 0: # XXX wut/php compat
            message_flags |= MSGFLAG_UNSENT
        message = cal_item.create_item(message_flags, hidden=True)

        self._update_embedded(basedate, message, item, copytags, create=True)

        message[PidLidResponseStatus] = respDeclined | respOrganized # XXX php bug for merge case?
        if copytags:
            message[PidLidBusyStatus] = 0

        # copy over recipients (XXX check php delta stuff..)
        item = item or recips_from
        if item:
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

            organiser = _meetingrequest._organizer_props(cal_item, item)
            if organiser and not merge: # XXX merge -> initialize?
                recips.insert(0, organiser)

            message.mapiobj.ModifyRecipients(MODRECIP_ADD, recips)

        _utils._save(message.mapiobj)
        _utils._save(message._attobj)
        _utils._save(cal_item.mapiobj)

        # XXX attachments?

        # update blob
        self.deleted_instance_count += 1
        deldate = _timezone._from_utc(basedate, self._tzinfo)
        deldate_val = _utils.unixtime_to_rectime(time.mktime(deldate.timetuple()))
        self._deleted_instance_dates.append(deldate_val)
        self._deleted_instance_dates.sort()

        self._modified_instance_count += 1
        moddate = message.prop(PidLidAppointmentStartWhole).value
        daystart = moddate - datetime.timedelta(hours=moddate.hour, minutes=moddate.minute) # XXX different approach in php? seconds?
        localdaystart = _timezone._from_utc(daystart, self._tzinfo)
        moddate_val = _utils.unixtime_to_rectime(time.mktime(localdaystart.timetuple()))
        self._modified_instance_dates.append(moddate_val)
        self._modified_instance_dates.sort()

        exception = {}
        extended_exception = {}
        self._update_exception(cal_item, message, deldate_val, exception, extended_exception, copytags, create=True, orig_item=item)
        self._exceptions.append(exception) # no evidence of sorting
        self._extended_exceptions.append(extended_exception)

        self._save()

        # update calitem
        self._update_calitem()

    def _modify_exception(self, basedate, item=None, copytags=None, **kwargs): # XXX 'item' too MR specific
        cal_item = self.item

        # update embedded item
        for message in cal_item.items(): # XXX no cal_item? to helper
            replacetime = message.get(PidLidExceptionReplaceTime)
            if replacetime and replacetime.date() == basedate.date():
                self._update_embedded(basedate, message, item, copytags, **kwargs)

                if item:
                    icon_index = item.get(PR_ICON_INDEX)
                    if not copytags and icon_index is not None:
                        message[PR_ICON_INDEX] = icon_index

                _utils._save(message._attobj)
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

            _utils._save(message.mapiobj)
            _utils._save(message._attobj)
            _utils._save(cal_item.mapiobj)

        # update blob
        basedate_val = _utils.unixtime_to_rectime(time.mktime(_timezone._from_utc(basedate, self._tzinfo).timetuple()))

        startdate = _timezone._from_utc(message.prop(PidLidAppointmentStartWhole).value, self._tzinfo)
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
                self._update_exception(cal_item, message, basedate_val, exception, extended_exception, copytags, create=False, orig_item=item, **kwargs)

        self._save()

        # update calitem
        self._update_calitem()

    def _delete_exception(self, basedate, item, copytags):
        basedate2 = _timezone._from_utc(basedate, self._tzinfo)
        basedate_val = _utils.unixtime_to_rectime(time.mktime(basedate2.timetuple()))

        if self._is_exception(basedate):
            self._modify_exception(basedate, item, copytags)

            for i, exc in enumerate(self._exceptions):
                if exc['original_start_date'] == basedate_val:
                    break

            self._modified_instance_count -= 1
            self._modified_instance_dates = [m for m in self._modified_instance_dates if m != exc['original_start_date']]

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

    def __init__(self, item, start=None, end=None, subject=None, location=None, busystatus=None, basedate_val=None, exception=False):
        self.item = item
        self._start = start
        self._end = end
        self._subject = subject
        self._location = location
        self._busystatus = busystatus
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
        value = _unicode(value)
        self._update(location=value)
        self._location = value

    @property
    def subject(self):
        return self._subject or self.item.subject

    @subject.setter
    def subject(self, value):
        value = _unicode(value)
        self._update(subject=value)
        self._subject = value

    @property
    def color(self): # property used by old clients
        return self._color or self.item.color

    @color.setter
    def color(self, value):
        self._update(color=value)
        self._color = value

    @property
    def busystatus(self):
        return self._busystatus or self.item.busystatus

    @busystatus.setter
    def busystatus(self, value):
        self._update(busystatus=value)
        self._busystatus = value

    def _update(self, **kwargs):
        if self.item.recurring:
            rec = self.item.recurrence
            basedate = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(self._basedate_val))
            basedate = basedate.replace(hour=0, minute=0)

            if rec._is_exception(basedate):
                rec._modify_exception(basedate, **kwargs) # TODO does too much - only need to update kwargs!
            else:
                rec._create_exception(basedate, recips_from=self.item)
                rec._modify_exception(basedate, **kwargs)

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

    def attendees(self):
        if self.item.recurring:
            rec = self.item.recurrence
            basedate = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(self._basedate_val))
            basedate = basedate.replace(hour=0, minute=0)
            message = rec._exception_message(basedate)
            if message:
                for row in message.table(PR_MESSAGE_RECIPIENTS):
                    yield Attendee(self.item.server, row)
                return
            # TODO else?

        for attendee in self.item.attendees():
            yield attendee

    def create_attendee(self, type_, addr):
        if self.item.recurring:
            rec = self.item.recurrence
            basedate = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(self._basedate_val))
            basedate = basedate.replace(hour=0, minute=0)
            message = rec._exception_message(basedate)
            if message:
                message.create_attendee(type_, addr)
                _utils._save(message._attobj)
                return
            # TODO else?
        else:
            self.item.create_attendee(type_, addr)

    def cancel(self):
        if self.item.recurring:
            self._update(canceled=True)
        else:
            self.item.cancel()

    @property
    def canceled(self):
        if self.item.recurring:
            rec = self.item.recurrence
            basedate = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(self._basedate_val))
            basedate = basedate.replace(hour=0, minute=0)
            message = rec._exception_message(basedate)
            if message:
                return message.canceled

        return self.item.canceled

    def send(self, copy_to_sentmail=True):
        if self.item.recurring:
            basedate = datetime.datetime.utcfromtimestamp(_utils.rectime_to_unixtime(self._basedate_val))
            message = self.item.recurrence._exception_message(basedate)
            if message:
                message.store = self.store
                to = list(message.to)
                if not to:
                    message.to = self.item.to # TODO don't change message on send
                message.send(copy_to_sentmail=copy_to_sentmail, _basedate=basedate, cal_item=self.item)
            else:
                self.item.send(copy_to_sentmail=copy_to_sentmail, _basedate=basedate, cal_item=self.item)
        else:
            self.item.send(copy_to_sentmail)

    def __getattr__(self, x): # TODO get from exception message by default? eg subject, attendees..
        return getattr(self.item, x)

    def __unicode__(self):
        return u'Occurrence(%s)' % self.subject

    def __repr__(self):
        return _repr(self)
