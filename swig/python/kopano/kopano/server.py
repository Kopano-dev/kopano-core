# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano.

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import atexit
import codecs
import gc
import datetime
import functools
import os
import time
import socket
import sys
import warnings

from MAPI import (
    MAPI_UNICODE, MDB_WRITE, RELOP_EQ, MAPI_MAILUSER, TBL_BATCH,
    ECSTORE_TYPE_PRIVATE, DT_REMOTE_MAILUSER, RELOP_NE, FL_FULLSTRING,
    FL_IGNORECASE, BOOKMARK_BEGINNING, EC_OVERRIDE_HOMESERVER, WrapStoreEntryID
)
from MAPI.Util import (
    GetDefaultStore, OpenECSession
)
from MAPI.Defs import (
    HrGetOneProp,
)

from MAPI.Struct import (
    SPropertyRestriction, SPropValue, ECCOMPANY, ECGROUP, ECUSER,
    MAPIErrorNotFound, MAPIErrorNoSupport, MAPIErrorCollision,
    MAPIErrorLogonFailed, MAPIErrorNetworkError, MAPIErrorDiskError,
    SAndRestriction, SContentRestriction,
)
from MAPI.Tags import (
    PR_ACCOUNT_W, PURGE_CACHE_ALL, PR_DISPLAY_NAME_W, PR_ENTRYID,
    PR_STORE_RECORD_KEY, PR_MAPPING_SIGNATURE,
    PR_EC_STATSTABLE_SYSTEM, PR_EC_STATSTABLE_SESSIONS,
    PR_EC_STATSTABLE_USERS, PR_EC_STATSTABLE_COMPANY, PR_DISPLAY_NAME,
    PR_EC_STATSTABLE_SERVERS, PR_EC_STATS_SERVER_HTTPSURL,
    PR_STORE_ENTRYID, EC_PROFILE_FLAGS_NO_UID_AUTH, PR_EC_STATS_SYSTEM_VALUE,
    EC_PROFILE_FLAGS_NO_NOTIFICATIONS, EC_PROFILE_FLAGS_OIDC,
    PR_FREEBUSY_ENTRYIDS,
    PR_OBJECT_TYPE, PR_DISPLAY_TYPE,
)
from MAPI.Tags import (
    IID_IMsgStore, IID_IMAPITable, IID_IExchangeManageStore,
    IID_IECServiceAdmin
)

from .errors import (
    Error, NotFoundError, DuplicateError, NotSupportedError,
    LogonError, ArgumentError, _DeprecationWarning
)
from .log import LOG, _loglevel

from .parser import parser
from .table import Table
from .company import Company
from .group import Group
from .query import _query_to_restriction

from .compat import (
    repr as _repr, benc as _benc,
    bdec as _bdec, fake_unicode as _unicode,
)

try:
    from . import user as _user
except ImportError: # pragma: no cover
    _user = sys.modules[__package__ + '.user']
try:
    from . import config as _config
except ImportError: # pragma: no cover
    _config = sys.modules[__package__ + '.config']
from . import ics as _ics
try:
    from . import store as _store
except ImportError: # pragma: no cover
    _store = sys.modules[__package__ + '.store']
try:
    from . import utils as _utils
except ImportError: # pragma: no cover
    _utils = sys.modules[__package__ + '.utils']

def _timed_cache(seconds=0, minutes=0, hours=0, days=0):
    # used with permission from will mcgugan, https://www.willmcgugan.com
    time_delta = datetime.timedelta(
        seconds=seconds, minutes=minutes, hours=hours, days=days)

    def decorate(f):
        f._updates = {}
        f._results = {}

        def do_cache(*args, **kwargs):
            key = tuple(sorted(kwargs.items()))

            updates = f._updates
            results = f._results

            t = datetime.datetime.now()
            updated = updates.get(key, t)

            if key not in results or t - updated > time_delta:
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

def instance_method_lru_cache(*cache_args, **cache_kwargs):
    # Just like functools.lru_cache, but a new cache is created for each
    # instance of the class that owns the method this is applied to.
    # Snippet by Alex Fraser (https://github.com/z0u, alex@phatcore.com)
    def cache_decorator(func):
        @functools.wraps(func)
        def cache_factory(self, *args, **kwargs):
            # Wrap the function in a cache by calling the decorator
            instance_cache = functools.lru_cache(
                *cache_args, **cache_kwargs)(func)
            # Bind the decorated function to the instance to make it a method
            instance_cache = instance_cache.__get__(self, self.__class__)
            setattr(self, func.__name__, instance_cache)
            # Call the instance cache now. Next time the method is called, the
            # call will go directly to the instance cache and not via this
            # decorator.
            return instance_cache(*args, **kwargs)
        return cache_factory
    return cache_decorator

