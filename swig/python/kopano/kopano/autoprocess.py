# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Part of the high-level python bindings for Kopano

Copyright 2018 - 2019 Kopano and its licensors (see LICENSE file)
"""

import sys

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
    MAPI_MODIFY, PT_BOOLEAN, MAPI_CREATE
)

from .defs import NAMED_PROPS_KC

try:
    from . import utils as _utils
except ImportError: # pragma: no cover
    _utils = sys.modules[__package__ + '.utils']

class AutoProcess(object):
    """AutoProcess class

    Manage settings for automatically processing meeting requests.
    """

    def __init__(self, store):
        fbeid = store.root.prop(PR_FREEBUSY_ENTRYIDS).value[1]
        self._fb = store.mapiobj.OpenEntry(fbeid, None, MAPI_MODIFY)
        self._ids = store.mapiobj.GetIDsFromNames(NAMED_PROPS_KC, MAPI_CREATE)

    @property
    def enabled(self):
        """Auto-processing is enabled."""
        prop = CHANGE_PROP_TYPE(self._ids[0], PT_BOOLEAN)
        try:
            return HrGetOneProp(self._fb, prop).Value
        except MAPIErrorNotFound:
            return True

    @enabled.setter
    def enabled(self, b):
        prop = CHANGE_PROP_TYPE(self._ids[0], PT_BOOLEAN)
        self._fb.SetProps([SPropValue(prop, b)])
        _utils._save(self._fb)
