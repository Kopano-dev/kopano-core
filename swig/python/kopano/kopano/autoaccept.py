"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI.Tags import (
    PR_FREEBUSY_ENTRYIDS, PR_PROCESS_MEETING_REQUESTS,
    PR_DECLINE_CONFLICTING_MEETING_REQUESTS,
    PR_DECLINE_RECURRING_MEETING_REQUESTS
)
from MAPI.Defs import HrGetOneProp
from MAPI.Struct import SPropValue
from MAPI import MAPI_MODIFY, KEEP_OPEN_READWRITE

class AutoAccept(object):
    def __init__(self, store):
        fbeid = store.root.prop(PR_FREEBUSY_ENTRYIDS).value[1]
        self._fb = store.mapiobj.OpenEntry(fbeid, None, MAPI_MODIFY)
        self.store = store

    @property
    def enabled(self):
        return HrGetOneProp(self._fb, PR_PROCESS_MEETING_REQUESTS).Value

    @enabled.setter
    def enabled(self, b):
        self._fb.SetProps([SPropValue(PR_PROCESS_MEETING_REQUESTS, b)])
        self._fb.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def conflicts(self):
        return not HrGetOneProp(self._fb, PR_DECLINE_CONFLICTING_MEETING_REQUESTS).Value

    @conflicts.setter
    def conflicts(self, b):
        self._fb.SetProps([SPropValue(PR_DECLINE_CONFLICTING_MEETING_REQUESTS, not b)])
        self._fb.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def recurring(self):
        return not HrGetOneProp(self._fb, PR_DECLINE_RECURRING_MEETING_REQUESTS).Value

    @recurring.setter
    def recurring(self, b):
        self._fb.SetProps([SPropValue(PR_DECLINE_RECURRING_MEETING_REQUESTS, not b)])
        self._fb.SaveChanges(KEEP_OPEN_READWRITE)
