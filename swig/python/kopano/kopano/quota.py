# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import sys

from MAPI.Struct import ECQUOTA, MAPIErrorNotFound, MAPIErrorCollision

from .defs import CONTAINER_COMPANY, ACTIVE_USER
from .compat import repr as _repr
from .errors import NotFoundError, DuplicateError

try:
    from . import utils as _utils
except ImportError: # pragma: no cover
    _utils = sys.modules[__package__ + '.utils']

class Quota(object):
    """Quota class

    Manage :class:`user <User>` or :class:`company <Company>`
    quota settings.
    """

    def __init__(self, server, userid):
        self.server = server
        self.userid = userid
        # XXX quota for 'default' company?
        self._warning_limit = self._soft_limit = self._hard_limit = 0
        if userid:
            quota = server.sa.GetQuota(userid, False)
            self._warning_limit = quota.llWarnSize
            self._soft_limit = quota.llSoftSize
            self._hard_limit = quota.llHardSize
            # XXX: logical name for variable
            # Use default quota set in /etc/kopano/server.cfg
            self._use_default_quota = bool(quota.bUseDefaultQuota)
            # XXX: is this for multitenancy?
            self._isuser_default_quota = quota.bIsUserDefaultQuota

    @property
    def warning_limit(self):
        """Warning limit."""
        return self._warning_limit

    @warning_limit.setter
    def warning_limit(self, value):
        self.update(warning_limit=value)

    @property
    def soft_limit(self):
        """Soft limit."""
        return self._soft_limit

    @soft_limit.setter
    def soft_limit(self, value):
        self.update(soft_limit=value)

    @property
    def hard_limit(self):
        """Hard limit."""

        return self._hard_limit

    @hard_limit.setter
    def hard_limit(self, value):
        self.update(hard_limit=value)

    #TODO: support defaultQuota and IsuserDefaultQuota
    def update(self, **kwargs):
        """
        Update function for Quota limits.

        :param warning_limit: Warning limit.
        :param soft_limit: Soft limit.
        :param hard_limit: Hard limit.
        """
        self._warning_limit = kwargs.get('warning_limit', self._warning_limit)
        self._soft_limit = kwargs.get('soft_limit', self._soft_limit)
        self._hard_limit = kwargs.get('hard_limit', self._hard_limit)
        self._use_default_quota = kwargs.get('use_default',
            self._use_default_quota)
        # TODO: implement setting defaultQuota, userdefaultQuota
        # (self, bUseDefaultQuota, bIsUserDefaultQuota, llWarnSize, llSoftSize,
        # llHardSize)
        quota = ECQUOTA(self._use_default_quota, False, self._warning_limit,
            self._soft_limit, self._hard_limit)
        self.server.sa.SetQuota(self.userid, quota)

    @property
    def use_default(self):
        """Use default quota."""
        return self._use_default_quota

    @use_default.setter
    def use_default(self, x):
        self.update(use_default=x)

    def recipients(self):
        """Return all :class:`recipients <User>` of quota messages."""
        if self.userid:
            for ecuser in self.server.sa.GetQuotaRecipients(self.userid, 0):
                yield self.server.user(ecuser.Username)

    # XXX remove company flag
    def add_recipient(self, user, company=False):
        """Add :class:`recipient <User>` of quota messages."""
        objclass = CONTAINER_COMPANY if company else ACTIVE_USER
        try:
            self.server.sa.AddQuotaRecipient(self.userid, user._ecuser.UserID,
                objclass)
        except MAPIErrorCollision:
            raise DuplicateError("user '%s' already in %squota recipients" % \
                (user.name, 'company' if company else 'user'))

    # XXX remove company flag
    def remove_recipient(self, user, company=False):
        """Remove :class:`recipient <User>` of quota messages."""
        objclass = CONTAINER_COMPANY if company else ACTIVE_USER
        try:
            self.server.sa.DeleteQuotaRecipient(
                self.userid, user._ecuser.UserID, objclass)
        except MAPIErrorNotFound:
            raise NotFoundError("user '%s' not in %squota recipients" % \
                (user.name, 'company' if company else 'user'))

    def __unicode__(self):
        return 'Quota(warning=%s, soft=%s, hard=%s)' % (
            _utils.bytes_to_human(self.warning_limit),
            _utils.bytes_to_human(self.soft_limit),
            _utils.bytes_to_human(self.hard_limit)
        )

    def __repr__(self):
        return _repr(self)
