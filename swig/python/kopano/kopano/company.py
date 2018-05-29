"""
Part of the high-level python bindings for Kopano.

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)
"""

import sys

from MAPI import (
    MAPI_UNICODE, RELOP_EQ, TBL_BATCH, ECSTORE_TYPE_PUBLIC,
    WrapStoreEntryID
)
from MAPI.Defs import PpropFindProp
from MAPI.Struct import (
    MAPIErrorNotFound, SPropertyRestriction, SPropValue,
    MAPIErrorCollision
)
from MAPI.Tags import PR_EC_COMPANY_NAME_W, PR_EC_STOREGUID
from MAPI.Util import GetPublicStore

from .properties import Properties
from .quota import Quota
from .group import Group

from .defs import EID_EVERYONE
from .errors import (
    NotFoundError, DuplicateError
)
from .compat import (
    benc as _benc, bdec as _bdec, fake_unicode as _unicode,
)

if sys.hexversion >= 0x03000000:
    try:
        from . import server as _server
    except ImportError: # pragma: no cover
        _server = sys.modules[__package__ + '.server']
    try:
        from . import user as _user
    except ImportError: # pragma: no cover
        _user = sys.modules[__package__ + '.user']
    try:
        from . import store as _store
    except ImportError: # pragma: no cover
        _store = sys.modules[__package__ + '.store']
else: # pragma: no cover
    import server as _server
    import user as _user
    import store as _store

