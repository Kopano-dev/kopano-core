# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import time
import datetime
import sys

from MAPI.Tags import (
    PR_EC_OUTOFOFFICE, PR_EC_OUTOFOFFICE_SUBJECT_W,
    PR_EC_OUTOFOFFICE_MSG_W, PR_EC_OUTOFOFFICE_FROM,
    PR_EC_OUTOFOFFICE_UNTIL
)
from MAPI.Struct import SPropValue
from MAPI.Time import unixtime

from .errors import NotFoundError

try:
    from . import utils as _utils
except ImportError: # pragma: no cover
    _utils = sys.modules[__package__ + '.utils']

class OutOfOffice(object):
    """OutOfOffice class

    Manage out-of-office settings, such as status, subject and message.
    """
    def __init__(self, store):
        self.store = store

    @property
    def enabled(self):
        """Out-of-office is enabled."""
        try:
            return self.store.prop(PR_EC_OUTOFOFFICE).value
        except NotFoundError:
            return False

    @enabled.setter
    def enabled(self, value):
        self.store.mapiobj.SetProps([SPropValue(PR_EC_OUTOFOFFICE, value)])
        _utils._save(self.store.mapiobj)

    @property
    def subject(self):
        """Out-of-office subject."""
        try:
            return self.store.prop(PR_EC_OUTOFOFFICE_SUBJECT_W).value
        except NotFoundError:
            return ''

    @subject.setter
    def subject(self, value):
        if value is None:
            self.store.mapiobj.DeleteProps([PR_EC_OUTOFOFFICE_SUBJECT_W])
        else:
            self.store.mapiobj.SetProps(
                [SPropValue(PR_EC_OUTOFOFFICE_SUBJECT_W, str(value))])
        _utils._save(self.store.mapiobj)

    @property
    def message(self):
        """Out-of-office message."""
        try:
            return self.store.prop(PR_EC_OUTOFOFFICE_MSG_W).value
        except NotFoundError:
            return u''

    @message.setter
    def message(self, value):
        if value is None:
            self.store.mapiobj.DeleteProps([PR_EC_OUTOFOFFICE_MSG_W])
        else:
            self.store.mapiobj.SetProps(
                [SPropValue(PR_EC_OUTOFOFFICE_MSG_W, str(value))])
        _utils._save(self.store.mapiobj)

    @property
    def start(self):
        """Out-of-office is active starting from the given date."""
        try:
            return self.store.prop(PR_EC_OUTOFOFFICE_FROM).value
        except NotFoundError:
            pass

    @start.setter
    def start(self, value):
        if value is None:
            self.store.mapiobj.DeleteProps([PR_EC_OUTOFOFFICE_FROM])
        else:
            value = unixtime(time.mktime(value.timetuple()))
            self.store.mapiobj.SetProps(
                [SPropValue(PR_EC_OUTOFOFFICE_FROM, value)])
        _utils._save(self.store.mapiobj)

    @property
    def end(self):
        """Out-of-office is active until the given date."""
        try:
            return self.store.prop(PR_EC_OUTOFOFFICE_UNTIL).value
        except NotFoundError:
            pass

    @end.setter
    def end(self, value):
        if value is None:
            self.store.mapiobj.DeleteProps([PR_EC_OUTOFOFFICE_UNTIL])
        else:
            value = unixtime(time.mktime(value.timetuple()))
            self.store.mapiobj.SetProps(
                [SPropValue(PR_EC_OUTOFOFFICE_UNTIL, value)])
        _utils._save(self.store.mapiobj)

    @property
    def period_desc(self): # TODO class Period?
        """English description of out-of-office date range."""
        terms = []
        if self.start:
            terms.append('from %s' % self.start)
        if self.end:
            terms.append('until %s' % self.end)
        return ' '.join(terms)

    @property
    def active(self):
        """Out-of-office is currently active (start <= now < end)."""
        if not self.enabled:
            return False
        now = datetime.datetime.now()
        if self.start and now < self.start:
            return False
        if self.end and now >= self.end:
            return False
        return True

    def __unicode__(self):
        return 'OutOfOffice(%s)' % self.subject

    def __repr__(self):
        return self.__unicode__()

    def update(self, **kwargs):
        for key, val in kwargs.items():
            setattr(self, key, val)
