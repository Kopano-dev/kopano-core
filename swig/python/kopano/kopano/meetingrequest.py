"""
Part of the high-level python bindings for Kopano

Copyright 2017 - Kopano and its licensors (see LICENSE file for details)
"""

import datetime
import struct

from MAPI import (
    MAPI_UNICODE, MODRECIP_MODIFY, KEEP_OPEN_READWRITE
)

from MAPI.Tags import (
    PR_DISPLAY_NAME_W, PR_RECIPIENT_TRACKSTATUS, PR_MESSAGE_RECIPIENTS,
    IID_IMAPITable, IID_IMessage,
)

from MAPI.Defs import PpropFindProp

from MAPI.Struct import SPropValue

from .compat import repr as _repr, hex as _hex
from .errors import Error

class MeetingRequest(object):
    def __init__(self, item):
        self.item = item

    @property
    def calendar_item(self):
        """ Global calendar item :class:`item <Item>` """

        goid = self.item.prop('meeting:35').value
        for item in self.item.store.calendar: # XXX restriction
            if item.prop('meeting:35').value == goid:
                return item

    @property
    def basedate(self):
        """ Exception date """

        blob = self.item.prop('meeting:3').value
        y, m, d = struct.unpack_from('>HBB', blob, 16)
        if (y, m, d) != (0, 0, 0):
            return datetime.datetime(y, m, d)

    @property
    def update_counter(self):
        """ Update counter """

        return self.item.prop('appointment:33281').value

    @property
    def track_status(self):
        if self.item.message_class == 'IPM.Schedule.Meeting.Resp.Pos':
            return 3 # XXX hardcoded
        elif self.item.message_class == 'IPM.Schedule.Meeting.Resp.Tent':
            return 2
        elif self.item.message_class == 'IPM.Schedule.Meeting.Resp.Neg':
            return 4

    def accept(self, tentative=False, respond=True):
        """ Accept meeting request

        :param tentative: accept tentatively?
        :param respond: send response message?
        """

        if tentative:
            self._accept('IPM.Schedule.Meeting.Resp.Tent', respond=respond)
        else:
            self._accept('IPM.Schedule.Meeting.Resp.Pos', respond=respond)

    def decline(self, respond=True):
        """ Decline meeting request

        :param respond: send response message?
        """
        self._accept('IPM.Schedule.Meeting.Resp.Neg', respond=respond)

    def _accept(self, message_class, respond=True):
        if not self.is_request:
            raise Error('item is not a meeting request')

        cal_item = self.calendar_item
        calendar = self.item.store.calendar # XXX

        if cal_item:
            if self.update_counter <= cal_item.meetingrequest.update_counter:
                raise Error('trying to accept out-of-date meeting request')
            calendar.delete(cal_item)

        cal_item = self.item.copy(self.item.store.calendar)
        cal_item.message_class = 'IPM.Appointment'

        if respond:
            response = self.item.copy(self.item.store.outbox)
            response.subject = 'Accepted: ' + self.item.subject
            response.message_class = message_class
            response.to = self.item.server.user(email=self.item.from_.email) # XXX
            response.from_ = self.item.store.user # XXX slow?
            response.send()

    @property
    def is_request(self):
        return self.item.message_class == 'IPM.Schedule.Meeting.Request'

    @property
    def is_response(self):
        return self.item.message_class.startswith('IPM.Schedule.Meeting.Resp.')

    @property
    def is_cancellation(self):
        return self.item.message_class == 'IPM.Schedule.Meeting.Canceled'

    def process_cancellation(self):
        """ Process meeting request cancellation """

        cal_item = self.calendar_item
        calendar = self.item.store.calendar # XXX

        # XXX basedate, merging

        if cal_item: # XXX merge delete/copy with accept
            calendar.delete(cal_item)

        cal_item = self.item.copy(self.item.store.calendar)
        cal_item.message_class = 'IPM.Appointment'

    def process_response(self):
        """ Process meeting request response """

        if not self.is_response:
            raise Error('item is not a meeting request response')

        cal_item = self.calendar_item
        basedate = self.basedate

        # modify calendar item or embedded message (in case of exception)
        attach = None
        if basedate:
            for message in cal_item.embedded_items():
                if message.prop('appointment:33320').value.date() == basedate.date(): # XXX date
                    attach = message._attobj # XXX
                    message = message.mapiobj
                    break
        else:
            message = cal_item.mapiobj

        # update recipient track status # XXX partially to recurrence.py
        table = message.OpenProperty(PR_MESSAGE_RECIPIENTS, IID_IMAPITable, MAPI_UNICODE, 0)
        rows = table.QueryRows(-1, 0)
        for row in rows:
            disp = PpropFindProp(row, PR_DISPLAY_NAME_W)
            if disp.Value == self.item.from_.name: # XXX resolving
                row.append(SPropValue(PR_RECIPIENT_TRACKSTATUS, self.track_status)) # XXX append

        message.ModifyRecipients(MODRECIP_MODIFY, rows)

        # save all the things
        message.SaveChanges(KEEP_OPEN_READWRITE)
        if attach:
            attach.SaveChanges(KEEP_OPEN_READWRITE)
            cal_item.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def __unicode__(self):
        return u'MeetingRequest()'

    def __repr__(self):
        return _repr(self)
