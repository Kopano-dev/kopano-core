# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import binascii

from MAPI import (
    MAPI_UNICODE, MAPI_UNRESOLVED, ECSTORE_TYPE_PRIVATE, ECSTORE_TYPE_ARCHIVE,
    WrapStoreEntryID
)
from MAPI.Defs import HrGetOneProp
from MAPI.Struct import (
    SPropValue, MAPIErrorNotFound, MAPIErrorInvalidParameter,
    MAPIErrorCollision, MAPIErrorNoSupport, ECUSER
)
from MAPI.Tags import (
    PR_EMAIL_ADDRESS_W, PR_DISPLAY_NAME_W, PR_EC_ENABLED_FEATURES_W,
    PR_EC_DISABLED_FEATURES_W, PR_EC_COMPANY_NAME_W,
    PR_MAPPING_SIGNATURE, PR_EC_ARCHIVE_SERVERS,
    EMS_AB_ADDRESS_LOOKUP, PR_GIVEN_NAME_W, PR_SURNAME_W,
    PR_MOBILE_TELEPHONE_NUMBER_W, PR_OFFICE_LOCATION_W,
    PR_TITLE_W, PR_EMS_AB_THUMBNAIL_PHOTO,
)

from .store import Store
from .properties import Properties
from .group import Group
from .quota import Quota
from .defs import (
    ACTIVE_USER, NONACTIVE_USER,
)
from .errors import (
    Error, NotFoundError, NotSupportedError, DuplicateError, ArgumentError
)
from .compat import (
    fake_unicode as _unicode, benc as _benc, bdec as _bdec,
)
from .picture import Picture

from . import server as _server
from . import company as _company