# python does not always seem to release modules at shutdown:
#
# https://bugs.python.org/issue9072
#
# also, for the tests we've seen SWIG objects end up here:
#
# https://docs.python.org/2/library/gc.html#gc.garbage
#
# in both cases, store objects are not finalized, so the associated memory
# on the C side is not released, causing valgrind to report many leaks.
#
# to avoid valgrind going nuts, we manually clean up references at shutdown.
# for the second case, this may only be a work-around as it may be too late.
#
# for python3, both problems may have been solved already:
#
# https://www.python.org/dev/peps/pep-0442/
#
# and if not, python3 provides a work-around we could use, so manual
# clearing can be done each collection:
#
# https://docs.python.org/3/library/gc.html#gc.callbacks
#
# of course we might also try to prevent cycles in the first place.
#
# TODO valgrind python3

def _cleanup_mapistores():
    for obj in gc.get_objects():
        if isinstance(obj, (Server, _store.Store)):
            obj.__dict__.clear()
        elif (hasattr(obj, '__class__') and \
              obj.__class__.__name__ in ('IMAPISession', 'IMsgStore',
                  'IECServiceAdmin', 'IExchangeManageStore')):
            obj.__dict__.clear()

    del gc.garbage[:]
    gc.collect()

if os.getenv('ZCPSRCDIR'):
    atexit.register(_cleanup_mapistores)

class Options(object):
    pass

