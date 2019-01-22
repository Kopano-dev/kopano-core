# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import codecs
import json
import os
import uuid
import sys

from MAPI import (
    MAPI_UNICODE, MAPI_MODIFY, PT_MV_BINARY, RELOP_EQ,
    TBL_BATCH, ECSTORE_TYPE_PUBLIC, FOLDER_SEARCH, MAPI_ASSOCIATED,
    MAPI_DEFERRED_ERRORS, ROW_REMOVE, MAPI_CREATE,
)
from MAPI.Defs import (
    HrGetOneProp, CHANGE_PROP_TYPE, PpropFindProp
)
from MAPI.Tags import (
    PR_ENTRYID, PR_MDB_PROVIDER, ZARAFA_STORE_PUBLIC_GUID,
    PR_STORE_RECORD_KEY, PR_EC_HIERARCHYID, PR_REM_ONLINE_ENTRYID,
    PR_IPM_PUBLIC_FOLDERS_ENTRYID, PR_IPM_SUBTREE_ENTRYID,
    PR_FINDER_ENTRYID, PR_ADDITIONAL_REN_ENTRYIDS, PR_ACL_TABLE,
    PR_IPM_APPOINTMENT_ENTRYID, PR_IPM_OUTBOX_ENTRYID,
    PR_IPM_CONTACT_ENTRYID, PR_COMMON_VIEWS_ENTRYID,
    PR_IPM_DRAFTS_ENTRYID, PR_IPM_WASTEBASKET_ENTRYID,
    PR_IPM_JOURNAL_ENTRYID, PR_IPM_NOTE_ENTRYID, PR_MEMBER_ID,
    PR_IPM_SENTMAIL_ENTRYID, PR_IPM_TASK_ENTRYID,
    PR_IPM_OL2007_ENTRYIDS, PR_MESSAGE_SIZE_EXTENDED,
    PR_LAST_LOGON_TIME, PR_LAST_LOGOFF_TIME, IID_IExchangeModifyTable,
    PR_MAILBOX_OWNER_ENTRYID, PR_EC_STOREGUID, PR_EC_STORETYPE,
    PR_EC_USERNAME_W, PR_EC_COMPANY_NAME_W, PR_MESSAGE_CLASS_W,
    PR_SUBJECT_W,
    PR_WLINK_STORE_ENTRYID, PR_WLINK_ENTRYID,
    PR_EXTENDED_FOLDER_FLAGS, PR_WB_SF_ID, PR_FREEBUSY_ENTRYIDS,
    PR_SCHDINFO_DELEGATE_ENTRYIDS, PR_SCHDINFO_DELEGATE_NAMES_W,
    PR_DELEGATE_FLAGS, PR_MAPPING_SIGNATURE, PR_EC_WEBACCESS_SETTINGS_JSON
)
from MAPI.Struct import (
    SPropertyRestriction, SPropValue, ROWENTRY, MAPINAMEID,
    MAPIErrorNotFound, MAPIErrorInvalidEntryid, MAPIErrorNoSupport
)

from .defs import (
    RSF_PID_SUGGESTED_CONTACTS, RSF_PID_TODO_SEARCH,
    RSF_PID_RSS_SUBSCRIPTION, NAMED_PROPS_ARCHIVER
)

from .errors import NotFoundError, ArgumentError, DuplicateError
from .properties import Properties
from .autoaccept import AutoAccept
from .autoprocess import AutoProcess
from .outofoffice import OutOfOffice
from .property_ import Property
from .delegation import Delegation
from .permission import Permission
from .freebusy import FreeBusy
from .table import Table
from .restriction import Restriction

from . import notification as _notification

from .compat import (
    encode as _encode, bdec as _bdec, benc as _benc, fake_unicode as _unicode,
    fake_ord as _ord,
)

