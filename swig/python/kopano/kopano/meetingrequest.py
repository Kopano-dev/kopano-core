"""
Part of the high-level python bindings for Kopano

Copyright 2017 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI import (
    MAPI_UNICODE, MODRECIP_MODIFY, KEEP_OPEN_READWRITE
)

from MAPI.Tags import (
    PR_DISPLAY_NAME_W, PR_RECIPIENT_TRACKSTATUS, PR_MESSAGE_RECIPIENTS,
    IID_IMAPITable, IID_IMessage,
)

from MAPI.Defs import PpropFindProp

from MAPI.Struct import SPropValue

from .compat import repr as _repr
from .errors import Error

class MeetingRequest(object):
    def __init__(self, item):
        self.item = item

    @property
    def calendar_item(self):
        """ Matching calendar :class:`item <Item>` """

        goid = self.item.prop('meeting:3').value
        for item in self.item.store.calendar: # XXX restriction
            if item.prop('meeting:3').value == goid:
                return item

    @property
    def update_counter(self):
        return self.item.prop('appointment:33281').value

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
        if self.item.message_class != 'IPM.Schedule.Meeting.Request':
            raise Error('trying to accept non meeting request')

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

    def process(self):
        """ Process meeting request response """

        if self.item.message_class == 'IPM.Schedule.Meeting.Resp.Pos':
            track_status = 3 # XXX hardcoded
        elif self.item.message_class == 'IPM.Schedule.Meeting.Resp.Tent':
            track_status = 2
        elif self.item.message_class == 'IPM.Schedule.Meeting.Resp.Neg':
            track_status = 4
        else:
            raise Error('trying to process non meeting request reponse')

        cal_item = self.calendar_item
        table = cal_item.mapiobj.OpenProperty(PR_MESSAGE_RECIPIENTS, IID_IMAPITable, MAPI_UNICODE, 0)
        rows = list(table.QueryRows(-1, 0))
        for row in rows:
            disp = PpropFindProp(row, PR_DISPLAY_NAME_W)
            if disp.Value == self.item.from_.name: # XXX resolving
                row.append(SPropValue(PR_RECIPIENT_TRACKSTATUS, track_status))
        cal_item.mapiobj.ModifyRecipients(MODRECIP_MODIFY, rows)
        cal_item.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def __unicode__(self):
        return u'MeetingRequest()'

    def __repr__(self):
        return _repr(self)