class Server(object):
    """Server class.

    Abstraction for Kopano servers. A MAPI session is automatically setup,
    according to the passed arguments and environment.
    """

    def __init__(self, options=None, config=None, sslkey_file=None,
            sslkey_pass=None, server_socket=None, auth_user=None,
            auth_pass=None, log=None, service=None, mapisession=None,
            parse_args=False, notifications=False, store_cache=True,
            oidc=False, _skip_check=True):
        """
        Create Server instance.

        By default, tries to connect to a storage server as configured in
        ``/etc/kopano/admin.cfg`` or at UNIX socket
        ``/var/run/kopano/server.sock``

        Looks at command-line to see if another server address or other
        related options were given (such as -c, -s, -k, -p)

        :param server_socket: similar to 'server_socket' option in config file
        :param sslkey_file: similar to 'sslkey_file' option in config file
        :param sslkey_pass: similar to 'sslkey_pass' option in config file
        :param config: path of configuration file containing common server
            options, for example ``/etc/kopano/admin.cfg``
        :param auth_user: username to user for user authentication
        :param auth_pass: password to use for user authentication
        :param options: OptionParser instance to get settings from
            (see:func:`parser`)
        :param parse_args: set this True if cli arguments should be parsed
        """
        self.options = options
        self.config = config
        self.sslkey_file = sslkey_file
        self.sslkey_pass = sslkey_pass
        self.server_socket = server_socket
        self.service = service
        if log: # TODO deprecate?
            self.log = log
        elif service:
            self.log = service.log
        elif config:
            self.log = LOG
            self.log.setLevel(_loglevel(options, config))
        else:
            self.log = LOG
        self.mapisession = mapisession
        self.store_cache = store_cache

        if not _skip_check:
            warnings.warn('use kopano.server instead of kopano.Server',
                _DeprecationWarning)

        if not self.mapisession:
            # get cmd-line options
            if not self.options:
                if parse_args:
                    self.options, _ = parser().parse_args()
                else:
                    self.options = Options()

            # determine config file
            if config:
                pass
            elif getattr(self.options, 'config_file', None):
                config_file = os.path.abspath(self.options.config_file)
                config = _config.Config(
                    None, filename=self.options.config_file)
            else:
                config_file = '/etc/kopano/admin.cfg'
                try:
                    open(config_file) # check if accessible
                    config = _config.Config(None, filename=config_file)
                except IOError:
                    pass
            self.config = config

            # get defaults
            if os.getenv("KOPANO_SOCKET"): # env variable used in testset
                self.server_socket = os.getenv("KOPANO_SOCKET")
            elif config:
                if (not (server_socket or \
                    getattr(self.options, 'server_socket', None))):
                    self.server_socket = config.get('server_socket')
                    self.sslkey_file = config.get('sslkey_file')
                    self.sslkey_pass = config.get('sslkey_pass')
            self.server_socket = self.server_socket or "default:"

            # override with explicit or command-line args
            self.server_socket = server_socket or \
                getattr(self.options, 'server_socket', None) or \
                self.server_socket
            self.sslkey_file = sslkey_file or \
                getattr(self.options, 'sslkey_file', None) or \
                self.sslkey_file
            self.sslkey_pass = sslkey_pass or \
                getattr(self.options, 'sslkey_pass', None) or \
                self.sslkey_pass

            # make actual connection. in case of service,
            # wait until this succeeds.
            self.auth_user = auth_user or \
                getattr(self.options, 'auth_user', None) or \
                ''
            self.auth_pass = auth_pass or \
                getattr(self.options, 'auth_pass', None) or \
                ''

            flags = 0
            self.notifications = notifications
            if not notifications:
                flags |= EC_PROFILE_FLAGS_NO_NOTIFICATIONS

            # Username and password was supplied, so let us do verfication
            # (OpenECSession will not check password unless this parameter
            # is provided)
            if self.auth_user and self.auth_pass:
                flags |= EC_PROFILE_FLAGS_NO_UID_AUTH

            if oidc:
                flags |= EC_PROFILE_FLAGS_OIDC
            elif not self.auth_user:
                self.auth_user = "SYSTEM"

            while True:
                try:
                    self.mapisession = OpenECSession(
                        self.auth_user,
                        self.auth_pass,
                        self.server_socket,
                        sslkey_file=self.sslkey_file,
                        sslkey_pass=self.sslkey_pass,
                        flags=flags)
                    break
                except (MAPIErrorNetworkError, MAPIErrorDiskError):
                    if service:
                        self.log.warning("could not connect to server at \
'%s', retrying in 5 sec", self.server_socket)
                        time.sleep(5)
                    else:
                        raise Error("could not connect to server at '%s'" %
                            self.server_socket)
                except MAPIErrorLogonFailed:
                    raise LogonError('Could not logon to server: username or \
password incorrect')

        self._mapistore = None
        self._sa = None
        self._ems = None
        self._ab = None
        self._admin_store = None
        self._gab = None
        self._pseudo_url = None
        self._name = None

    @property
    def mapistore(self):
        if self._mapistore is None:
            self._mapistore = GetDefaultStore(self.mapisession)
        return self._mapistore

    @property
    def sa(self):
        if self._sa is None:
            self._sa = self.mapistore.QueryInterface(IID_IECServiceAdmin)
        return self._sa

    @property
    def ems(self):
        if self._ems is None:
            self._ems = self.mapistore.QueryInterface(IID_IExchangeManageStore)
        return self._ems

    @property
    def pseudo_url(self):
        if self._pseudo_url is None:
            entryid = HrGetOneProp(self.mapistore, PR_STORE_ENTRYID).Value
            # TODO ECSERVER
            self._pseudo_url = entryid[entryid.find(b'pseudo:'):-1]
        return self._pseudo_url

    @property
    def name(self):
        """Server name."""
        if self._name is None:
            # TODO encoding, get this kind of stuff from
            # pr_ec_statstable_servers..?
            self._name = self.pseudo_url[9:].decode('ascii')
        return self._name

    # TODO delay mapi sessions until actually needed
    def nodes(self):
        """For a multi-server setup, return all servers (nodes)."""
        for row in self.table(PR_EC_STATSTABLE_SERVERS).dict_rows():
            yield Server(options=self.options, config=self.config,
                sslkey_file=self.sslkey_file, sslkey_pass=self.sslkey_pass,
                server_socket=codecs.decode(
                    row[PR_EC_STATS_SERVER_HTTPSURL], 'utf-8'),
                log=self.log, service=self.service, _skip_check=True)

    def table(self, name, restriction=None, order=None, columns=None):
        return Table(
            self,
            self.mapistore,
            self.mapistore.OpenProperty(name, IID_IMAPITable, MAPI_UNICODE, 0),
            name,
            restriction=restriction,
            order=order,
            columns=columns,
        )

    def tables(self):
        for table in (
            PR_EC_STATSTABLE_SYSTEM,
            PR_EC_STATSTABLE_SESSIONS,
            PR_EC_STATSTABLE_USERS,
            PR_EC_STATSTABLE_COMPANY,
            PR_EC_STATSTABLE_SERVERS):
            try:
                yield self.table(table)
            except MAPIErrorNotFound:
                pass

    @property
    def ab(self):
        if not self._ab:
            self._ab = self.mapisession.OpenAddressBook(0, None, 0) # TODO
        return self._ab

    @property
    def admin_store(self):
        """Admin store."""
        if not self._admin_store:
            self._admin_store = _store.Store(
                mapiobj=self.mapistore, server=self)
        return self._admin_store

    @property
    def gab(self):
        # TODO deprecate, because MAPI-level
        if not self._gab:
            self._gab = self.ab.OpenEntry(self.ab.GetDefaultDir(), None, 0)
        return self._gab

    @property
    def guid(self):
        """Server GUID."""
        return _benc(HrGetOneProp(self.mapistore, PR_MAPPING_SIGNATURE).Value)

    def user(self, name=None, email=None, create=False, userid=None):
        """:class:`User <User>` with given name, email address or userid.

        :param name: User name (optional)
        :param email: User email address (optional)
        :param userid: User userid (optional)
        :param create: create user if it doesn't exist (default False,
            name required)
        """

        if not (name or email or userid):
            raise ArgumentError('missing argument to identify user')

        try:
            return _user.User(name, email=email, server=self, userid=userid)
        except NotFoundError:
            if create and name:
                return self.create_user(name)
            else:
                raise

    def get_user(self, name):
        """Return :class:`user <User>` with given name or *None* if not
        found."""
        try:
            return self.user(name)
        except NotFoundError:
            pass

    # TODO change default for hidden
    def users(self, remote=False, system=False, parse=True, page_start=None,
              page_limit=None, order=None, hidden=True, inactive=True,
              _server=None, _company=None, query=None):
        """Return all :class:`users <User>` on server.

        :param remote: Include users on remote server nodes (default False)
        :param system: Include system users (default False)
        :param hidden: Include hidden users (default True)
        :param inactive: Include inactive users (default True)
        :param query: Search query (optional)
        """

        # users specified on command-line
        if parse and getattr(self.options, 'users', None):
            for username in self.options.users:
                yield _user.User(username, self)
            return

        # global listing on multitenant setup
        if self.multitenant and not _company:
            if (page_limit is None and page_start is None and \
                query is None and order is None):
                for company in self.companies():
                    for user in company.users(remote=remote, system=system,
                            parse=parse, hidden=hidden, inactive=inactive):
                        yield user
                return
            else:
                raise NotSupportedError('unsupported method of user listing')

        # find right addressbook container (global or for company)
        gab = self.gab

        restriction = SAndRestriction([
            SPropertyRestriction(RELOP_EQ, PR_OBJECT_TYPE,
                SPropValue(PR_OBJECT_TYPE, MAPI_MAILUSER)),
            SPropertyRestriction(RELOP_NE, PR_DISPLAY_TYPE,
                SPropValue(PR_DISPLAY_TYPE, DT_REMOTE_MAILUSER))
        ])

        if _company and _company.name != 'Default':
            htable = gab.GetHierarchyTable(0)
            htable.SetColumns([PR_ENTRYID], TBL_BATCH)
            # TODO(longsleep): Find a way to avoid finding the limited table,
            # if the gab is itself already limited to the same.
            try:
                htable.FindRow(SContentRestriction(
                        FL_FULLSTRING | FL_IGNORECASE,
                        PR_DISPLAY_NAME_W,
                        SPropValue(PR_DISPLAY_NAME_W, _company.name)),
                    BOOKMARK_BEGINNING, 0)
            except MAPIErrorNotFound:
                # If not, found we do not have permission to access that row. and
                # instead fall back to the gab and let it handle access and
                # limits based on user authentication.
                container = gab
            else:
                row = htable.QueryRows(1, 0)[0]
                container = gab.OpenEntry(row[0].Value, None, 0)
        else:
            container = gab

        table = container.GetContentsTable(0)
        table.SetColumns([PR_ENTRYID], MAPI_UNICODE)

        # apply query restriction
        if query is not None:
            store = _store.Store(mapiobj=self.mapistore, server=self)
            restriction.lpRes.append(
                _query_to_restriction(query, 'user', store).mapiobj)

        table.Restrict(restriction, TBL_BATCH)

        # TODO apply order argument here

        def include(user, ecuser):
            return ((system or user.name != 'SYSTEM') and
                    (remote or ecuser.Servername in (self.name, '')) and
                    (hidden or not user.hidden) and
                    (inactive or user.active))

        # TODO simpler/faster if sa.GetUserList could do restrictions,
        # ordering, pagination..
        # since then we can always work with ecuser objects in bulk
        userid_ecuser = None
        if page_limit is None and page_start is None and query is None:
            userid_ecuser = {}
            if _company and _company.name != 'Default':
                ecusers = self.sa.GetUserList(
                    _company._eccompany.CompanyID, MAPI_UNICODE)
            else:
                ecusers = self.sa.GetUserList(None, MAPI_UNICODE)
            for ecuser in ecusers:
                userid_ecuser[ecuser.UserID] = ecuser

                # fast path: just get all users
                if order is None:
                    user = _user.User(server=self, ecuser=ecuser)
                    if include(user, ecuser):
                        yield user
            if order is None:
                return

        # loop over rows and paginate
        pos = 0
        count = 0

        while True:
            rows = table.QueryRows(50, 0)
            if not rows:
                break
            for row in rows:
                userid = row[0].Value
                if userid_ecuser is None:
                    try:
                        user = _user.User(server=self, userid=_benc(userid))
                    except NotFoundError:
                        continue
                else:
                    try:
                        user = _user.User(
                            server=self, ecuser=userid_ecuser[userid])
                    except KeyError:
                        continue

                if include(user, user._ecuser):
                    if page_start is None or pos >= page_start:
                        yield user
                        count += 1
                    if page_limit is not None and count >= page_limit:
                        return
                    pos += 1

    def create_user(self, name, email=None, password=None, company=None,
            fullname=None, create_store=True):
        """Create a new :class:`user <User>` on the server.

        :param name: User login name
        :param email: User email address (optional)
        :param password: User login password (optional)
        :param company: User company (optional)
        :param fullname: User full name (optional)
        :param create_store: Should a store be created (default True)
        """
        fullname = _unicode(fullname or name or '')
        name = _unicode(name)
        if email:
            email = _unicode(email)
        else:
            email = '%s@%s' % (name, socket.gethostname())
        if password:
            password = _unicode(password)
        if company:
            company = _unicode(company)
        if company and company != 'Default':
            self.sa.CreateUser(ECUSER('%s@%s' % (name, company), password,
                email, fullname), MAPI_UNICODE)
            user = self.company(company).user('%s@%s' % (name, company))
        else:
            try:
                self.sa.CreateUser(
                    ECUSER(name, password, email, fullname),MAPI_UNICODE)
            except MAPIErrorNoSupport:
                raise NotSupportedError(
                    "cannot create users with configured user plugin")
            except MAPIErrorCollision:
                raise DuplicateError("user '%s' already exists" % name)
            user = self.user(name)
        if create_store:
            try:
                self.sa.CreateStore(ECSTORE_TYPE_PRIVATE, _bdec(user.userid))
            except MAPIErrorCollision:
                pass # create-user userscript may already create store
        return user

    def remove_user(self, name): # TODO delete(object)?
        """Remove :class:`User`.

        :param name: User login name
        """
        user = self.user(name)
        self.sa.DeleteUser(user._ecuser.UserID)

    def company(self, name, create=False):
        """Return :class:`company <Company>` with given name.

        :param name: Company name
        :param create: Create company if it doesn't exist (default False)
        """
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
        """:class:`Company <Company>` with given name or *None* if not found.

        :param name: Company name
        """
        try:
            return self.company(name)
        except NotFoundError:
            pass

    def remove_company(self, name): # TODO delete(object)?
        """Remove :class:`Company`.

        :param name: Company name
        """
        company = self.company(name)

        if company.name == 'Default':
            raise NotSupportedError(
                'cannot remove company in single-tenant mode')
        else:
            self.sa.DeleteCompany(company._eccompany.CompanyID)

    def _companylist(self):
        return [eccompany.Companyname for eccompany in \
            self.sa.GetCompanyList(MAPI_UNICODE)]

    @property
    def multitenant(self):
        """The server is multi-tenant (multiple :class:`companies <Company>`)."""
        try:
            self._companylist()
            return True
        except MAPIErrorNoSupport:
            return False

    def companies(self, remote=False, parse=True): # TODO remote?
        """Return all :class:`companies <Company>` on server.

        :param remote: include companies without users on this server node
            (default False)
        :param parse: take cli argument --companies into account
            (default True)
        """
        if parse and getattr(self.options, 'companies', None):
            for name in self.options.companies:
                try:
                    yield Company(name, self)
                except MAPIErrorNoSupport:
                    raise NotFoundError('no such company: %s' % name)
            return
        try:
            for name in self._companylist():
                yield Company(name, self)
        except MAPIErrorNoSupport:
            yield Company('Default', self)

    def create_company(self, name):
        """Create a new :class:`company <Company>` on the server.

        :param name: Company name
        """
        name = _unicode(name)
        try:
            self.sa.CreateCompany(ECCOMPANY(name, None), MAPI_UNICODE)
        except MAPIErrorCollision:
            raise DuplicateError("company '%s' already exists" % name)
        except MAPIErrorNoSupport:
            raise NotSupportedError(
                'cannot create company in single-tenant mode')
        return self.company(name)

    # TODO move guid checks to caller
    def _store(self, guid):
        if len(guid) != 32:
            raise Error("invalid store id: '%s'" % guid)
        try:
            storeid = _bdec(guid)
        except:
            raise Error("invalid store id: '%s'" % guid)
        # TODO merge with Store.__init__
        table = self.ems.GetMailboxTable(None, 0)
        table.SetColumns([PR_ENTRYID], 0)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_STORE_RECORD_KEY,
            SPropValue(PR_STORE_RECORD_KEY, storeid)), TBL_BATCH)
        for row in table.QueryRows(-1, 0):
            return self._store2(row[0].Value)
        raise NotFoundError("no such store: '%s'" % guid)

    @instance_method_lru_cache(128) # backend doesn't like too many open stores
    def _store_cached(self, storeid):
        return self.mapisession.OpenMsgStore(
            0, storeid, IID_IMsgStore, MDB_WRITE)

    def _store2(self, storeid): # TODO max lifetime?
        if self.store_cache:
            return self._store_cached(storeid)
        else:
            return self.mapisession.OpenMsgStore(
                0, storeid, IID_IMsgStore, MDB_WRITE)

    def groups(self):
        """Return all :class:`groups <Group>` on the server."""
        try:
            for ecgroup in self.sa.GetGroupList(None, MAPI_UNICODE):
                yield Group(ecgroup.Groupname, self)
        except NotFoundError:
            # TODO what to do here (single-tenant..), as groups do exist?
            pass

    def group(self, name, create=False):
        """Return :class:`group <Group>` with given name.

        :param name: Group name
        :param create: Create group if it doesn't exist (default False)
        """
        try:
            return Group(name, self)
        except NotFoundError:
            if create:
                return self.create_group(name)
            else:
                raise

    def create_group(self, name, fullname='', email='', hidden=False,
            groupid=None):
        """Create a new :class:`group <Group>` on the server.

        :param name: the name of the group
        :param fullname: the full name of the group (optional)
        :param email: the email address of the group (optional)
        :param hidden: hide the group (optional)
        :param groupid: the id of the group (optional)
        """
        name = _unicode(name) # TODO: fullname/email unicode?
        email = _unicode(email)
        fullname = _unicode(fullname)
        try:
            self.sa.CreateGroup(ECGROUP(name, fullname, email, int(hidden),
                groupid), MAPI_UNICODE)
        except MAPIErrorNoSupport:
            raise NotSupportedError(
                'cannot create groups with configured user plugin')
        except MAPIErrorCollision:
            raise DuplicateError("group '%s' already exists" % name)

        return self.group(name)

    def remove_group(self, name): # TODO delete
        """Remove :class:`group <Group>`.

        :param name: Group name
        """
        group = self.group(name)
        self.sa.DeleteGroup(group._ecgroup.GroupID)

    def delete(self, objects):
        """Delete users, groups, companies or stores from server.

        :param objects: The object(s) to delete
        """
        objects = _utils.arg_objects(objects,
            (_user.User, Group, Company, _store.Store), 'Server.delete')

        for item in objects:
            if isinstance(item, _user.User):
                self.remove_user(item.name)
            elif isinstance(item, Group):
                self.remove_group(item.name)
            elif isinstance(item, Company):
                self.remove_company(item.name)
            elif isinstance(item, _store.Store):
                self.remove_store(item)

    def _pubstore(self, name):
        if name == 'public':
            if not self.public_store:
                raise NotFoundError("no public store")
            return self.public_store
        else:
            company = Company(name.split('@')[1], self)
            if not company.public_store:
                raise NotFoundError(
                    "no public store for company '%s'" % company.name)
            return company.public_store

    def store(self, guid=None, entryid=None):
        """Return :class:`store <Store>` with given GUID or entryid.

        :param guid: Store GUID (optional)
        :param entryid: Store entryid (optional)
        """
        if isinstance(guid, str) and _unicode(guid).split('@')[0] == 'public':
            return self._pubstore(guid)
        else:
            return _store.Store(guid=guid, entryid=entryid, server=self)

    def get_store(self, guid):
        """Return :class:`store <Store>` with given GUID or
        *None* if not found."""
        try:
            return self.store(guid)
        except Error:
            pass

    # TODO implement remote
    def stores(self, system=False, remote=False, parse=True):
        """Return all :class:`stores <Store>` on the server node.

        :param system: Include system stores (default False)
        :param remote: Include stores on other nodes (default False)
        """
        if parse and getattr(self.options, 'stores', None):
            for guid in self.options.stores:
                if guid.split('@')[0] == 'public':
                    yield self._pubstore(guid)
                elif len(guid) == 32:
                    yield _store.Store(guid=guid, server=self)
                else:
                    yield _store.Store(entryid=guid, server=self)
            return

        table = self.ems.GetMailboxTable(None, 0)
        table.SetColumns([PR_DISPLAY_NAME_W, PR_ENTRYID], 0)
        for row in table.QueryRows(-1, 0):
            store = _store.Store(
                mapiobj=self._store2(row[1].Value),server=self)
            if not system and store.user and store.user.name == 'SYSTEM':
                continue
            yield store

    def create_store(self, user, _msr=False):
        """Create store for :class:`User`.

        :param user: User
        """
        # TODO detect homeserver override
        storetype = ECSTORE_TYPE_PRIVATE
        # TODO configurable storetype

        if _msr:
            try:
                storeid, rootid = self.sa.CreateEmptyStore(storetype,
                    _bdec(user.userid), EC_OVERRIDE_HOMESERVER, None, None)
            except MAPIErrorCollision:
                raise DuplicateError("user '%s' already has an associated \
store (unhook first?)" % user.name)
            store_entryid = \
                WrapStoreEntryID(0, b'zarafa6client.dll',storeid[:-4]) + \
                b'https://' + \
                codecs.encode(self.name, 'utf-8') + \
                b':237\x00'
            # multi-server flag
            store_entryid = \
                store_entryid[:66] + \
                b'\x10' + \
                store_entryid[67:]
            store = self.store(entryid=_benc(store_entryid))

            # TODO add EC_OVERRIDE_HOMESERVER flag to CreateStore instead?
            # or how does the old MSR do this?

            # TODO language

            # system folders
            root = store.root

            store.findroot = root.create_folder('FINDER_ROOT')
            store.findroot.permission(
                self.group('Everyone'), create=True).rights = \
                ['read_items', 'create_subfolders', 'edit_own', 'delete_own',
                 'folder_visible']

            store.views = root.create_folder('IPM_VIEWS')
            store.common_views = root.create_folder('IPM_COMMON_VIEWS')

            root.create_folder('Freebusy Data')
            root.create_folder('Schedule')
            root.create_folder('Shortcut')

            # special folders
            subtree = store.subtree = root.folder('IPM_SUBTREE', create=True)

            store.calendar = subtree.create_folder('Calendar')
            store.contacts = subtree.create_folder('Contacts')
            # TODO Conversation Action Settings?
            store.wastebasket = subtree.create_folder('Deleted Items')
            store.drafts = subtree.create_folder('Drafts')
            store.inbox = subtree.create_folder('Inbox')
            store.journal = subtree.create_folder('Journal')
            store.junk = subtree.create_folder('Junk E-mail')
            store.notes = subtree.create_folder('Notes')
            store.outbox = subtree.create_folder('Outbox')
            # TODO Quick Step Settings?
            # TODO RSS Feeds?
            store.sentmail = subtree.create_folder('Sent Items')
            # TODO Suggested Contacts?
            store.tasks = subtree.create_folder('Tasks')

            # freebusy message TODO create dynamically instead?
            fbmsg = root.create_item(
                subject='LocalFreebusy',
                message_class='IPM.Microsoft.ScheduleData.FreeBusy'
            )
            root[PR_FREEBUSY_ENTRYIDS] = [b'', _bdec(fbmsg.entryid), b'', b'']

        else:
            store = user.create_store()
        return store

    def remove_store(self, store):
        """Remove :class:`Store`.

        :param store: Store
        """
        try:
            self.sa.RemoveStore(_bdec(store.guid))
        except MAPIErrorCollision:
            raise Error("cannot remove store with GUID '%s'" % store.guid)

    def sync_users(self):
        """Synchronize users with external source."""
        self.sa.SyncUsers(None)

    def clear_cache(self): # TODO specify one or more caches?
        """Clear caches."""
        self.sa.PurgeCache(PURGE_CACHE_ALL)

    def purge_softdeletes(self, days):
        """Purge soft-deletes older than certain amount of days.

        :param days: Amount of days in the past.
        """
        self.sa.PurgeSoftDelete(days)

    def purge_deferred(self): # TODO purge all at once?
        """Purge deferred updates."""
        try:
            return self.sa.PurgeDeferredUpdates() # remaining records
        except MAPIErrorNotFound:
            return 0

    def _pubhelper(self):
        try:
            self.sa.GetCompanyList(MAPI_UNICODE)
            raise NotSupportedError('request for server-wide public store in \
multi-tenant setup')
        except MAPIErrorNoSupport:
            return next(self.companies())

    @property
    def public_store(self):
        """Public :class:`store <Store>` (single-tenant mode)."""
        return self._pubhelper().public_store

    def create_public_store(self):
        """Create public :class:`store <Store>` (single-tenant mode)."""
        return self._pubhelper().create_public_store()

    def hook_public_store(self, store):
        """Hook public :class:`store <Store>` (single-tenant mode).

        :param store: store to hook
        """
        return self._pubhelper().hook_public_store(store)

    def unhook_public_store(self):
        """Unhook public :class:`store <Store>` (single-tenant mode)."""
        return self._pubhelper().unhook_public_store()

    @property
    def state(self):
        """Server state (for use with ICS synchronization)."""
        return _ics.state(self.mapistore)

    def sync(self, importer, state, log=None, max_changes=None, window=None,
            begin=None, end=None, stats=None):
        """Perform ICS synchronization against server node.

        :param importer: importer instance with callbacks to process changes
        :param state: start from this state (has to be given)
        :log: logger instance to receive important warnings/errors
        """
        importer.store = None
        return _ics.sync(self, self.mapistore, importer, state, max_changes,
            window=window, begin=begin, end=end, stats=stats)

    def sync_gab(self, importer, state=None):
        """Perform ICS synchronization against global address book.

        :param importer: importer instance with callbacks to process changes
        :param state: start from this state (optional)
        """
        if state is None:
            state = _benc(8 * b'\0')
        return _ics.sync_gab(self, self.mapistore, importer, state)

    def stats(self):
        """Dictionary containing useful server statistics."""
        table = self.table(PR_EC_STATSTABLE_SYSTEM)
        stats = {}
        # TODO use *_W?
        for key, value in table.dict_(
            PR_DISPLAY_NAME, PR_EC_STATS_SYSTEM_VALUE).items():
            stats[key] = value

        # TODO shouldn't be necessary
        stats = dict([(s.decode('UTF-8'), stats[s].decode('UTF-8')) \
            for s in stats])

        return stats

    def stat(self, key): # TODO optimize with restriction?
        """Specific server statistic.

        :param key: Statistic key
        """
        return self.stats()[key]

    def get_stat(self, key, default=None):
        """Specific server statistic or *None* if not found.

        :param key: Statistic key
        """
        return self.stats().get(key, default)

    @_timed_cache(minutes=60)
    def _resolve_email(self, entryid=None):
        try:
            mailuser = self.mapisession.OpenEntry(entryid, None, 0)
            # TODO PR_SMTP_ADDRESS_W from mailuser?
            return self.user(HrGetOneProp(mailuser, PR_ACCOUNT_W).Value).email
        except (Error, MAPIErrorNotFound): # TODO deleted user
            return '' # TODO groups

    def __unicode__(self):
        return 'Server(%s)' % self.server_socket

    def __repr__(self):
        return _repr(self)
