"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import datetime
import os
import time
import socket

from MAPI.Util import *

from .parser import parser
from .user import User
from .table import Table
from .company import Company
from .group import Group
from .store import Store
from .config import Config

from .errors import *
from .defs import *

from .compat import (
    unhex as _unhex, decode as _decode, repr as _repr,
    fake_unicode as _unicode
)
from .utils import state as _state, sync as _sync

def _timed_cache(seconds=0, minutes=0, hours=0, days=0):
    # used with permission from will mcgugan, https://www.willmcgugan.com
    time_delta = datetime.timedelta(seconds=seconds, minutes=minutes, hours=hours, days=days)

    def decorate(f):
        f._updates = {}
        f._results = {}

        def do_cache(*args, **kwargs):
            key = tuple(sorted(kwargs.items()))

            updates = f._updates
            results = f._results

            t = datetime.datetime.now()
            updated = updates.get(key, t)

            if key not in results or t-updated > time_delta:
                # calculate
                updates[key] = t
                result = f(*args, **kwargs)
                results[key] = result
                return result
            else:
                # cache
                return results[key]
        return do_cache
    return decorate

class Server(object):
    """Server class

    By default, tries to connect to a storage server as configured in ``/etc/kopano/admin.cfg`` or
    at UNIX socket ``/var/run/kopano/server.sock``

    Looks at command-line to see if another server address or other related options were given (such as -c, -s, -k, -p)

    :param server_socket: similar to 'server_socket' option in config file
    :param sslkey_file: similar to 'sslkey_file' option in config file
    :param sslkey_pass: similar to 'sslkey_pass' option in config file
    :param config: path of configuration file containing common server options, for example ``/etc/kopano/admin.cfg``
    :param auth_user: username to user for user authentication
    :param auth_pass: password to use for user authentication
    :param log: logger object to receive useful (debug) information
    :param options: OptionParser instance to get settings from (see :func:`parser`)
    :param parse_args: set this True if cli arguments should be parsed
    """

    def __init__(self, options=None, config=None, sslkey_file=None, sslkey_pass=None, server_socket=None, auth_user=None, auth_pass=None, log=None, service=None, mapisession=None, parse_args=True):
        self.options = options
        self.config = config
        self.sslkey_file = sslkey_file
        self.sslkey_pass = sslkey_pass
        self.server_socket = server_socket
        self.service = service
        self.log = log
        self.mapisession = mapisession
        self._store_cache = {}

        if not self.mapisession:
            # get cmd-line options
            if parse_args and not self.options:
                self.options, args = parser().parse_args()

            # determine config file
            if config:
                pass
            elif getattr(self.options, 'config_file', None):
                config_file = os.path.abspath(self.options.config_file)
                config = Config(None, filename=self.options.config_file)
            else:
                config_file = '/etc/kopano/admin.cfg'
                try:
                    open(config_file) # check if accessible
                    config = Config(None, filename=config_file)
                except IOError:
                    pass
            self.config = config

            # get defaults
            if os.getenv("KOPANO_SOCKET"): # env variable used in testset
                self.server_socket = os.getenv("KOPANO_SOCKET")
            elif config:
                if not (server_socket or getattr(self.options, 'server_socket')): # XXX generalize
                    self.server_socket = config.get('server_socket')
                    self.sslkey_file = config.get('sslkey_file')
                    self.sslkey_pass = config.get('sslkey_pass')
            self.server_socket = self.server_socket or "default:"

            # override with explicit or command-line args
            self.server_socket = server_socket or getattr(self.options, 'server_socket', None) or self.server_socket
            self.sslkey_file = sslkey_file or getattr(self.options, 'sslkey_file', None) or self.sslkey_file
            self.sslkey_pass = sslkey_pass or getattr(self.options, 'sslkey_pass', None) or self.sslkey_pass

            # make actual connection. in case of service, wait until this succeeds.
            self.auth_user = auth_user or getattr(self.options, 'auth_user', None) or 'SYSTEM' # XXX override with args
            self.auth_pass = auth_pass or getattr(self.options, 'auth_pass', None) or ''

            flags = EC_PROFILE_FLAGS_NO_NOTIFICATIONS

            # Username and password was supplied, so let us do verfication
            # (OpenECSession will not check password unless this parameter is provided)
            if self.auth_user and self.auth_pass:
                flags |= EC_PROFILE_FLAGS_NO_UID_AUTH

            while True:
                try:
                    self.mapisession = OpenECSession(self.auth_user, self.auth_pass, self.server_socket, sslkey_file=self.sslkey_file, sslkey_pass=self.sslkey_pass, flags=flags)
                    break
                except (MAPIErrorNetworkError, MAPIErrorDiskError):
                    if service:
                        service.log.warn("could not connect to server at '%s', retrying in 5 sec" % self.server_socket)
                        time.sleep(5)
                    else:
                        raise Error("could not connect to server at '%s'" % self.server_socket)
                except MAPIErrorLogonFailed:
                    raise LogonError('Could not logon to server: username or password incorrect')

        # start talking dirty
        self.mapistore = GetDefaultStore(self.mapisession)
        self.sa = self.mapistore.QueryInterface(IID_IECServiceAdmin)
        self.ems = self.mapistore.QueryInterface(IID_IExchangeManageStore)
        self._ab = None
        self._admin_store = None
        self._gab = None
        entryid = HrGetOneProp(self.mapistore, PR_STORE_ENTRYID).Value
        self.pseudo_url = entryid[entryid.find(b'pseudo:'):-1] # XXX ECSERVER
        self.name = self.pseudo_url[9:] # XXX get this kind of stuff from pr_ec_statstable_servers..?

    def nodes(self): # XXX delay mapi sessions until actually needed
        for row in self.table(PR_EC_STATSTABLE_SERVERS).dict_rows():
            yield Server(options=self.options, config=self.config, sslkey_file=self.sslkey_file, sslkey_pass=self.sslkey_pass, server_socket=row[PR_EC_STATS_SERVER_HTTPSURL], log=self.log, service=self.service)

    def table(self, name, restriction=None, order=None, columns=None):
        return Table(self, self.mapistore.OpenProperty(name, IID_IMAPITable, MAPI_UNICODE, 0), name, restriction=restriction, order=order, columns=columns)

    def tables(self):
        for table in (PR_EC_STATSTABLE_SYSTEM, PR_EC_STATSTABLE_SESSIONS, PR_EC_STATSTABLE_USERS, PR_EC_STATSTABLE_COMPANY, PR_EC_STATSTABLE_SERVERS):
            try:
                yield self.table(table)
            except MAPIErrorNotFound:
                pass

    def gab_table(self): # XXX separate addressbook class? useful to add to self.tables?
        ct = self.gab.GetContentsTable(MAPI_DEFERRED_ERRORS)
        return Table(self, ct, PR_CONTAINER_CONTENTS)

    @property
    def ab(self):
        """ Address Book """
        if not self._ab:
            self._ab = self.mapisession.OpenAddressBook(0, None, 0) # XXX
        return self._ab

    @property
    def admin_store(self):
        if not self._admin_store:
            self._admin_store = Store(mapiobj=self.mapistore, server=self)
        return self._admin_store

    @property
    def gab(self):
        """ Global Address Book """

        if not self._gab:
            self._gab = self.ab.OpenEntry(self.ab.GetDefaultDir(), None, 0)
        return self._gab

    @property
    def guid(self):
        """ Server GUID """

        return bin2hex(HrGetOneProp(self.mapistore, PR_MAPPING_SIGNATURE).Value)

    def user(self, name=None, email=None, create=False):
        """ Return :class:`user <User>` with given name """

        try:
            return User(name, email=email, server=self)
        except NotFoundError:
            if create and name:
                return self.create_user(name)
            else:
                raise

    def get_user(self, name):
        """ Return :class:`user <User>` with given name or *None* if not found """

        try:
            return self.user(name)
        except Error:
            pass

    def users(self, remote=False, system=False, parse=True):
        """ Return all :class:`users <User>` on server

            :param remote: include users on remote server nodes
            :param system: include system users
        """

        if parse and getattr(self.options, 'users', None):
            for username in self.options.users:
                if '*' in username or '?' in username: # XXX unicode.. need to use something like boost::wregex in ZCP?
                    regex = username.replace('*', '.*').replace('?', '.')
                    restriction = SPropertyRestriction(RELOP_RE, PR_DISPLAY_NAME_A, SPropValue(PR_DISPLAY_NAME_A, regex))
                    for match in MAPI.Util.AddressBook.GetAbObjectList(self.mapisession, restriction):
                        if fnmatch.fnmatch(match, username):
                            yield User(match, self)
                else:
                    yield User(_decode(username), self) # XXX can optparse output unicode?
            return
        try:
            for name in self._companylist():
                for user in Company(name, self).users(): # XXX remote/system check
                    yield user
        except MAPIErrorNoSupport:
            for username in MAPI.Util.AddressBook.GetUserList(self.mapisession, None, MAPI_UNICODE):
                user = User(username, self)
                if system or username != u'SYSTEM':
                    if remote or user._ecuser.Servername in (self.name, ''):
                        yield user
                    # XXX following two lines not necessary with python-mapi from trunk
                    elif not remote and user.local: # XXX check if GetUserList can filter local/remote users
                        yield user

    def create_user(self, name, email=None, password=None, company=None, fullname=None, create_store=True):
        """ Create a new :class:`user <Users>` on the server

        :param name: the login name of the new user
        :param email: the email address of the user
        :param password: the login password of the user
        :param company: the company of the user
        :param fullname: the full name of the user
        :param create_store: should a store be created for the new user
        :return: :class:`<User>`
        """
        name = _unicode(name)
        fullname = _unicode(fullname or '')
        if email:
            email = _unicode(email)
        else:
            email = u'%s@%s' % (name, socket.gethostname())
        if password:
            password = _unicode(password)
        if company:
            company = _unicode(company)
        if company and company != u'Default':
            usereid = self.sa.CreateUser(ECUSER(u'%s@%s' % (name, company), password, email, fullname), MAPI_UNICODE)
            user = self.company(company).user(u'%s@%s' % (name, company))
        else:
            try:
                usereid = self.sa.CreateUser(ECUSER(name, password, email, fullname), MAPI_UNICODE)
            except MAPIErrorCollision:
                raise DuplicateError("user '%s' already exists" % name)
            user = self.user(name)
        if create_store:
            self.sa.CreateStore(ECSTORE_TYPE_PRIVATE, _unhex(user.userid))
        return user

    def remove_user(self, name): # XXX delete(object)?
        user = self.user(name)
        self.sa.DeleteUser(user._ecuser.UserID)

    def company(self, name, create=False):
        """ Return :class:`company <Company>` with given name; raise exception if not found """

        try:
            return Company(name, self)
        except MAPIErrorNoSupport:
            raise NotFoundError('no such company: %s' % name)
        except NotFoundError:
            if create:
                return self.create_company(name)
            else:
                raise

    def get_company(self, name):
        """ Return :class:`company <Company>` with given name or *None* if not found """

        try:
            return self.company(name)
        except Error:
            pass

    def remove_company(self, name): # XXX delete(object)?
        company = self.company(name)

        if company.name == u'Default':
            raise NotSupportedError('cannot remove company in singletenant mode')
        else:
            self.sa.DeleteCompany(company._eccompany.CompanyID)

    def _companylist(self): # XXX fix self.sa.GetCompanyList(MAPI_UNICODE)? looks like it's not swigged correctly?
        self.sa.GetCompanyList(MAPI_UNICODE) # XXX exception for single-tenant....
        return MAPI.Util.AddressBook.GetCompanyList(self.mapisession, MAPI_UNICODE)

    @property
    def multitenant(self):
        """ Return boolean showing if the server is multitenant """

        try:
            self._companylist()
            return True
        except MAPIErrorNoSupport:
            return False

    def companies(self, remote=False, parse=True): # XXX remote?
        """ Return all :class:`companies <Company>` on server

            :param remote: include companies without users on this server node
        """
        if parse and getattr(self.options, 'companies', None):
            for name in self.options.companies:
                name = _decode(name) # can optparse give us unicode?
                try:
                    yield Company(name, self)
                except MAPIErrorNoSupport:
                    raise NotFoundError('no such company: %s' % name)
            return
        try:
            for name in self._companylist():
                yield Company(name, self)
        except MAPIErrorNoSupport:
            yield Company(u'Default', self)

    def create_company(self, name): # XXX deprecated because of company(create=True)?
        name = _unicode(name)
        try:
            companyeid = self.sa.CreateCompany(ECCOMPANY(name, None), MAPI_UNICODE)
        except MAPIErrorCollision:
            raise DuplicateError("company '%s' already exists" % name)
        except MAPIErrorNoSupport:
            raise NotSupportedError("cannot create company in singletenant mode")
        return self.company(name)

    def _store(self, guid):
        if len(guid) != 32:
            raise Error("invalid store id: '%s'" % guid)
        try:
            storeid = _unhex(guid)
        except:
            raise Error("invalid store id: '%s'" % guid)
        table = self.ems.GetMailboxTable(None, 0) # XXX merge with Store.__init__
        table.SetColumns([PR_ENTRYID], 0)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_STORE_RECORD_KEY, SPropValue(PR_STORE_RECORD_KEY, storeid)), TBL_BATCH)
        for row in table.QueryRows(-1, 0):
            return self.mapisession.OpenMsgStore(0, row[0].Value, None, MDB_WRITE)
        raise NotFoundError("no such store: '%s'" % guid)

    def _store2(self, storeid): # XXX max lifetime
        if storeid not in self._store_cache:
            self._store_cache[storeid] = self.mapisession.OpenMsgStore(0, storeid, IID_IMsgStore, MDB_WRITE)
        return self._store_cache[storeid]

    def groups(self):
        """ Return all :class:`groups <Group>` on server """

        for name in MAPI.Util.AddressBook.GetGroupList(self.mapisession, None, MAPI_UNICODE):
            yield Group(name, self)

    def group(self, name):
        """ Return :class:`group <Group>` with given name """

        return Group(name, self)

    def create_group(self, name, fullname='', email='', hidden = False, groupid = None):
        name = _unicode(name) # XXX: fullname/email unicode?
        email = _unicode(email)
        fullname = _unicode(fullname)
        try:
            companyeid = self.sa.CreateGroup(ECGROUP(name, fullname, email, int(hidden), groupid), MAPI_UNICODE)
        except MAPIErrorCollision:
            raise DuplicateError("group '%s' already exists" % name)

        return self.group(name)

    def remove_group(self, name):
        group = self.group(name)
        self.sa.DeleteGroup(group._ecgroup.GroupID)

    def delete(self, items):
        if isinstance(items, (User, Group, Company, Store)):
            items = [items]
        else:
            items = list(items)

        for item in items:
            if isinstance(item, User):
                self.remove_user(item.name)
            elif isinstance(item, Group):
                self.remove_group(item.name)
            elif isinstance(item, Company):
                self.remove_company(item.name)
            elif isinstance(item, Store):
                self.remove_store(item)

    def _pubstore(self, name):
        if name == 'public':
            if not self.public_store:
                raise NotFoundError("no public store")
            return self.public_store
        else:
            company = Company(name.split('@')[1])
            if not company.public_store:
                raise NotFoundError("no public store for company '%s'" % company.name)
            return company.public_store

    def store(self, guid=None, entryid=None):
        """ Return :class:`store <Store>` with given GUID """

        if _unicode(guid).split('@')[0] == 'public':
            return self._pubstore(guid)
        else:
            return Store(guid=guid, entryid=entryid, server=self)

    def get_store(self, guid):
        """ Return :class:`store <Store>` with given GUID or *None* if not found """

        try:
            return self.store(guid)
        except Error:
            pass

    def stores(self, system=False, remote=False, parse=True): # XXX implement remote
        """ Return all :class:`stores <Store>` on server node

        :param system: include system stores
        :param remote: include stores on other nodes

        """

        if parse and getattr(self.options, 'stores', None):
            for guid in self.options.stores:
                if guid.split('@')[0] == 'public':
                    yield self._pubstore(guid)
                else:
                    yield Store(guid, server=self)
            return

        table = self.ems.GetMailboxTable(None, 0)
        table.SetColumns([PR_DISPLAY_NAME_W, PR_ENTRYID], 0)
        for row in table.QueryRows(-1, 0):
            store = Store(mapiobj=self.mapisession.OpenMsgStore(0, row[1].Value, None, MDB_WRITE), server=self) # XXX cache
            if system or store.public or (store.user and store.user.name != 'SYSTEM'):
                yield store

    def create_store(self, public=False): # XXX deprecated?
        if public:
            return self.create_public_store()
        # XXX

    def remove_store(self, store): # XXX server.delete?
        self.sa.RemoveStore(_unhex(store.guid))

    def sync_users(self):
        self.sa.SyncUsers(None)

    def clear_cache(self): # XXX specify one or more caches?
        self.sa.PurgeCache(PURGE_CACHE_ALL)

    def purge_softdeletes(self, days):
        self.sa.PurgeSoftDelete(days)

    def purge_deferred(self): # XXX purge all at once?
        try:
            return self.sa.PurgeDeferredUpdates() # remaining records
        except MAPIErrorNotFound:
            return 0

    def _pubhelper(self):
        try:
            self.sa.GetCompanyList(MAPI_UNICODE)
            raise Error('request for server-wide public store in multi-company setup')
        except MAPIErrorNoSupport:
            return next(self.companies())

    @property
    def public_store(self):
        """ public :class:`store <Store>` in single-company mode """

        return self._pubhelper().public_store

    def create_public_store(self):

        return self._pubhelper().create_public_store()

    def hook_public_store(self, store):

        return self._pubhelper().hook_public_store(store)

    def unhook_public_store(self):

        return self._pubhelper().unhook_public_store()

    @property
    def state(self):
        """ Current server state """

        return _state(self.mapistore)

    def sync(self, importer, state, log=None, max_changes=None, window=None, begin=None, end=None, stats=None):
        """ Perform synchronization against server node

        :param importer: importer instance with callbacks to process changes
        :param state: start from this state (has to be given)
        :log: logger instance to receive important warnings/errors
        """

        importer.store = None
        return _sync(self, self.mapistore, importer, state, log or self.log, max_changes, window=window, begin=begin, end=end, stats=stats)

    @_timed_cache(minutes=60)
    def _resolve_email(self, entryid=None):
        try:
            mailuser = self.mapisession.OpenEntry(entryid, None, 0)
            return self.user(HrGetOneProp(mailuser, PR_ACCOUNT_W).Value).email # XXX PR_SMTP_ADDRESS_W from mailuser?
        except (Error, MAPIErrorNotFound): # XXX deleted user
            return '' # XXX groups

    def __unicode__(self):
        return u'Server(%s)' % self.server_socket

    def __repr__(self):
        return _repr(self)