class User(Properties):
    """User class.

    Includes all functionality from :class:`Properties`.

    :class:`Store` attributes can be accessed directly from a :class:`User` instance.
    """

    def __init__(self, name=None, server=None, email=None, ecuser=None,
                 userid=None):
        self.server = server or _server.Server(
            _skip_check=True, parse_args=False)
        self._ecuser = None
        self._name = None

        if ecuser:
            self._ecuser = ecuser

        elif userid:
            try:
                self._ecuser = \
                    self.server.sa.GetUser(_bdec(userid), MAPI_UNICODE)
            except (binascii.Error, TypeError): # TODO generalize?
                raise ArgumentError("invalid entryid: '%s'" % userid)
            except MAPIErrorNotFound:
                raise NotFoundError("no user found with userid '%s'" % userid)

        elif email or name:
            if email:
                try:
                    self._name = _unicode(self.server.gab.ResolveNames(
                        [PR_EMAIL_ADDRESS_W],
                        MAPI_UNICODE | EMS_AB_ADDRESS_LOOKUP,
                        [[SPropValue(PR_DISPLAY_NAME_W, _unicode(email))]],
                        [MAPI_UNRESOLVED]
                    )[0][0][1].Value)
                except (MAPIErrorNotFound, MAPIErrorInvalidParameter, \
                    IndexError):
                    raise NotFoundError("no such user '%s'" % email)
            else:
                self._name = _unicode(name)

            try:
                self._ecuser = self.server.sa.GetUser(
                    self.server.sa.ResolveUserName(
                        self._name, MAPI_UNICODE), MAPI_UNICODE)
            # multi-tenant, but no '@' in username..
            except (MAPIErrorNotFound, MAPIErrorInvalidParameter):
                raise NotFoundError("no such user: '%s'" % self.name)

        if self._ecuser:
            self._name = self._ecuser.Username
            self._userid = _benc(self._ecuser.UserID)

        self._mapiobj = None

    @property
    def mapiobj(self):
        """Underlying MAPI object."""
        if not self._mapiobj:
            self._mapiobj = \
                self.server.mapisession.OpenEntry(self._ecuser.UserID, None, 0)
        return self._mapiobj

    @property
    def admin(self):
        """Is the user (regular or system) administrator."""
        return self._ecuser.IsAdmin >= 1

    @admin.setter
    def admin(self, value):
        self._update(admin=value)

    @property
    def admin_level(self):
        """User administration level (1 means regular administrator,
        2 means system administrator)."""
        return self._ecuser.IsAdmin

    @admin_level.setter
    def admin_level(self, value):
        self._update(admin=value)

    @property
    def hidden(self):
        """The user is hidden from the addressbook."""
        return bool(self._ecuser.IsHidden)

    @property
    def name(self):
        """User account name."""
        return self._name

    @name.setter
    def name(self, value):
        self._update(username=_unicode(value))

    @property
    def fullname(self):
        """User full name."""
        return self._ecuser.FullName

    @fullname.setter
    def fullname(self, value):
        self._update(fullname=_unicode(value))

    @property
    def email(self):
        """User email address."""
        return self._ecuser.Email

    @email.setter
    def email(self, value):
        self._update(email=_unicode(value))

    @property
    def password(self):
        """User password (set-only)."""
        raise Error('passwords are write-only')

    @password.setter
    def password(self, value):
        self._update(password=_unicode(value))

    # TODO uniform with contact.photo.. class Picture (filename, dimensions..)?
    @property
    def photo(self):
        """User :class:`photo <Picture>`."""
        data = self.get(PR_EMS_AB_THUMBNAIL_PHOTO)
        if data is not None:
            return Picture(data=data)

    @property
    def features(self):
        """Enabled features (*pop3*, *imap*, *mobile*, ..)."""
        if not hasattr(self._ecuser, 'MVPropMap'):
            raise NotSupportedError('Python-Mapi does not support MVPropMap')

        for entry in self._ecuser.MVPropMap:
            if entry.ulPropId == PR_EC_ENABLED_FEATURES_W:
                return entry.Values

    @features.setter
    def features(self, value):
        if not hasattr(self._ecuser, 'MVPropMap'):
            raise NotSupportedError('Python-Mapi does not support MVPropMap')

        # Enabled + Disabled defines all features.
        features = \
            set([e for entry in self._ecuser.MVPropMap for e in entry.Values])
        disabled = list(features - set(value))

        # TODO: performance
        for entry in self._ecuser.MVPropMap:
            if entry.ulPropId == PR_EC_ENABLED_FEATURES_W:
                entry.Values = value
            if entry.ulPropId == PR_EC_DISABLED_FEATURES_W:
                entry.Values = disabled

        self._update()

    def add_feature(self, feature):
        """Enable a feature for a user.

        :param feature: The feature
        """
        feature = _unicode(feature)
        if feature in self.features:
            raise DuplicateError("feature '%s' already enabled for user \
'%s'" % (feature, self.name))
        self.features = self.features + [feature]

    def remove_feature(self, feature):
        """Disable a feature for a user

        :param feature: The feature
        """
        # Copy features otherwise we will miss a disabled feature.
        # TODO: improvement?
        features = self.features[:]
        try:
            features.remove(_unicode(feature))
        except ValueError:
            raise NotFoundError("no feature '%s' enabled for user '%s'" % \
                (feature, self.name))
        self.features = features
        self._update()

    @property
    def userid(self):
        """User id."""
        return self._userid

    @property
    def company(self):
        """:class:`Company` the user belongs to."""
        try:
            return _company.Company(HrGetOneProp(self.mapiobj,
                PR_EC_COMPANY_NAME_W).Value, self.server)
        except MAPIErrorNoSupport:
            return _company.Company('Default', self.server)

    @property
    def first_name(self):
        """User first name."""
        return self.get(PR_GIVEN_NAME_W, '')

    @property
    def job_title(self):
        """User job title."""
        return self.get(PR_TITLE_W, '')

    @property
    def mobile_phone(self):
        """User mobile phone number."""
        return self.get(PR_MOBILE_TELEPHONE_NUMBER_W, '')

    @property
    def office_location(self):
        """User office Location."""
        return self.get(PR_OFFICE_LOCATION_W, '')

    @property
    def last_name(self):
        """User last name."""
        return self.get(PR_SURNAME_W, '')

    @property # TODO
    def local(self):
        store = self.store
        return bool(store and (self.server.guid == \
            _benc(HrGetOneProp(store.mapiobj, PR_MAPPING_SIGNATURE).Value)))

    def create_store(self):
        """Create and attach store for user."""
        try:
            storeid_rootid = self.server.sa.CreateStore(
                ECSTORE_TYPE_PRIVATE, self._ecuser.UserID)
        except MAPIErrorCollision:
            raise DuplicateError("user '%s' already has store" % self.name)
        store_entryid = WrapStoreEntryID(0, b'zarafa6client.dll',
            storeid_rootid[0][:-4]) + self.server.pseudo_url + b'\x00'
        return Store(entryid=_benc(store_entryid), server=self.server)

    @property
    def store(self):
        """Return user :class:`Store` or *None* if no store is attached."""
        try:
            entryid = self.server.ems.CreateStoreEntryID(
                None, self._name, MAPI_UNICODE)
            return Store(entryid=_benc(entryid), server=self.server)
        except (MAPIErrorNotFound, NotFoundError):
            pass

    # TODO deprecated? user.store = .., user.archive_store = ..
    def hook(self, store):
        """Hook (attach) store.

        :param store: :class:`Store <Store>` to hook.
        """
        try:
            self.server.sa.HookStore(
                ECSTORE_TYPE_PRIVATE, _bdec(self.userid), _bdec(store.guid))
        except MAPIErrorCollision:
            raise DuplicateError("user '%s' already has hooked store" % \
                self.name)

    # TODO deprecated? user.store = None
    def unhook(self):
        """Unhook attached store."""
        try:
            self.server.sa.UnhookStore(
                ECSTORE_TYPE_PRIVATE, _bdec(self.userid))
        except MAPIErrorNotFound:
            raise NotFoundError("user '%s' has no hooked store" % self.name)

    def hook_archive(self, store):
        """Hook archive store.

        :param store: Archive :class:`store <Store>` to hook.
        """
        try:
            self.server.sa.HookStore(
                ECSTORE_TYPE_ARCHIVE, _bdec(self.userid), _bdec(store.guid))
        except MAPIErrorCollision:
            raise DuplicateError("user '%s' already has hooked archive \
store" % self.name)

    def unhook_archive(self):
        """Unhook archive store."""
        try:
            self.server.sa.UnhookStore(
                ECSTORE_TYPE_ARCHIVE, _bdec(self.userid))
        except MAPIErrorNotFound:
            raise NotFoundError("user '%s' has no hooked archive store" % \
                self.name)

    @property
    def active(self):
        """Is the user active."""
        return self._ecuser.Class == ACTIVE_USER

    @active.setter
    def active(self, value):
        if value:
            self._update(user_class=ACTIVE_USER)
        else:
            self._update(user_class=NONACTIVE_USER)

    @property
    def home_server(self):
        """Name of user home server."""
        return self._ecuser.Servername or self.server.name

    @property
    def archive_server(self):
        """Name of archive server."""
        try:
            return HrGetOneProp(self.mapiobj, PR_EC_ARCHIVE_SERVERS).Value[0]
        except MAPIErrorNotFound:
            return

    @property
    def quota(self):
        """User :class:`Quota`"""
        return Quota(self.server, self._ecuser.UserID)

    def groups(self):
        """Return all :class:`groups <Group>` to which user belongs."""
        for g in self.server.sa.GetGroupListOfUser(
                self._ecuser.UserID, MAPI_UNICODE):
            yield Group(g.Groupname, self.server)

    def send_as(self):
        """Return all :class:`users <User>` who can send-as this user."""
        for u in self.server.sa.GetSendAsList(
                self._ecuser.UserID, MAPI_UNICODE):
            yield self.server.user(u.Username)

    def add_send_as(self, user):
        """Add :class:`user <User>` as send-as.

        :param user: User to add.
        """
        try:
            self.server.sa.AddSendAsUser(
                self._ecuser.UserID, user._ecuser.UserID)
        except MAPIErrorCollision:
            raise DuplicateError("user '%s' already in send-as for user \
'%s'" % (user.name, self.name))

    def remove_send_as(self, user):
        """Remove :class:`user <User>` from send-as.

        :param user: User to remove.
        """
        try:
            self.server.sa.DelSendAsUser(
                self._ecuser.UserID, user._ecuser.UserID)
        except MAPIErrorNotFound:
            raise NotFoundError("no user '%s' in send-as for user '%s'" % \
                (user.name, self.name))

    def rules(self):
        """Return all :class:`rules <Rule>` on user inbox."""
        return self.inbox.rules()

    def __eq__(self, u): # TODO check same server?
        if isinstance(u, User):
            return self.userid == u.userid
        return False

    def __ne__(self, u):
        return not self == u

    def __unicode__(self):
        return "User(%s)" % (self._name or '')

    def _update(self, **kwargs):
        username = kwargs.get('username', self.name)
        password = kwargs.get('password', self._ecuser.Password)
        email = kwargs.get('email', _unicode(self._ecuser.Email))
        fullname = kwargs.get('fullname', _unicode(self._ecuser.FullName))
        user_class = kwargs.get('user_class', self._ecuser.Class)
        admin = kwargs.get('admin', self._ecuser.IsAdmin)

        try:
            # Pass the MVPropMAP otherwise the set values are reset
            if hasattr(self._ecuser, 'MVPropMap'):
                self.server.sa.SetUser(ECUSER(
                    Username=username, Password=password,
                    Email=email, FullName=fullname,
                    Class=user_class, UserID=self._ecuser.UserID,
                    IsAdmin=admin, MVPropMap=self._ecuser.MVPropMap
                ), MAPI_UNICODE)

            else:
                self.server.sa.SetUser(ECUSER(
                    Username=username, Password=password, Email=email,
                    FullName=fullname, Class=user_class,
                    UserID=self._ecuser.UserID, IsAdmin=admin
                ), MAPI_UNICODE)
        except MAPIErrorNoSupport:
            pass

        self._ecuser = self.server.sa.GetUser(
            self.server.sa.ResolveUserName(username, MAPI_UNICODE),
            MAPI_UNICODE
        )
        self._name = username

    def __getattr__(self, x):
        store = self.store
        if store:
            try:
                return getattr(store, x)
            except AttributeError:
                raise AttributeError("'User' object has no attribute '%s'" % x)

    def __setattr__(self, x, val):
        if x not in User.__dict__ and x in Store.__dict__:
            setattr(self.store, x, val)
        else:
            super(User, self).__setattr__(x, val)

    def __dir__(self):
        return list(User.__dict__) + list(Store.__dict__)
