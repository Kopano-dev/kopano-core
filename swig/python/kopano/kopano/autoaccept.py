# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)
"""

import sys

from MAPI.Tags import (
    PR_FREEBUSY_ENTRYIDS, PR_PROCESS_MEETING_REQUESTS,
    PR_DECLINE_CONFLICTING_MEETING_REQUESTS,
    PR_DECLINE_RECURRING_MEETING_REQUESTS
)
from MAPI.Defs import HrGetOneProp
from MAPI.Struct import SPropValue
from MAPI import MAPI_MODIFY, KEEP_OPEN_READWRITE

if sys.hexversion >= 0x03000000:
    try:
        from . import utils as _utils
    except ImportError: # pragma: no cover
        _utils = sys.modules[__package__ + '.utils']
else: # pragma: no cover
    import utils as _utils

class AutoAccept(object):
    """AutoAccept class"""

    def __init__(self, store):
        fbeid = store.root.prop(PR_FREEBUSY_ENTRYIDS).value[1]
        self._fb = store.mapiobj.OpenEntry(fbeid, None, MAPI_MODIFY)
        self.store = store

    @property
    def enabled(self):
        """Auto-accept is enabled."""
        return HrGetOneProp(self._fb, PR_PROCESS_MEETING_REQUESTS).Value

    @enabled.setter
    def enabled(self, b):
        self._fb.SetProps([SPropValue(PR_PROCESS_MEETING_REQUESTS, b)])
        _utils._save(self._fb)

    @property
    def conflicts(self):
        """Conflicting appointments are accepted."""
        prop = HrGetOneProp(self._fb, PR_DECLINE_CONFLICTING_MEETING_REQUESTS)
        return not prop.Value

    @conflicts.setter
    def conflicts(self, b):
        props = [SPropValue(PR_DECLINE_CONFLICTING_MEETING_REQUESTS, not b)]
        self._fb.SetProps(props)
        _utils._save(self._fb)

    @property
    def recurring(self):
        """Recurring appointments are accepted."""
        prop = HrGetOneProp(self._fb, PR_DECLINE_RECURRING_MEETING_REQUESTS)
        return not prop.Value

    @recurring.setter
    def recurring(self, b):
        props = [SPropValue(PR_DECLINE_RECURRING_MEETING_REQUESTS, not b)]
        self._fb.SetProps(props)
        _utils._save(self._fb)
