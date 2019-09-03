# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import sys

from MAPI import MAPI_UNICODE
from MAPI.Struct import (
    ECGROUP, MAPIErrorNotFound, MAPIErrorInvalidParameter,
    MAPIErrorCollision
)
from .properties import Properties
from .errors import NotFoundError, DuplicateError
from .compat import fake_unicode as _unicode, benc as _benc

try:
    from . import server as _server
except ImportError: # pragma: no cover
    _server = sys.modules[__package__ + '.server']
try:
    from . import user as _user
except ImportError: # pragma: no cover
    _user = sys.modules[__package__ + '.user']

class Group(Properties):
    """Group class

    A group is a collection of :class:`users <User>` and/or
    subgroups of :class:`users <User>`.
    """

    def __init__(self, name, server=None):
        self.server = server or \
            _server.Server(_skip_check=True, parse_args=False)
        self._name = _unicode(name)
        try:
            self._ecgroup = self.server.sa.GetGroup(
                self.server.sa.ResolveGroupName(self._name, MAPI_UNICODE),
                MAPI_UNICODE
            )
        except (MAPIErrorNotFound, MAPIErrorInvalidParameter):
            raise NotFoundError("no such group '%s'" % name)

        self._mapiobj = None

    @property
    def mapiobj(self):
        """Underlying MAPI object."""
        if not self._mapiobj:
            self._mapiobj = self.server.mapisession.OpenEntry(
                self._ecgroup.GroupID, None, 0)
        return self._mapiobj

    @property
    def groupid(self):
        """Group id."""
        return _benc(self._ecgroup.GroupID)

    def users(self): # TODO recurse?
        """Return all :class:`users <User>` in group."""
        return self.members(groups=False)

    def groups(self): # TODO recurse?
        """Return all :class:`groups <Group>` in group."""
        return self.members(users=False)

    def members(self, groups=True, users=True): # TODO recurse?
        """Return all members in group (:class:`users <User>` or
        :class:`groups <Group>`)."""
        for ecuser in self.server.sa.GetUserListOfGroup(
                self._ecgroup.GroupID, MAPI_UNICODE):
            if ecuser.Username == 'SYSTEM':
                continue
            if users:
                try:
                    # TODO working around '@' duplication
                    username = '@'.join(ecuser.Username.split('@')[:2])
                    yield _user.User(username, self.server)
                    # TODO 'everyone', groups are included as users..
                except NotFoundError:
                    pass
            if groups:
                try:
                    yield Group(ecuser.Username, self.server)
                except NotFoundError:
                    pass

    @property
    def name(self):
        """Group name."""
        return self._name

    @name.setter
    def name(self, value):
        self._update(name=_unicode(value))

    @property
    def email(self):
        """Group email address."""
        return self._ecgroup.Email

    @email.setter
    def email(self, value):
        self._update(email=_unicode(value))

    @property
    def fullname(self): # currently identical to name (may change later?)
        return self._ecgroup.Fullname

    @fullname.setter
    def fullname(self, value):
        self._update(fullname=_unicode(value))

    @property
    def hidden(self):
        """The group is hidden from the addressbook."""
        return bool(self._ecgroup.IsHidden)

    @hidden.setter
    def hidden(self, value):
        self._update(hidden=value)

    def send_as(self):
        """Return :class:`users <User>` in send-as list."""
        for u in self.server.sa.GetSendAsList(
                self._ecgroup.GroupID,MAPI_UNICODE):
            yield self.server.user(u.Username)

    def add_send_as(self, user):
        """Add :class:`user <User>` to send-as list.

        :param user: user to add
        """
        try:
            self.server.sa.AddSendAsUser(
                self._ecgroup.GroupID, user._ecuser.UserID)
        except MAPIErrorCollision:
            raise DuplicateError(
                "user '%s' already in send-as for group '%s'" %
                    (user.name, self.name))

    def remove_send_as(self, user):
        """Remove :class:`user <User>` from send-as list.

        :param user: user to remove
        """
        try:
            self.server.sa.DelSendAsUser(
                self._ecgroup.GroupID, user._ecuser.UserID)
        except MAPIErrorNotFound:
            raise NotFoundError(
                "no user '%s' in send-as for group '%s'" %
                    (user.name, self.name))

    # TODO: also does groups..
    def add_user(self, user):
        """Add :class:`user <User>` to group.

        :param user: user to add
        """
        if isinstance(user, Group):
            self.server.sa.AddGroupUser(
                self._ecgroup.GroupID, user._ecgroup.GroupID)
        else:
            try:
                self.server.sa.AddGroupUser(
                    self._ecgroup.GroupID, user._ecuser.UserID)
            except MAPIErrorCollision:
                raise DuplicateError(
                    "group '%s' already contains user '%s'" %
                        (self.name, user.name))

    def remove_user(self, user): # TODO also 'delete' for consistency?
        """Remove :class:`user <User>` from group.

        :param user: user to remove
        """
        try:
            self.server.sa.DeleteGroupUser(
                self._ecgroup.GroupID, user._ecuser.UserID)
        except MAPIErrorNotFound:
            raise NotFoundError(
                "group '%s' does not contain user '%s'" %
                    (self.name, user.name))

    def _update(self, **kwargs):
        # TODO: crashes server on certain characters...
        self._name = kwargs.get('name', self.name)
        fullname = kwargs.get('fullname', self.fullname)
        email = kwargs.get('email', self.email)
        hidden = kwargs.get('hidden', self.hidden)
        group = ECGROUP(self._name, fullname, email, int(hidden),
            self._ecgroup.GroupID)
        self.server.sa.SetGroup(group, MAPI_UNICODE)
        self._ecgroup = self.server.sa.GetGroup(
            self.server.sa.ResolveGroupName(self._name, MAPI_UNICODE),
            MAPI_UNICODE
        )

    def __eq__(self, u): # TODO check same server?
        if isinstance(u, Group):
            return self.groupid == u.groupid
        return False

    def __ne__(self, g):
        return not self == g

    def __contains__(self, u): # TODO subgroups
        return u in self.users()

    def __unicode__(self):
        return "Group('%s')" % self.name