class Company(Properties):
    """Company class"""

    def __init__(self, name, server=None):
        self.server = server or _server.Server()

        self._name = name = _unicode(name)
        if name != u'Default': # XXX
            try:
                id_ = self.server.sa.ResolveCompanyName(name, MAPI_UNICODE)
                self._eccompany = self.server.sa.GetCompany(id_, MAPI_UNICODE)
            except MAPIErrorNotFound:
                raise NotFoundError("no such company: '%s'" % name)

        # XXX cached because GetPublicStore does not see changes..
        #     do we need folder & store notifications (slow)?
        self._public_store = None

        self._mapiobj = None

    @property
    def mapiobj(self):
        if not self._mapiobj:
            id_ = self._eccompany.CompanyID
            self._mapiobj = self.server.mapisession.OpenEntry(id_, None, 0)
        return self._mapiobj

    @property
    def companyid(self): # XXX single-tenant case
        if self._name != u'Default':
            return _benc(self._eccompany.CompanyID)

    @property
    def admin(self):
        """Company :class:`administrator <User>` in multi-tenant mode."""
        if self._name != u'Default':
            id_ = self._eccompany.AdministratorID
            ecuser = self.server.sa.GetUser(id_, MAPI_UNICODE)
            return self.server.user(ecuser.Username)

    @admin.setter
    def admin(self, user):
        self._eccompany.AdministratorID = user._ecuser.UserID
        self.server.sa.SetCompany(self._eccompany, MAPI_UNICODE)

    @property
    def hidden(self):
        """The company is hidden from the addressbook."""
        if self._name != u'Default':
            return bool(self._eccompany.IsHidden)
        return False

    @property
    def name(self):
        """Company name."""
        return self._name

    @name.setter
    def name(self, value):
        value = _unicode(value)
        self._eccompany.Companyname = value
        self._name = value
        self.server.sa.SetCompany(self._eccompany, MAPI_UNICODE)

    def store(self, guid):
        """Store for the given GUID

        :param guid: store guid
        :return: :class:`store <Store>` with given GUID.
        """
        if guid == 'public':
            if not self.public_store:
                raise NotFoundError(
                    "no public store for company '%s'" % self.name
                )
            return self.public_store
        else:
            return self.server.store(guid)

    def stores(self):
        """Return all company :class:`stores <Store>`."""
        if self.server.multitenant:
            table = self.server.sa.OpenUserStoresTable(MAPI_UNICODE)
            restriction = SPropertyRestriction(
                RELOP_EQ, PR_EC_COMPANY_NAME_W, SPropValue(PR_EC_COMPANY_NAME_W, self.name)
            )
            table.Restrict(restriction, TBL_BATCH)
            for row in table.QueryRows(-1, 0):
                prop = PpropFindProp(row, PR_EC_STOREGUID)
                if prop:
                    yield _store.Store(_benc(prop.Value), self.server)
        else:
            for store in self.server.stores():
                yield store

    @property
    def public_store(self):
        """Company public :class:`store <Store>`."""
        if not self._public_store:
            if self._name == u'Default': # XXX
                pubstore = GetPublicStore(self.server.mapisession)
                if pubstore is None:
                    self._public_store = None
                else:
                    self._public_store = _store.Store(mapiobj=pubstore, server=self.server)
            else:
                try:
                    entryid = self.server.ems.CreateStoreEntryID(None, self._name, MAPI_UNICODE)
                except MAPIErrorNotFound:
                    self._public_store = None
                else:
                    self._public_store = _store.Store(entryid=_benc(entryid), server=self.server)

        return self._public_store

    def create_public_store(self):
        """Create company public :class:`store <Store>`."""
        if self._name == u'Default':
            try:
                storeid_rootid = self.server.sa.CreateStore(ECSTORE_TYPE_PUBLIC, EID_EVERYONE)
            except MAPIErrorCollision:
                raise DuplicateError("public store already exists")
        else:
            try:
                storeid_rootid = self.server.sa.CreateStore(ECSTORE_TYPE_PUBLIC, self._eccompany.CompanyID)
            except MAPIErrorCollision:
                raise DuplicateError("public store already exists for company '%s'" % self.name)

        store_entryid = WrapStoreEntryID(0, b'zarafa6client.dll', storeid_rootid[0][:-4]) + self.server.pseudo_url + b'\x00'

        self._public_store = _store.Store(entryid=_benc(store_entryid), server=self.server)
        return self._public_store

    def hook_public_store(self, store):
        """Hook company public :class:`store <Store>`.

        :param store: store to hook as public store
        """
        if self._name == u'Default':
            try:
                self.server.sa.HookStore(ECSTORE_TYPE_PUBLIC, EID_EVERYONE, _bdec(store.guid))
            except MAPIErrorCollision:
                raise DuplicateError("hooked public store already exists")
        else:
            try:
                self.server.sa.HookStore(ECSTORE_TYPE_PUBLIC, _bdec(self.companyid), _bdec(store.guid))
            except MAPIErrorCollision:
                raise DuplicateError("hooked public store already exists for company '%s'" % self.name)

        self._public_store = store

    def unhook_public_store(self):
        """Unhook company public :class:`store <Store>`."""
        if self._name == u'Default':
            try:
                self.server.sa.UnhookStore(ECSTORE_TYPE_PUBLIC, EID_EVERYONE)
            except MAPIErrorNotFound:
                raise NotFoundError("no hooked public store")
        else:
            try:
                self.server.sa.UnhookStore(ECSTORE_TYPE_PUBLIC, _bdec(self.companyid))
            except MAPIErrorNotFound:
                raise NotFoundError("no hooked public store for company '%s'" % self.name)
        self._public_store = None

    def user(self, name, create=False):
        """Return :class:`user <User>` with given name.

        :param name: user name
        :param create: create user if it doesn't exist (default False)
        """
        if '@' not in name and self._name != 'Default':
            name = name + '@' + self._name
        try:
            return self.server.user(name)
        except NotFoundError:
            if create:
                return self.create_user(name)
            else:
                raise # FIXME

    def get_user(self, name):
        """Return :class:`user <User>` with given name or *None* if not found.

        :param name: user name
        """
        try:
            return self.user(name)
        except NotFoundError:
            pass

    def users(self, parse=True, system=False):
        """

        :param parse: filter users on cli argument --user (default True)
        :param system: include system users (default False)
        :return: all :class:`users <User>` within company
        """
        if parse and getattr(self.server.options, 'users', None):
            for username in self.server.options.users:
                yield _user.User(username, self.server)
            return

        if self._name == u'Default':
            for user in self.server.users(system=system, remote=True):
                yield user
        else:
            for ecuser in self.server.sa.GetUserList(self._eccompany.CompanyID, MAPI_UNICODE):
                if ecuser.Username != 'SYSTEM':
                    yield _user.User(ecuser.Username, self.server)

    def admins(self):
        for ecuser in self.server.sa.GetRemoteAdminList(self._eccompany.CompanyID, MAPI_UNICODE):
            yield self.server.user(ecuser.Username)

    def add_admin(self, user):
        """Add user to admin list

        :param user: the user class:`user <User>`
        :raises: DuplicateError
        """
        try:
            self.server.sa.AddUserToRemoteAdminList(user._ecuser.UserID, self._eccompany.CompanyID)
        except MAPIErrorCollision:
            raise DuplicateError("user '%s' already admin for company '%s'" % (user.name, self.name))

    def remove_admin(self, user):
        """Remove user from admin list

        :param user: the :class:`user <User>`
        :raises: NotFoundError
        """
        try:
            self.server.sa.DelUserFromRemoteAdminList(user._ecuser.UserID, self._eccompany.CompanyID)
        except MAPIErrorNotFound:
            raise NotFoundError("user '%s' no admin for company '%s'" % (user.name, self.name))

    def views(self):
        for eccompany in self.server.sa.GetRemoteViewList(self._eccompany.CompanyID, MAPI_UNICODE):
            yield self.server.company(eccompany.Companyname)

    def add_view(self, company):
        try:
            self.server.sa.AddCompanyToRemoteViewList(company._eccompany.CompanyID, self._eccompany.CompanyID)
        except MAPIErrorCollision:
            raise DuplicateError("company '%s' already in view-list for company '%s'" % (company.name, self.name))

    def remove_view(self, company):
        try:
            self.server.sa.DelCompanyFromRemoteViewList(company._eccompany.CompanyID, self._eccompany.CompanyID)
        except MAPIErrorNotFound:
            raise NotFoundError("company '%s' not in view-list for company '%s'" % (company.name, self.name))

    def create_user(self, name, password=None):
        """Create a new :class:`user <User>` within the company.

        :param name: user name
        :param password: password (default None)
        """
        name = name.split('@')[0]
        self.server.create_user(name, password=password, company=self._name)
        return self.user('%s@%s' % (name, self._name))

    # XXX create_group/create=True
    def group(self, name):
        """Return :class:`group <Group>` with given name.

        :param name: group name
        :raises: NotFoundError
        """
        for group in self.groups(): # XXX
            if group.name == name:
                return group
        raise NotFoundError("no such group: '%s'" % name)

    def groups(self):
        """Return all :class:`groups <Group>` within the company."""
        if self.name == u'Default': # XXX
            for group in self.server.groups():
                yield group
        else:
            for ecgroup in self.server.sa.GetGroupList(self._eccompany.CompanyID, MAPI_UNICODE):
                yield Group(ecgroup.Groupname, self.server)

    @property
    def quota(self):
        """Company :class:`Quota`."""
        if self._name == u'Default':
            return Quota(self.server, None)
        else:
            return Quota(self.server, self._eccompany.CompanyID)

    def __eq__(self, c):
        if isinstance(c, Company):
            return self.companyid == c.companyid
        return False

    def __ne__(self, c):
        return not self == c

    def __contains__(self, u):
        return u in self.users()

    def __unicode__(self):
        return u"Company('%s')" % self._name