if sys.hexversion >= 0x03000000:
    from . import server as _server
    try:
        from . import user as _user
    except ImportError: # pragma: no cover
        _user = sys.modules[__package__ + '.user']
    from . import folder as _folder
    from . import item as _item
    try:
        from . import utils as _utils
    except ImportError: # pragma: no cover
        _utils = sys.modules[__package__ + '.utils']
else: # pragma: no cover
    import server as _server
    import user as _user
    import folder as _folder
    import item as _item
    import utils as _utils


class Store(Properties):
    """Store class"""

    def __init__(self, guid=None, entryid=None, mapiobj=None, server=None):
        self.server = server or _server.Server()

        if guid:
            mapiobj = self.server._store(guid)
        elif entryid:
            try:
                mapiobj = self.server._store2(_bdec(entryid))
            except (MAPIErrorNotFound, MAPIErrorInvalidEntryid):
                raise NotFoundError("no store with entryid '%s'" % entryid)

        self.mapiobj = mapiobj
        # XXX: fails if store is orphaned and guid is given..
        self.__root = None

        self._name_id_cache = {}

    @property
    def _root(self):
        if self.__root is None:
            self.__root = self.mapiobj.OpenEntry(None, None, 0)
        return self.__root

    @property
    def entryid(self):
        """Store entryid"""
        return _benc(self.prop(PR_ENTRYID).value)

    @property
    def public(self):
        """This is a public store."""
        return self.prop(PR_MDB_PROVIDER).mapiobj.Value == ZARAFA_STORE_PUBLIC_GUID

    @property
    def guid(self):
        """Store GUID"""
        return _benc(self.prop(PR_STORE_RECORD_KEY).value)

    @property
    def name(self):
        """User name ('public' for public store), or GUID."""
        user = self.user
        if user:
            return user.name
        elif self.public:
            return 'public'
        else:
            return self.guid

    @property
    def hierarchyid(self):
        """Hierarchy (database) id."""
        return self.prop(PR_EC_HIERARCHYID).value

    @property
    def webapp_settings(self):
        """Webapp settings (JSON)."""
        try:
            return json.loads(self.user.store.prop(PR_EC_WEBACCESS_SETTINGS_JSON).value)
        except NotFoundError:
            pass

    @property
    def root(self):
        """:class:`Folder` designated as store root."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self._root, PR_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def subtree(self):
        """:class:`Folder` designated as IPM.Subtree."""
        try:
            if self.public:
                ipmsubtreeid = HrGetOneProp(self.mapiobj, PR_IPM_PUBLIC_FOLDERS_ENTRYID).Value
            else:
                ipmsubtreeid = HrGetOneProp(self.mapiobj, PR_IPM_SUBTREE_ENTRYID).Value

            return _folder.Folder(self, _benc(ipmsubtreeid))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def findroot(self):
        """:class:`Folder` designated as search-results root."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self.mapiobj, PR_FINDER_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def reminders(self):
        """:class:`Folder` designated as reminders."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self._root, PR_REM_ONLINE_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def inbox(self):
        """:class:`Folder` designated as inbox."""
        try:
            return _folder.Folder(self, _benc(self.mapiobj.GetReceiveFolder(u'IPM', MAPI_UNICODE)[0]))
        except (MAPIErrorNotFound, NotFoundError, MAPIErrorNoSupport):
            pass

    @inbox.setter
    def inbox(self, folder):
        # XXX can we get a list of current 'filters', or override all somehow?
        for messageclass in (u'', u'IPM', u'REPORT.IPM'):
            self.mapiobj.SetReceiveFolder(messageclass, MAPI_UNICODE, _bdec(folder.entryid))

    @property
    def junk(self):
        """:class:`Folder` designated as junk."""
        # PR_ADDITIONAL_REN_ENTRYIDS is a multi-value property, 4th entry is the junk folder
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self._root, PR_ADDITIONAL_REN_ENTRYIDS).Value[4]))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def calendar(self):
        """:class:`Folder` designated as calendar."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self._root, PR_IPM_APPOINTMENT_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def outbox(self):
        """:class:`Folder` designated as outbox."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self.mapiobj, PR_IPM_OUTBOX_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def contacts(self):
        """:class:`Folder` designated as contacts."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self._root, PR_IPM_CONTACT_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def common_views(self):
        """:class:`Folder` contains folders for managing views for the message store."""
        try:
            return _folder.Folder(self, _benc(self.prop(PR_COMMON_VIEWS_ENTRYID).value))
        except NotFoundError:
            pass

    @property
    def drafts(self):
        """:class:`Folder` designated as drafts."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self._root, PR_IPM_DRAFTS_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def wastebasket(self):
        """:class:`Folder` designated as wastebasket."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self.mapiobj, PR_IPM_WASTEBASKET_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def journal(self):
        """:class:`Folder` designated as journal."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self._root, PR_IPM_JOURNAL_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def notes(self):
        """:class:`Folder` designated as notes."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self._root, PR_IPM_NOTE_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def sentmail(self):
        """:class:`Folder` designated as sentmail."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self.mapiobj, PR_IPM_SENTMAIL_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def tasks(self):
        """:class:`Folder` designated as tasks."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(self._root, PR_IPM_TASK_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def suggested_contacts(self):
        """:class`Folder` designated as Suggested contacts."""
        try:
            return _folder.Folder(self, self._extract_ipm_ol2007_entryid(RSF_PID_SUGGESTED_CONTACTS))
        except NotFoundError:
            pass

    @property
    def todo_search(self):
        """:class`Folder` designated as To-Do Search folder."""
        try:
            return _folder.Folder(self, self._extract_ipm_ol2007_entryid(RSF_PID_TODO_SEARCH))
        except NotFoundError:
            pass

    @property
    def rss(self):
        """:class`Folder` designated as RSS items."""
        try:
            return _folder.Folder(self, self._extract_ipm_ol2007_entryid(RSF_PID_RSS_SUBSCRIPTION))
        except NotFoundError:
            pass

    def _extract_ipm_ol2007_entryid(self, offset):
        # Extracts entryids from PR_IPM_OL2007_ENTRYIDS blob using logic from common/Util.cpp Util::ExtractAdditionalRenEntryID
        if not self.inbox:
            raise NotFoundError('no inbox')
        blob = self.inbox.prop(PR_IPM_OL2007_ENTRYIDS).value
        pos = 0
        while True:
            blocktype = _utils.unpack_short(blob, pos)
            if blocktype == 0:
                break
            pos += 2

            totallen = _utils.unpack_short(blob, pos)
            pos += 2

            if blocktype == offset:
                pos += 2  # skip check
                sublen = _utils.unpack_short(blob, pos)
                pos += 2
                return _benc(blob[pos:pos + sublen])
            else:
                pos += totallen
        raise NotFoundError('entryid not found')

    def delete(self, objects, soft=False):
        """Delete properties, delegations, permissions, folders or items from store.

        :param props: The object(s) to delete
        """
        objects = _utils.arg_objects(objects, (_folder.Folder, _item.Item, Property, Delegation, Permission), 'Store.delete')
        # XXX directly delete inbox rule?

        props = [o for o in objects if isinstance(o, Property)]
        if props:
            self.mapiobj.DeleteProps([p.proptag for p in props])
            _utils._save(self.mapiobj)

        delgs = [o for o in objects if isinstance(o, Delegation)]
        for d in delgs:
            d._delete()

        perms = [o for o in objects if isinstance(o, Permission)]
        for perm in perms:
            acl_table = self.mapiobj.OpenProperty(PR_ACL_TABLE, IID_IExchangeModifyTable, 0, 0)
            acl_table.ModifyTable(0, [ROWENTRY(ROW_REMOVE, [SPropValue(PR_MEMBER_ID, perm.mapirow[PR_MEMBER_ID])])])

        others = [o for o in objects if isinstance(o, (_item.Item, _folder.Folder))]
        if others:
            self.root.delete(others, soft=soft)

    def folder(self, path=None, entryid=None, recurse=False, create=False, guid=None):
        """Return :class:`Folder` with given path/entryid.

        :param path: The path of the folder
        :param entryid: The entryid of the folder
        :param create: Create folder if it doesn't exist
        """

        if path is None and entryid is None and guid is None:
            raise ArgumentError('missing argument to identify folder')

        if guid is not None:
            # 01 -> entryid format version, 03 -> object type (folder)
            entryid = '00000000' + self.guid + '0100000003000000' + guid + '00000000'

        if entryid is not None:
            try:
                return _folder.Folder(self, entryid)
            except (MAPIErrorInvalidEntryid, MAPIErrorNotFound): # XXX move to Folder
                raise NotFoundError("no folder with entryid '%s'" % entryid)

        return self.subtree.folder(path, recurse=recurse, create=create)

    def get_folder(self, path=None, entryid=None):
        """Return :class:`folder <Folder>` with given path/entryid or *None* if not found.

        :param path: The path of the folder
        :param entryid: The entryid of the folder
        """
        try:
            return self.folder(path, entryid=entryid)
        except NotFoundError:
            pass

    def create_folder(self, path=None, **kwargs):
        """Create :class:`folder <Folder>` with given path.

        :param path: The path of the folder
        """
        return self.subtree.create_folder(path, **kwargs)

    def folders(self, recurse=True, parse=True, **kwargs):
        """Return all :class:`folders <Folder>` in store.

        :param recurse: include all sub-folders
        :param system: include system folders

        """
        # parse=True
        if parse and getattr(self.server.options, 'folders', None):
            for path in self.server.options.folders:
                yield self.folder(path)
            return

        if self.subtree:
            for folder in self.subtree.folders(recurse=recurse, **kwargs):
                yield folder

    def mail_folders(self, **kwargs): # TODO replace by store.folders(type='contacts') etc
        # TODO restriction
        for folder in self.folders():
            if folder.type_ == 'mail':
                yield folder

    def contact_folders(self, **kwargs):
        # TODO restriction
        for folder in self.folders():
            if folder.type_ == 'contacts':
                yield folder

    def calendars(self, **kwargs):
        # TODO restriction
        for folder in self.folders():
            if folder.type_ == 'calendar':
                yield folder

    def create_searchfolder(self, text=None): # XXX store.findroot.create_folder()?
        mapiobj = self.findroot.mapiobj.CreateFolder(FOLDER_SEARCH, _encode(str(uuid.uuid4())), _encode('comment'), None, 0)
        return _folder.Folder(self, mapiobj=mapiobj)

    def item(self, entryid=None, guid=None):
        """Return :class:`Item` with given entryid."""

        item = _item.Item() # XXX copy-pasting..
        item.store = self
        item.server = self.server

        if guid is not None:
            # 01 -> entryid format version, 05 -> object type (message)
            entryid = '00000000' + self.guid + '0100000005000000' + guid + '00000000'

        if entryid is not None:
            eid = _utils._bdec_eid(entryid)
        else:
            raise ArgumentError("no guid or entryid specified")

        try:
            item.mapiobj = _utils.openentry_raw(self.mapiobj, eid, 0) # XXX soft-deleted item?
        except MAPIErrorNotFound:
            raise NotFoundError("no item with entryid '%s'" % entryid)
        except MAPIErrorInvalidEntryid:
            raise ArgumentError("invalid entryid: %r" % entryid)

        return item

    @property
    def size(self):
        """Store size."""
        return self.prop(PR_MESSAGE_SIZE_EXTENDED).value

    def config_item(self, name):
        """Retrieve the config item for the given name.

        :param name: The config item name
        """
        name = _unicode(name)
        table = self.subtree.mapiobj.GetContentsTable(MAPI_DEFERRED_ERRORS | MAPI_ASSOCIATED)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_SUBJECT_W, SPropValue(PR_SUBJECT_W, name)), 0)
        rows = table.QueryRows(1, 0)
        # No config item found, create new message
        if len(rows) == 0:
            item = self.subtree.associated.create_item(message_class='IPM.Zarafa.Configuration', subject=name)
        else:
            mapiobj = self.subtree.mapiobj.OpenEntry(rows[0][0].Value, None, MAPI_MODIFY)
            item = _item.Item(mapiobj=mapiobj)
        return item

    @property
    def last_logon(self):
        """Return :datetime Last logon of a user on this store."""
        return self.get(PR_LAST_LOGON_TIME)

    @last_logon.setter
    def last_logon(self, value):
        self[PR_LAST_LOGON_TIME] = value

    @property
    def last_logoff(self):
        """Return :datetime of the last logoff of a user on this store."""
        return self.get(PR_LAST_LOGOFF_TIME)

    @last_logoff.setter
    def last_logoff(self, value):
        self[PR_LAST_LOGOFF_TIME] = value

    @property
    def outofoffice(self):
        """Return :class:`OutOfOffice` settings."""
        # FIXME: If store is public store, return None?
        return OutOfOffice(self)

    @property
    def autoprocess(self):
        return AutoProcess(self)

    @property
    def autoaccept(self):
        """Return :class:`AutoAccept` settings."""
        return AutoAccept(self)

    @property
    def user(self):
        """Store :class:`owner <User>`."""
        try:
            userid = HrGetOneProp(self.mapiobj, PR_MAILBOX_OWNER_ENTRYID).Value # XXX
            return _user.User(self.server.sa.GetUser(userid, MAPI_UNICODE).Username, self.server)
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def archive_store(self):
        """Archive :class:`Store`."""
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, MAPI_CREATE) # XXX merge namedprops stuff
        PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)

        try:
            # support for multiple archives was a mistake, and is not and _should not_ be used. so we just pick nr 0.
            entryid = HrGetOneProp(self.mapiobj, PROP_STORE_ENTRYIDS).Value[0]
        except MAPIErrorNotFound:
            return

        return Store(entryid=_benc(entryid), server=self.server) # XXX server?

    @archive_store.setter
    def archive_store(self, store):
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, MAPI_CREATE) # XXX merge namedprops stuff
        PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)
        PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

        # XXX only for detaching atm
        if store is None:
            self.mapiobj.DeleteProps([PROP_STORE_ENTRYIDS, PROP_ITEM_ENTRYIDS])
            _utils._save(self.mapiobj)

    @property
    def archive_folder(self):
        """Archive :class:`Folder`."""
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, MAPI_CREATE) # XXX merge namedprops stuff
        PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

        try:
            # support for multiple archives was a mistake, and is not and _should not_ be used. so we just pick nr 0.
            arch_folderid = HrGetOneProp(self.mapiobj, PROP_ITEM_ENTRYIDS).Value[0]
        except MAPIErrorNotFound:
            return

        return self.archive_store.folder(entryid=_benc(arch_folderid))

    @property
    def company(self):
        """Store :class:`company <Company>`."""
        if self.server.multitenant:
            table = self.server.sa.OpenUserStoresTable(MAPI_UNICODE)
            table.Restrict(SPropertyRestriction(RELOP_EQ, PR_EC_STOREGUID, SPropValue(PR_EC_STOREGUID, _bdec(self.guid))), TBL_BATCH)
            for row in table.QueryRows(1, 0):
                storetype = PpropFindProp(row, PR_EC_STORETYPE)
                if storetype.Value == ECSTORE_TYPE_PUBLIC:
                    companyname = PpropFindProp(row, PR_EC_USERNAME_W) # XXX bug in ECUserStoreTable.cpp?
                    if not companyname:
                        companyname = PpropFindProp(row, PR_EC_COMPANY_NAME_W) # XXX
                else:
                    companyname = PpropFindProp(row, PR_EC_COMPANY_NAME_W)
                return self.server.company(companyname.Value)
        else:
            return next(self.server.companies())

    @property
    def orphan(self):
        """The store is orphaned."""
        if self.public:
            pubstore = self.company.public_store
            return (pubstore is None or pubstore.guid != self.guid)
        else:
            return (self.user is None or self.user.store is None or self.user.store.guid != self.guid)

    def permissions(self):
        """Return all :class:`permissions <Permission>` set for this store."""
        return _utils.permissions(self)

    def permission(self, member, create=False):
        """Return :class:`permission <Permission>` for user or group set for this store.

        :param member: user or group
        :param create: create new permission for this user or group
        """
        return _utils.permission(self, member, create)

    def _fbmsg_delgs(self):
        fbeid = self.root.prop(PR_FREEBUSY_ENTRYIDS).value[1]
        fbmsg = self.mapiobj.OpenEntry(fbeid, None, MAPI_MODIFY)

        try:
            entryids = HrGetOneProp(fbmsg, PR_SCHDINFO_DELEGATE_ENTRYIDS)
            names = HrGetOneProp(fbmsg, PR_SCHDINFO_DELEGATE_NAMES_W)
            flags = HrGetOneProp(fbmsg, PR_DELEGATE_FLAGS)
        except MAPIErrorNotFound:
            entryids = SPropValue(PR_SCHDINFO_DELEGATE_ENTRYIDS, [])
            names = SPropValue(PR_SCHDINFO_DELEGATE_NAMES_W, [])
            flags = SPropValue(PR_DELEGATE_FLAGS, [])

        return fbmsg, (entryids, names, flags)

    def delegations(self):
        """Return all :class:`delegations <Delegation>`."""
        _, (entryids, _, _) = self._fbmsg_delgs()

        for entryid in entryids.Value:
            try:
                username = self.server.sa.GetUser(entryid, MAPI_UNICODE).Username
            except MAPIErrorNotFound:
                raise NotFoundError("no user found with userid '%s'" % _benc(entryid))
            yield Delegation(self, self.server.user(username))

    def delegation(self, user, create=False, see_private=False):
        """Return :class:`delegation <Delegation>` for user.

        :param user: user
        :param create: create new delegation for this user
        :param see_private: user can see private items
        """
        for delegation in self.delegations():
            if delegation.user == user:
                return delegation
        if create:
            fbmsg, (entryids, names, flags) = self._fbmsg_delgs()

            entryids.Value.append(_bdec(user.userid))
            names.Value.append(user.fullname)
            flags.Value.append(1 if see_private else 0)

            fbmsg.SetProps([entryids, names, flags])
            _utils._save(fbmsg)

            return Delegation(self, user)
        else:
            raise NotFoundError("no delegation for user '%s'" % user.name)

    @property
    def send_only_to_delegates(self):
        """When sending meetingrequests to delegates, do not send them to the owner."""
        return Delegation._send_only_to_delegates(self)

    @send_only_to_delegates.setter
    def send_only_to_delegates(self, value):
        Delegation._set_send_only_to_delegates(self, value)

    def favorites(self):
        """Returns all favorite folders"""

        restriction = Restriction(SPropertyRestriction(
            RELOP_EQ, PR_MESSAGE_CLASS_W,
            SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Microsoft.WunderBar.Link')
        ))

        table = Table(
            self.server,
            self.mapiobj,
            self.common_views.mapiobj.GetContentsTable(MAPI_ASSOCIATED),
            restriction=restriction,
            columns=[PR_WLINK_ENTRYID, PR_WLINK_STORE_ENTRYID],
        )
        for entryid, store_entryid in table:
            try:
                if store_entryid == self.entryid: # XXX: Handle favorites from public stores
                    yield self.folder(entryid=_benc(entryid.value))
                else:
                    store = Store(entryid=_benc(store_entryid.value), server=self.server)
                    yield store.folder(entryid=_benc(entryid.value))
            except NotFoundError:
                pass

    def add_favorite(self, folder): # TODO remove_favorite, folder.favorite
        if folder in self.favorites():
            raise DuplicateError("folder '%s' already in favorites" % folder.name)

        item = self.common_views.associated.create_item()
        item[PR_MESSAGE_CLASS_W] = u'IPM.Microsoft.WunderBar.Link'
        item[PR_WLINK_ENTRYID] = _bdec(folder.entryid)
        item[PR_WLINK_STORE_ENTRYID] = _bdec(folder.store.entryid)

    def _subprops(self, value):
        result = {}
        pos = 0
        while pos < len(value):
            id_ = _ord(value[pos])
            cb = _ord(value[pos + 1])
            result[id_] = value[pos + 2:pos + 2 + cb]
            pos += 2 + cb
        return result

    def searches(self):
        """Return all permanent search folders."""
        findroot = self.root.folder('FINDER_ROOT') # XXX

        # extract special type of guid from search folder PR_EXTENDED_FOLDER_FLAGS to match against
        guid_folder = {}
        for folder in findroot.folders():
            try:
                prop = folder.prop(PR_EXTENDED_FOLDER_FLAGS)
                subprops = self._subprops(prop.value)
                guid_folder[subprops[2]] = folder
            except NotFoundError:
                pass

        # match common_views SFInfo records against these guids
        table = self.common_views.mapiobj.GetContentsTable(MAPI_ASSOCIATED)

        table.SetColumns([PR_MESSAGE_CLASS_W, PR_WB_SF_ID], MAPI_UNICODE)
        table.Restrict(SPropertyRestriction(
            RELOP_EQ, PR_MESSAGE_CLASS_W,
            SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Microsoft.WunderBar.SFInfo')),
            TBL_BATCH)

        for row in table.QueryRows(-1, 0):
            try:
                yield guid_folder[row[1].Value]
            except KeyError:
                pass

    def add_search(self, searchfolder):
        # add random tag, id to searchfolder
        tag = os.urandom(4)
        id_ = os.urandom(16)
        flags = b"0104000000010304" + \
                codecs.encode(tag, 'hex') + \
                b"0210" + \
                codecs.encode(id_, 'hex')
        searchfolder[PR_EXTENDED_FOLDER_FLAGS] = codecs.decode(flags, 'hex')

        # add matching entry to common views
        item = self.common_views.associated.create_item()
        item[PR_MESSAGE_CLASS_W] = u'IPM.Microsoft.WunderBar.SFInfo'
        item[PR_WB_SF_ID] = id_

    @property
    def home_server(self):
        if self.user:
            return self.user.home_server
        else:
            try:
                for node in self.server.nodes(): # XXX faster?
                    if node.guid == _benc(self.prop(PR_MAPPING_SIGNATURE).value):
                        return node.name
            except MAPIErrorNotFound:
                return self.server.name

    @property
    def freebusy(self):
        """Return :class:`freebusy <Freebusy>` information."""
        return FreeBusy(self)

    def _name_id(self, name_tuple):
        id_ = self._name_id_cache.get(name_tuple)
        if id_ is None:
            named_props = [MAPINAMEID(*name_tuple)]
            id_ = self.mapiobj.GetIDsFromNames(named_props, 0)[0] # TODO use MAPI_CREATE, or too dangerous because of potential db overflow?
            self._name_id_cache[name_tuple] = id_
        return id_

    def __eq__(self, s): # XXX check same server?
        if isinstance(s, Store):
            return self.guid == s.guid
        return False

    def subscribe(self, sink, **kwargs):
        _notification.subscribe(self, None, sink, **kwargs)

    def unsubscribe(self, sink):
        _notification.unsubscribe(self, sink)

    def __ne__(self, s):
        return not self == s

    def __iter__(self):
        return self.folders()

    def __unicode__(self):
        return u"Store('%s')" % self.guid
