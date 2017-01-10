"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI.Util import *

from .errors import *
from .defs import *

from utils import repr as _repr, prop as _prop, props as _props

class Group(object):
    """Group class"""

    def __init__(self, name, server=None):
        from . import Server

        self.server = server or Server()
        self._name = unicode(name)
        try:
            self._ecgroup = self.server.sa.GetGroup(self.server.sa.ResolveGroupName(self._name, MAPI_UNICODE), MAPI_UNICODE)
        except (MAPIErrorNotFound, MAPIErrorInvalidParameter):
            raise NotFoundError("no such group '%s'" % name)

        self._mapiobj = None

    @property
    def mapiobj(self):
        if not self._mapiobj:
            self._mapiobj = self.server.mapisession.OpenEntry(self._ecgroup.GroupID, None, 0)
        return self._mapiobj

    @property
    def groupid(self):
        return bin2hex(self._ecgroup.GroupID)

    def users(self):
        """Users in group"""

        return self.members(groups=False)

    def groups(self):
        """Groups in group"""

        return self.members(users=False)

    def members(self, groups=True, users=True):
        """All members in group, users or groups"""

        from .user import User

        for ecuser in self.server.sa.GetUserListOfGroup(self._ecgroup.GroupID, MAPI_UNICODE):
            if ecuser.Username == 'SYSTEM':
                continue
            if users:
                try:
                    # XXX working around '@' duplication
                    username = '@'.join(ecuser.Username.split('@')[:2])
                    yield User(username, self.server)
                except NotFoundError: # XXX everyone, groups are included as users..
                    pass
            if groups:
                try:
                    yield Group(ecuser.Username, self.server)
                except NotFoundError:
                    pass

    @property
    def name(self):
        return self._name

    @name.setter
    def name(self, value):
        self._update(name=unicode(value))

    @property
    def email(self):
        return self._ecgroup.Email

    @email.setter
    def email(self, value):
        self._update(email=unicode(value))

    @property
    def fullname(self):
        return self._ecgroup.Fullname

    @fullname.setter
    def fullname(self, value):
        self._update(fullname=unicode(value))

    @property
    def hidden(self):
        return self._ecgroup.IsHidden == True

    @hidden.setter
    def hidden(self, value):
        self._update(hidden=value)

    def prop(self, proptag):
        return _prop(self, self.mapiobj, proptag)

    def props(self):
        return _props(self.mapiobj)

    def send_as(self):
        for u in self.server.sa.GetSendAsList(self._ecgroup.GroupID, MAPI_UNICODE):
            yield self.server.user(u.Username)

    def add_send_as(self, user):
        try:
            self.server.sa.AddSendAsUser(self._ecgroup.GroupID, user._ecuser.UserID)
        except MAPIErrorCollision:
            raise DuplicateError("user '%s' already in send-as for group '%s'" % (user.name, self.name))

    def remove_send_as(self, user):
        try:
            self.server.sa.DelSendAsUser(self._ecgroup.GroupID, user._ecuser.UserID)
        except MAPIErrorNotFound:
            raise NotFoundError("no user '%s' in send-as for group '%s'" % (user.name, self.name))

    # XXX: also does groups..
    def add_user(self, user):
        if isinstance(user, Group):
            self.server.sa.AddGroupUser(self._ecgroup.GroupID, user._ecgroup.GroupID)
        else:
            try:
                self.server.sa.AddGroupUser(self._ecgroup.GroupID, user._ecuser.UserID)
            except MAPIErrorCollision:
                raise DuplicateError("group '%s' already contains user '%s'" % (self.name, user.name))

    def remove_user(self, user):
        try:
            self.server.sa.DeleteGroupUser(self._ecgroup.GroupID, user._ecuser.UserID)
        except MAPIErrorNotFound:
            raise NotFoundError("group '%s' does not contain user '%s'" % (self.name, user.name))

    def _update(self, **kwargs):
        # XXX: crashes server on certain characters...
        self._name = kwargs.get('name', self.name)
        fullname = kwargs.get('fullname', self.fullname)
        email = kwargs.get('email', self.email)
        hidden = kwargs.get('hidden', self.hidden)
        group = ECGROUP(self._name, fullname, email, int(hidden), self._ecgroup.GroupID)
        self.server.sa.SetGroup(group, MAPI_UNICODE)
        self._ecgroup = self.server.sa.GetGroup(self.server.sa.ResolveGroupName(self._name, MAPI_UNICODE), MAPI_UNICODE)

    def __eq__(self, u): # XXX check same server?
        if isinstance(u, Group):
            return self.groupid == u.groupid
        return False

    def __ne__(self, g):
        return not self == g

    def __contains__(self, u): # XXX subgroups
        return u in self.users()

    def __unicode__(self):
        return u"Group('%s')" % self.name

    def __repr__(self):
        return _repr(self)
