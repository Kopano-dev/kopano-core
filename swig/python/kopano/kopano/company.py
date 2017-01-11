"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import codecs

from MAPI.Util import *

from .store import Store
from .user import User
from .quota import Quota
from .group import Group

from .defs import *
from .errors import *

from .compat import unhex as _unhex, repr as _repr
from .utils import prop as _prop, props as _props

class Company(object):
    """Company class"""

    def __init__(self, name, server=None):
        from .server import Server
        self.server = server or Server()

        self._name = name = unicode(name)
        if name != u'Default': # XXX
            try:
                self._eccompany = self.server.sa.GetCompany(self.server.sa.ResolveCompanyName(self._name, MAPI_UNICODE), MAPI_UNICODE)
            except MAPIErrorNotFound:
                raise NotFoundError("no such company: '%s'" % name)
        self._public_store = None # XXX cached because GetPublicStore does not see changes.. do we need folder & store notifications (slow)?

        self._mapiobj = None

    @property
    def mapiobj(self):
        if not self._mapiobj:
            self._mapiobj = self.server.mapisession.OpenEntry(self._eccompany.CompanyID, None, 0)
        return self._mapiobj

    @property
    def companyid(self): # XXX single-tenant case
        return bin2hex(self._eccompany.CompanyID)

    @property
    def admin(self):
        if self._name != u'Default':
            ecuser = self.server.sa.GetUser(self._eccompany.AdministratorID, MAPI_UNICODE)
            return self.server.user(ecuser.Username)
        # XXX

    @admin.setter
    def admin(self, user):
        self._eccompany.AdminstratorID = user._ecuser.UserID
        self.server.sa.SetCompany(self._eccompany, MAPI_UNICODE)

    @property
    def hidden(self):
        if self._name != u'Default':
            return self._eccompany.IsHidden == True
        # XXX

    @property
    def name(self):
        """ Company name """

        return self._name

    @name.setter
    def name(self, value):
        value = unicode(value)
        self._eccompany.Companyname = value
        self._name = value
        self.server.sa.SetCompany(self._eccompany, MAPI_UNICODE)

    def store(self, guid):
        if guid == 'public':
            if not self.public_store:
                raise NotFoundError("no public store for company '%s'" % self.name)
            return self.public_store
        else:
            return self.server.store(guid)

    def stores(self):
        if self.server.multitenant:
            table = self.server.sa.OpenUserStoresTable(MAPI_UNICODE)
            table.Restrict(SPropertyRestriction(RELOP_EQ, PR_EC_COMPANY_NAME_W, SPropValue(PR_EC_COMPANY_NAME_W, self.name)), TBL_BATCH)
            for row in table.QueryRows(-1,0):
                prop = PpropFindProp(row, PR_EC_STOREGUID)
                if prop:
                    yield Store(codecs.encode(prop.Value, 'hex'), self.server)
        else:
            for store in self.server.stores():
                yield store

    def prop(self, proptag):
        return _prop(self, self.mapiobj, proptag)

    def props(self):
        return _props(self.mapiobj)

    @property
    def public_store(self):
        """ Company public :class:`store <Store>` """

        if not self._public_store:
            if self._name == u'Default': # XXX
                pubstore = GetPublicStore(self.server.mapisession)
                if pubstore is None:
                    self._public_store = None
                else:
                    self._public_store = Store(mapiobj=pubstore, server=self.server)
            else:
                try:
                    entryid = self.server.ems.CreateStoreEntryID(None, self._name, MAPI_UNICODE)
                except MAPIErrorNotFound:
                    self._public_store = None
                else:
                    self._public_store = Store(entryid=entryid, server=self.server)

        return self._public_store

    def create_public_store(self):
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

        store_entryid = WrapStoreEntryID(0, b'zarafa6client.dll', storeid_rootid[0][:-4])+self.server.pseudo_url+b'\x00'

        self._public_store = Store(entryid=store_entryid, server=self.server)
        return self._public_store

    def create_store(self, public=False): # XXX deprecated?
        if public:
            return self.create_public_store
        # XXX

    def hook_public_store(self, store):
        if self._name == u'Default':
            try:
                self.server.sa.HookStore(ECSTORE_TYPE_PUBLIC, EID_EVERYONE, _unhex(store.guid))
            except MAPIErrorCollision:
                raise DuplicateError("hooked public store already exists")
        else:
            try:
                self.server.sa.HookStore(ECSTORE_TYPE_PUBLIC, _unhex(self.companyid), _unhex(store.guid))
            except MAPIErrorCollision:
                raise DuplicateError("hooked public store already exists for company '%s'" % self.name)

        self._public_store = store

    def unhook_public_store(self):
        if self._name == u'Default':
            try:
                self.server.sa.UnhookStore(ECSTORE_TYPE_PUBLIC, EID_EVERYONE)
            except MAPIErrorNotFound:
                raise NotFoundError("no hooked public store")
        else:
            try:
                self.server.sa.UnhookStore(ECSTORE_TYPE_PUBLIC, _unhex(self.companyid))
            except MAPIErrorNotFound:
                raise NotFoundError("no hooked public store for company '%s'" % self.name)
        self._public_store = None

    def user(self, name, create=False):
        """ Return :class:`user <User>` with given name; raise exception if not found """

        name = unicode(name)
        for user in self.users(): # XXX slow
            if user.name == name:
                return User(name, self.server)
        if create:
            return self.create_user(name)
        else:
            raise NotFoundError("no such user: '%s'" % name)

    def get_user(self, name):
        """ Return :class:`user <User>` with given name or *None* if not found """

        try:
            return self.user(name)
        except Error:
            pass

    def users(self, parse=True):
        """ Return all :class:`users <User>` within company """

        if parse and getattr(self.server.options, 'users', None):
            for username in self.server.options.users:
                yield User(username, self.server)
            return

        for username in MAPI.Util.AddressBook.GetUserList(self.server.mapisession, self._name if self._name != u'Default' else None, MAPI_UNICODE): # XXX serviceadmin?
            if username != 'SYSTEM':
                yield User(username, self.server)

    def admins(self):
        for ecuser in self.server.sa.GetRemoteAdminList(self._eccompany.CompanyID, MAPI_UNICODE):
            yield self.server.user(ecuser.Username)

    def add_admin(self, user):
        try:
            self.server.sa.AddUserToRemoteAdminList(user._ecuser.UserID, self._eccompany.CompanyID)
        except MAPIErrorCollision:
            raise DuplicateError("user '%s' already admin for company '%s'" % (user.name, self.name))

    def remove_admin(self, user):
        try:
            self.server.sa.DelUserFromRemoteAdminList(user._ecuser.UserID, self._eccompany.CompanyID)
        except MAPIErrorNotFound:
            raise DuplicateError("user '%s' no admin for company '%s'" % (user.name, self.name))

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
        self.server.create_user(name, password=password, company=self._name)
        return self.user('%s@%s' % (name, self._name))

    def group(self, name):
        for group in self.groups(): # XXX
            if group.name == name:
                return group
        raise NotFoundError("no such group: '%s'" % name)

    def groups(self):
        if self.name == u'Default': # XXX
            for ecgroup in self.server.sa.GetGroupList(None, MAPI_UNICODE):
                yield Group(ecgroup.Groupname, self.server)
        else:
            for ecgroup in self.server.sa.GetGroupList(self._eccompany.CompanyID, MAPI_UNICODE):
                yield Group(ecgroup.Groupname, self.server)

    @property
    def quota(self):
        """ Company :class:`Quota` """

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

    def __repr__(self):
        return _repr(self)
