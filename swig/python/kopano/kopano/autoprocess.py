"""
Part of the high-level python bindings for Kopano

Copyright 2018 - Kopano and its licensors (see LICENSE file)
"""

from MAPI.Tags import (
    PR_FREEBUSY_ENTRYIDS,
)
from MAPI.Defs import (
    HrGetOneProp, CHANGE_PROP_TYPE
)
from MAPI.Struct import (
    SPropValue, MAPIErrorNotFound
)
from MAPI import (
    MAPI_MODIFY, KEEP_OPEN_READWRITE, PT_BOOLEAN
)

from .defs import NAMED_PROPS_KC

class AutoProcess(object):
    """AutoProcess class"""

    def __init__(self, store):
        fbeid = store.root.prop(PR_FREEBUSY_ENTRYIDS).value[1]
        self._fb = store.mapiobj.OpenEntry(fbeid, None, MAPI_MODIFY)
        self.store = store
        self._ids = store.mapiobj.GetIDsFromNames(NAMED_PROPS_KC, 0)

    @property
    def enabled(self):
        prop = CHANGE_PROP_TYPE(self._ids[0], PT_BOOLEAN)
        try:
            return HrGetOneProp(self._fb, prop).Value
        except MAPIErrorNotFound:
            return True

    @enabled.setter
    def enabled(self, b):
        prop = CHANGE_PROP_TYPE(self._ids[0], PT_BOOLEAN)
        self._fb.SetProps([SPropValue(prop, b)])
        self._fb.SaveChanges(KEEP_OPEN_READWRITE)
