# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import binascii
import codecs
import json
import os
import uuid
import sys

from MAPI import (
    MAPI_UNICODE, MAPI_MODIFY, PT_MV_BINARY, RELOP_EQ,
    TBL_BATCH, ECSTORE_TYPE_PUBLIC, FOLDER_SEARCH, MAPI_ASSOCIATED,
    MAPI_DEFERRED_ERRORS, ROW_REMOVE, MAPI_CREATE,
    ECSTORE_TYPE_PRIVATE, ECSTORE_TYPE_ARCHIVE, KEEP_OPEN_READWRITE,
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
    PR_IPM_CONTACT_ENTRYID, PR_COMMON_VIEWS_ENTRYID, PR_VIEWS_ENTRYID,
    PR_IPM_DRAFTS_ENTRYID, PR_IPM_WASTEBASKET_ENTRYID,
    PR_IPM_JOURNAL_ENTRYID, PR_IPM_NOTE_ENTRYID, PR_MEMBER_ID,
    PR_IPM_SENTMAIL_ENTRYID, PR_IPM_TASK_ENTRYID,
    PR_IPM_OL2007_ENTRYIDS, PR_MESSAGE_SIZE_EXTENDED,
    PR_LAST_LOGON_TIME, PR_LAST_LOGOFF_TIME, IID_IExchangeModifyTable,
    PR_MAILBOX_OWNER_ENTRYID, PR_EC_STOREGUID, PR_EC_STORETYPE,
    PR_EC_USERNAME_W, PR_EC_COMPANY_NAME_W, PR_MESSAGE_CLASS_W,
    PR_SUBJECT_W, PR_CONTAINER_CLASS_W,
    PR_WLINK_STORE_ENTRYID, PR_WLINK_ENTRYID,
    PR_EXTENDED_FOLDER_FLAGS, PR_WB_SF_ID, PR_FREEBUSY_ENTRYIDS,
    PR_SCHDINFO_DELEGATE_ENTRYIDS, PR_SCHDINFO_DELEGATE_NAMES_W,
    PR_DELEGATE_FLAGS, PR_MAPPING_SIGNATURE, PR_EC_WEBACCESS_SETTINGS_JSON_W,
    PR_EC_WEBACCESS_SETTINGS_W, PR_EC_RECIPIENT_HISTORY_W,
    PR_EC_RECIPIENT_HISTORY_JSON_W, PR_EC_WEBAPP_PERSISTENT_SETTINGS_JSON_W,
    PR_EC_OUTOFOFFICE_SUBJECT_W, PR_EC_OUTOFOFFICE_MSG_W, PR_EC_OUTOFOFFICE,
    PR_EC_OUTOFOFFICE_FROM, PR_EC_OUTOFOFFICE_UNTIL,
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
from .log import log_exc
from .properties import Properties
from .autoaccept import AutoAccept
from .autoprocess import AutoProcess
from .outofoffice import OutOfOffice
from .property_ import Property, _name_to_proptag
from .delegation import Delegation
from .permission import Permission, _permissions_dumps, _permissions_loads
from .freebusy import FreeBusy
from .table import Table
from .restriction import Restriction

from . import notification as _notification

from .compat import (
    bdec as _bdec, benc as _benc
)

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

SETTINGS_PROPTAGS = (
    PR_EC_WEBACCESS_SETTINGS_W, PR_EC_RECIPIENT_HISTORY_W,
    PR_EC_WEBACCESS_SETTINGS_JSON_W, PR_EC_RECIPIENT_HISTORY_JSON_W,
    PR_EC_WEBAPP_PERSISTENT_SETTINGS_JSON_W,
    PR_EC_OUTOFOFFICE_SUBJECT_W, PR_EC_OUTOFOFFICE_MSG_W,
    PR_EC_OUTOFOFFICE, PR_EC_OUTOFOFFICE_FROM, PR_EC_OUTOFOFFICE_UNTIL,
)


class Store(Properties):
    """Store class.

    There are three types of stores: *public*, *private* and *archive*.

    A *private* store contains a hierarchical collection of
    :class:`folders <Folder>`, as well as various user settings, stored in
    :class:`properties <Property>` of the store. For example, out-of-office
    and webapp settings.

    The *root* folder is the user-invisible root folder of the hierarchy,
    whereas *subtree* folder is the user-visible root folder. Most special
    folders such as the *inbox* are found directly under the *subtree* folder.

    In the *root* folder there are can be certain invisible special folders,
    such as a folder containing searches, a folder containing archive state and
    a folder used for notifications.

    A *public* store also contains a hierarchical collection of folders, which
    are accessible to all :class:`users <User>`.

    An *archive* store is used in combination with the archiver, and contains
    'older' data, which is migrated there according to configurable criteria.

    Includes all functionality from :class:`Properties`.
    """

    def __init__(self, guid=None, entryid=None, mapiobj=None, server=None):
        self.server = server or _server.Server(
            _skip_check=True, parse_args=False)

        if guid:
            mapiobj = self.server._store(guid)
        elif entryid:
            try:
                mapiobj = self.server._store2(_bdec(entryid))
            except (binascii.Error, MAPIErrorNotFound,
                    MAPIErrorInvalidEntryid):
                raise NotFoundError("no store with entryid '%s'" % entryid)

        #: Underlying MAPI object.
        self.mapiobj = mapiobj
        # TODO: fails if store is orphaned and guid is given..
        self.__root = None

        self._name_id_cache = {}
        self._pidlid_cache = {}

    @property
    def _root(self):
        if self.__root is None:
            self.__root = self.mapiobj.OpenEntry(None, None, 0)
        return self.__root

    @property
    def entryid(self):
        """Store entryid."""
        return _benc(self.prop(PR_ENTRYID).value)

    @property
    def public(self):
        """The store is a public store."""
        return self.prop(PR_MDB_PROVIDER).mapiobj.Value == \
            ZARAFA_STORE_PUBLIC_GUID

    @property
    def guid(self):
        """Store GUID."""
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
        """Hierarchy (SQL) id."""
        return self.prop(PR_EC_HIERARCHYID).value

    @property
    def webapp_settings(self):
        """Webapp settings (JSON)."""
        try:
            return json.loads(self.user.store.prop(
                PR_EC_WEBACCESS_SETTINGS_JSON_W).value)
        except NotFoundError:
            pass

    def _set_special_folder(self, folder, proptag, container_class=None):
        _root = self.mapiobj.OpenEntry(None, None, MAPI_MODIFY)

        if isinstance(proptag, tuple):
            proptag, idx = proptag
            try:
                value = HrGetOneProp(self._root, proptag).Value
            except MAPIErrorNotFound:
                value = 5 * [b'']
            value[idx] = _bdec(folder.entryid)
            _root.SetProps([SPropValue(proptag, value)])
            if self.inbox:
                self.inbox.mapiobj.SetProps([SPropValue(proptag, value)])
        else:
            _root.SetProps([SPropValue(proptag, _bdec(folder.entryid))])
            if self.inbox:
                self.inbox.mapiobj.SetProps([SPropValue(proptag, _bdec(folder.entryid))])
        _utils._save(self._root)

        if container_class:
            folder.container_class = container_class
        else:
            prop = folder.get_prop(PR_CONTAINER_CLASS_W)
            if prop:
                folder.delete(prop)

    @property
    def root(self):
        """The user-invisible store root :class:`Folder`."""
        try:
            return _folder.Folder(self,
                _benc(HrGetOneProp(self._root, PR_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def subtree(self):
        """The user-visible store root :class:`Folder`."""
        try:
            if self.public:
                ipmsubtreeid = HrGetOneProp(
                    self.mapiobj, PR_IPM_PUBLIC_FOLDERS_ENTRYID).Value
            else:
                ipmsubtreeid = HrGetOneProp(
                    self.mapiobj, PR_IPM_SUBTREE_ENTRYID).Value

            return _folder.Folder(self, _benc(ipmsubtreeid))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @subtree.setter
    def subtree(self, folder):
        self[PR_IPM_SUBTREE_ENTRYID] = _bdec(folder.entryid)

    @property
    def findroot(self):
        """The user-invisible store search folder :class:`Folder`."""
        try:
            return _folder.Folder(self,
                _benc(HrGetOneProp(self.mapiobj, PR_FINDER_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @findroot.setter
    def findroot(self, folder):
        self[PR_FINDER_ENTRYID] = _bdec(folder.entryid)

    @property
    def reminders(self):
        """The user-invisible store reminder :class:`Folder`."""
        try:
            return _folder.Folder(self,
                _benc(HrGetOneProp(self._root, PR_REM_ONLINE_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass
    # TODO setter

    @property
    def inbox(self):
        """The store :class:`inbox <Folder>`."""
        try:
            return _folder.Folder(self,
                _benc(self.mapiobj.GetReceiveFolder('IPM', MAPI_UNICODE)[0]))
        except (MAPIErrorNotFound, NotFoundError, MAPIErrorNoSupport):
            pass

    @inbox.setter
    def inbox(self, folder):
        # TODO can we get a list of current 'filters', or override all somehow?
        for messageclass in ('', 'IPM', 'REPORT.IPM'):
            self.mapiobj.SetReceiveFolder(
                messageclass, MAPI_UNICODE, _bdec(folder.entryid))

    @property
    def junk(self):
        """The store :class:`junk folder <Folder>`."""
        # PR_ADDITIONAL_REN_ENTRYIDS is a multi-value property.
        # The 4th entry points to the junk folder
        try:
            return _folder.Folder(self,_benc(HrGetOneProp(
                self._root, PR_ADDITIONAL_REN_ENTRYIDS).Value[4]))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @junk.setter
    def junk(self, folder):
        self._set_special_folder(
            folder, (PR_ADDITIONAL_REN_ENTRYIDS, 4), 'IPF.Note')

    @property
    def calendar(self):
        """The store (default) :class:`calendar <Folder>`."""
        try:
            return _folder.Folder(self,_benc(HrGetOneProp(
                self._root, PR_IPM_APPOINTMENT_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @calendar.setter
    def calendar(self, folder):
        self._set_special_folder(folder, PR_IPM_APPOINTMENT_ENTRYID,
            'IPF.Appointment')

    @property
    def outbox(self):
        """The store :class:`outbox <Folder>`."""
        try:
            return _folder.Folder(self,
                _benc(HrGetOneProp(self.mapiobj, PR_IPM_OUTBOX_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @outbox.setter
    def outbox(self, folder):
        self[PR_IPM_OUTBOX_ENTRYID] = _bdec(folder.entryid)
        # TODO clear container class

    @property
    def contacts(self):
        """The store (default) :class:`contacts folder <Folder>`."""
        try:
            return _folder.Folder(self,
                _benc(HrGetOneProp(self._root, PR_IPM_CONTACT_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @contacts.setter
    def contacts(self, folder):
        self._set_special_folder(folder, PR_IPM_CONTACT_ENTRYID, 'IPF.Contact')

    # TODO describe difference between views and common-views
    @property
    def views(self):
        """:class:`Folder` containing sub-folders acting as special views
        on the message store."""
        try:
            return _folder.Folder(
                self, _benc(self.prop(PR_VIEWS_ENTRYID).value))
        except NotFoundError:
            pass

    @views.setter
    def views(self, folder):
        self[PR_VIEWS_ENTRYID] = _bdec(folder.entryid)
        # TODO clear container class

    @property
    def common_views(self):
        """:class:`Folder` containing sub-folders acting as special views
        on the message store."""
        try:
            return _folder.Folder(self,
                _benc(self.prop(PR_COMMON_VIEWS_ENTRYID).value))
        except NotFoundError:
            pass

    @common_views.setter
    def common_views(self, folder):
        self[PR_COMMON_VIEWS_ENTRYID] = _bdec(folder.entryid)
        # TODO clear container class

    @property
    def drafts(self):
        """The store :class:`drafts folder <Folder>`."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(
                self._root, PR_IPM_DRAFTS_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @drafts.setter
    def drafts(self, folder):
        self._set_special_folder(folder, PR_IPM_DRAFTS_ENTRYID, 'IPF.Note')

    @property
    def wastebasket(self):
        """The store :class:`wastebasket <Folder>`."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(
                self.mapiobj, PR_IPM_WASTEBASKET_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @wastebasket.setter
    def wastebasket(self, folder):
        self[PR_IPM_WASTEBASKET_ENTRYID] = _bdec(folder.entryid)
        # TODO clear container class

    @property
    def journal(self):
        """The store :class:`journal folder <Folder>`."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(
                self._root, PR_IPM_JOURNAL_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @journal.setter
    def journal(self, folder):
        self._set_special_folder(folder, PR_IPM_JOURNAL_ENTRYID, 'IPF.Journal')

    @property
    def notes(self):
        """The store :class:`notes folder <Folder>`."""
        try:
            return _folder.Folder(self,
                _benc(HrGetOneProp(self._root, PR_IPM_NOTE_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @notes.setter
    def notes(self, folder):
        self._set_special_folder(folder, PR_IPM_NOTE_ENTRYID, 'IPF.StickyNote')

    @property
    def sentmail(self):
        """The store :class:`sentmail folder <Folder>`."""
        try:
            return _folder.Folder(self, _benc(HrGetOneProp(
                self.mapiobj, PR_IPM_SENTMAIL_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @sentmail.setter
    def sentmail(self, folder):
        self[PR_IPM_SENTMAIL_ENTRYID] = _bdec(folder.entryid)
        # TODO clear container class

    @property
    def tasks(self):
        """The store :class:`tasks folder <Folder>`."""
        try:
            return _folder.Folder(self,
                _benc(HrGetOneProp(self._root, PR_IPM_TASK_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @tasks.setter
    def tasks(self, folder):
        self._set_special_folder(folder, PR_IPM_TASK_ENTRYID, 'IPF.Task')

    @property
    def suggested_contacts(self):
        """The store :class`suggested contacts <Folder>`."""
        try:
            return _folder.Folder(self,
                self._extract_ipm_ol2007_entryid(RSF_PID_SUGGESTED_CONTACTS))
        except NotFoundError:
            pass
    # TODO setter

    @property
    def todo_search(self):
        """The store :class`todo search folder <Folder>`."""
        try:
            return _folder.Folder(self,
                self._extract_ipm_ol2007_entryid(RSF_PID_TODO_SEARCH))
        except NotFoundError:
            pass
    # TODO setter

    @property
    def rss(self):
        """The store :class`RSS folder <Folder>`."""
        try:
            return _folder.Folder(self,
                self._extract_ipm_ol2007_entryid(RSF_PID_RSS_SUBSCRIPTION))
        except NotFoundError:
            pass
    # TODO setter

    def _extract_ipm_ol2007_entryid(self, offset):
        # Extracts entryids from PR_IPM_OL2007_ENTRYIDS blob using logic from
        # common/Util.cpp Util::ExtractAdditionalRenEntryID
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
        """Delete properties, delegations, permissions, folders or items
        from store.

        :param objects: The object(s) to delete
        :param soft: Soft-delete items and folders (default False)
        """
        objects = _utils.arg_objects(objects,
            (_folder.Folder, _item.Item, Property, Delegation, Permission),
            'Store.delete')
        # TODO directly delete inbox rule?

        props = [o for o in objects if isinstance(o, Property)]
        if props:
            self.mapiobj.DeleteProps([p.proptag for p in props])
            _utils._save(self.mapiobj)

        delgs = [o for o in objects if isinstance(o, Delegation)]
        for d in delgs:
            d._delete()

        perms = [o for o in objects if isinstance(o, Permission)]
        for perm in perms:
            acl_table = self.mapiobj.OpenProperty(PR_ACL_TABLE,
                IID_IExchangeModifyTable, 0, 0)
            acl_table.ModifyTable(0, [ROWENTRY(ROW_REMOVE,
                [SPropValue(PR_MEMBER_ID, perm.mapirow[PR_MEMBER_ID])])])

        others = [o for o in objects \
            if isinstance(o, (_item.Item, _folder.Folder))]
        if others:
            self.root.delete(others, soft=soft)

    def folder(self, path=None, entryid=None, recurse=False, create=False,
            guid=None):
        """Return :class:`Folder` with given path/entryid.

        :param path: The path of the folder (optional)
        :param entryid: The entryid of the folder (optional)
        :param create: Create folder if it doesn't exist (default False)
        """

        if path is None and entryid is None and guid is None:
            raise ArgumentError('missing argument to identify folder')

        if guid is not None:
            # 01 -> entryid format version, 03 -> object type (folder)
            entryid = '00000000' + self.guid + '0100000003000000' + \
                guid + '00000000'

        if entryid is not None:
            try:
                return _folder.Folder(self, entryid)
            # TODO move to Folder
            except (MAPIErrorInvalidEntryid, MAPIErrorNotFound):
                raise NotFoundError("no folder with entryid '%s'" % entryid)

        return self.subtree.folder(path, recurse=recurse, create=create)

    def get_folder(self, path=None, entryid=None):
        """Return :class:`folder <Folder>` with given path/entryid
        or *None* if not found.

        :param path: The path of the folder (optional)
        :param entryid: The entryid of the folder (optional)
        """
        try:
            return self.folder(path, entryid=entryid)
        except NotFoundError:
            pass

    def create_folder(self, path=None, **kwargs):
        """Create :class:`folder <Folder>` under *subtree* with given path.

        :param path: The path of the folder, relative to *subtree*.
        """
        return self.subtree.create_folder(path, **kwargs)

    def folders(self, recurse=True, parse=True, **kwargs):
        """Return all :class:`folders <Folder>` under *subtree*.

        :param recurse: include all sub-folders (default True)
        """
        # parse=True
        if parse and getattr(self.server.options, 'folders', None):
            for path in self.server.options.folders:
                yield self.folder(path)
            return

        if self.subtree:
            for folder in self.subtree.folders(recurse=recurse, **kwargs):
                yield folder

    def mail_folders(self, **kwargs):
        # Mail folders are a fixed set of folders which all can contain mail
        # items, plus any other customly created folder of container class
        # IPF.Note.
        static = {}
        for n in ('inbox', 'outbox', 'sentmail', 'wastebasket', 'drafts', 'junk'):
            f = getattr(self, n)
            if f is not None:
                static[f.entryid] = True
                yield f
        # Find additional folders using a restrictions.
        restriction = Restriction(SPropertyRestriction(
            RELOP_EQ, PR_CONTAINER_CLASS_W,
            SPropValue(PR_CONTAINER_CLASS_W, 'IPF.Note')
        ))
        for folder in self.folders(restriction=restriction, **kwargs):
            # Yield additional folders, filtering by the fixed ones.
            if folder.entryid not in static:
                yield folder

    def contact_folders(self, **kwargs):
        restriction = Restriction(SPropertyRestriction(
            RELOP_EQ, PR_CONTAINER_CLASS_W,
            SPropValue(PR_CONTAINER_CLASS_W, 'IPF.Contact')
        ))
        return self.folders(restriction=restriction, **kwargs)

    def calendars(self, **kwargs):
        restriction = Restriction(SPropertyRestriction(
            RELOP_EQ, PR_CONTAINER_CLASS_W,
            SPropValue(PR_CONTAINER_CLASS_W, 'IPF.Appointment')
        ))
        return self.folders(restriction=restriction, **kwargs)

    # TODO store.findroot.create_folder()?
    def create_searchfolder(self, text=None):
        mapiobj = self.findroot.mapiobj.CreateFolder(FOLDER_SEARCH,
            str(uuid.uuid4()).encode(), 'comment'.encode(), None, 0)
        return _folder.Folder(self, mapiobj=mapiobj)

    def item(self, entryid=None, guid=None):
        """Return :class:`Item` with given entryid."""

        item = _item.Item() # TODO copy-pasting..
        item.store = self
        item.server = self.server

        if guid is not None:
            # 01 -> entryid format version, 05 -> object type (message)
            entryid = '00000000' + self.guid + '0100000005000000' + \
                guid + '00000000'

        if entryid is not None:
            eid = _utils._bdec_eid(entryid)
        else:
            raise ArgumentError("no guid or entryid specified")

        try:
            # TODO soft-deleted item?
            item.mapiobj = _utils.openentry_raw(self.mapiobj, eid, 0)
        except MAPIErrorNotFound:
            raise NotFoundError("no item with entryid '%s'" % entryid)
        except MAPIErrorInvalidEntryid:
            raise ArgumentError("invalid entryid: %r" % entryid)

        return item

    @property
    def size(self):
        """Store storage size."""
        return self.prop(PR_MESSAGE_SIZE_EXTENDED).value

    def config_item(self, name):
        """Retrieve the config item for the given name.

        :param name: The config item name
        """
        name = str(name)
        table = self.subtree.mapiobj.GetContentsTable(
            MAPI_DEFERRED_ERRORS | MAPI_ASSOCIATED)
        table.Restrict(SPropertyRestriction(
            RELOP_EQ, PR_SUBJECT_W, SPropValue(PR_SUBJECT_W, name)), 0)
        rows = table.QueryRows(1, 0)
        # No config item found, create new message
        if not rows:
            item = self.subtree.associated.create_item(
                message_class='IPM.Zarafa.Configuration', subject=name)
        else:
            mapiobj = self.subtree.mapiobj.OpenEntry(
                rows[0][0].Value, None, MAPI_MODIFY)
            item = _item.Item(mapiobj=mapiobj)
        return item

    @property
    def last_logon(self):
        """Return :datetime of the last logon on this store."""
        return self.get(PR_LAST_LOGON_TIME)

    @last_logon.setter
    def last_logon(self, value):
        self[PR_LAST_LOGON_TIME] = value

    @property
    def last_logoff(self):
        """Return :datetime of the last logoff on this store."""
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
        """Return :class:`AutoProcess` settings."""
        return AutoProcess(self)

    @property
    def autoaccept(self):
        """Return :class:`AutoAccept` settings."""
        return AutoAccept(self)

    @property
    def user(self):
        """Store :class:`owner <User>`."""
        try:
            userid = HrGetOneProp(self.mapiobj, PR_MAILBOX_OWNER_ENTRYID).Value
            return _user.User(self.server.sa.GetUser(
                userid, MAPI_UNICODE).Username, self.server)
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def archive_store(self):
        """Archive :class:`Store`."""
        # TODO merge namedprops stuff
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, MAPI_CREATE)
        PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)

        try:
            # support for multiple archives was a mistake, and is not and
            # _should not_ be used. so we just pick nr 0.
            entryid = HrGetOneProp(self.mapiobj, PROP_STORE_ENTRYIDS).Value[0]
        except MAPIErrorNotFound:
            return

        return Store(entryid=_benc(entryid), server=self.server) # TODO server?

    @archive_store.setter
    def archive_store(self, store):
        # TODO merge namedprops stuff
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, MAPI_CREATE)
        PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)
        PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

        # TODO only for detaching atm
        if store is None:
            self.mapiobj.DeleteProps([PROP_STORE_ENTRYIDS, PROP_ITEM_ENTRYIDS])
            _utils._save(self.mapiobj)

    @property
    def archive_folder(self):
        """Archive :class:`Folder` (in case multiple stores are archived
        to a single archive store)."""
        # TODO merge namedprops stuff
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, MAPI_CREATE)
        PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

        try:
            # support for multiple archives was a mistake, and is not and
            # _should not_ be used. so we just pick nr 0.
            arch_folderid = HrGetOneProp(
                self.mapiobj, PROP_ITEM_ENTRYIDS).Value[0]
        except MAPIErrorNotFound:
            return

        return self.archive_store.folder(entryid=_benc(arch_folderid))

    @property
    def company(self):
        """Store :class:`company <Company>`."""
        if self.server.multitenant:
            table = self.server.sa.OpenUserStoresTable(MAPI_UNICODE)
            table.Restrict(SPropertyRestriction(RELOP_EQ, PR_EC_STOREGUID,
                SPropValue(PR_EC_STOREGUID, _bdec(self.guid))), TBL_BATCH)
            for row in table.QueryRows(1, 0):
                storetype = PpropFindProp(row, PR_EC_STORETYPE)
                if storetype.Value == ECSTORE_TYPE_PUBLIC:
                    # TODO bug in ECUserStoreTable.cpp?
                    companyname = PpropFindProp(row, PR_EC_USERNAME_W)
                    if not companyname:
                        companyname = PpropFindProp(row, PR_EC_COMPANY_NAME_W)
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
            return (self.user is None or self.user.store is None or \
                self.user.store.guid != self.guid)

    def permissions(self):
        """Return all :class:`permissions <Permission>` set for the store."""
        return _utils.permissions(self)

    def permission(self, member, create=False):
        """Return :class:`permission <Permission>` for :class:`User` or
        :class:`Group` set for the store.

        :param member: user or group
        :param create: create new permission for this user or group
        """
        return _utils.permission(self, member, create)

    def permissions_dumps(self, **kwargs):
        """Serialize :class:`permissions <Permission>` set for the store."""
        return _permissions_dumps(self, **kwargs)

    def permissions_loads(self, data, **kwargs):
        """Deserialize :class:`permissions <Permission>`.

        :param data: Serialized data
        """
        return _permissions_loads(self, data, **kwargs)

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
                username = \
                    self.server.sa.GetUser(entryid, MAPI_UNICODE).Username
            except MAPIErrorNotFound:
                raise NotFoundError("no user found with userid '%s'" % \
                    _benc(entryid))
            except MAPIErrorInvalidEntryid:
                self.server.log.warning("invalid delegation entryid: '%s'", _benc(entryid))
                continue
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

    def delegations_dumps(self, stats=None):
        """Serialize :class:`Delegation` settings."""
        log = self.server.log

        # TODO dump delegation flags (see _fbmsg_delgs above)

        usernames = []
        with log_exc(log, stats):
            try:
                usernames = [d.user.name for d in self.delegations()]
            except (MAPIErrorNotFound, NotFoundError):
                log.warning("could not load delegations")

        return _utils.pickle_dumps({
            b'usernames': usernames
        })

    def delegations_loads(self, data, stats=None):
        """Deserialize :class:`Delegation` settings.

        :param data: Serialized data
        """
        server = self.server
        log = self.server.log

        data = _utils.pickle_loads(data)
        if isinstance(data, dict):
            data = data[b'usernames']

        with log_exc(log, stats):
            users = []
            for name in data:
                try:
                    users.append(server.user(name))
                except NotFoundError:
                    log.warning(
                        "skipping delegation for unknown user '%s'", name)

            # TODO not in combination with --import-root, -f?
            self.delete(self.delegations())
            for user2 in users:
                self.delegation(user2, create=True)

    # TODO optionally include folder settings but not data
    def dumps(self):
        """Serialize entire store, including all settings."""

        data = {}

        data['settings'] = self.settings_dumps()

        data['folders'] = folders = {}
        for folder in self.folders():
            folders[folder.path] = folder.dumps()

        return _utils.pickle_dumps(data)

    def loads(self, data):
        """Deserialize entire store, including all settings.

        :param data: Serialized data
        """
        data = _utils.pickle_loads(data)

        self.settings_loads(data['settings'])

        for path, fdata in data['folders'].items():
            folder = self.folder(path, create=True)
            folder.loads(fdata)

    def settings_dumps(self):
        """Serialize store settings."""
        data = {}

        data['permissions'] = self.permissions_dumps()
        data['delegations'] = self.delegations_dumps()

        props = {}
        for prop in self.props():
            if prop.proptag in SETTINGS_PROPTAGS:
                props[prop.proptag] = prop.mapiobj.Value

        data['props'] = props

        return _utils.pickle_dumps(data)

    def settings_loads(self, data):
        """Deserialize (overriding) all store settings.

        :param data: Serialized data
        """
        data = _utils.pickle_loads(data)

        self.delegations_loads(data['delegations'])
        self.permissions_loads(data['permissions'])

        props = []
        for proptag, value in data['props'].items():
            if proptag in SETTINGS_PROPTAGS:
                props.append(SPropValue(proptag, value))

        self.mapiobj.SetProps(props)
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def send_only_to_delegates(self):
        """When sending meetingrequests to delegates, do not send them
        to the owner."""
        return Delegation._send_only_to_delegates(self)

    @send_only_to_delegates.setter
    def send_only_to_delegates(self, value):
        Delegation._set_send_only_to_delegates(self, value)

    def favorites(self):
        """Returns all favorite :class:`folders <Folder>`."""
        restriction = Restriction(SPropertyRestriction(
            RELOP_EQ, PR_MESSAGE_CLASS_W,
            SPropValue(PR_MESSAGE_CLASS_W, 'IPM.Microsoft.WunderBar.Link')
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
                # TODO: Handle favorites from public stores
                if store_entryid == self.entryid:
                    yield self.folder(entryid=_benc(entryid.value))
                else:
                    store = Store(entryid=_benc(store_entryid.value),
                        server=self.server)
                    yield store.folder(entryid=_benc(entryid.value))
            except NotFoundError:
                pass

    # TODO remove_favorite, folder.favorite
    def add_favorite(self, folder):
        """Add :class:`Folder` to favorites."""
        if folder in self.favorites():
            raise DuplicateError(
                "folder '%s' already in favorites" % folder.name)

        item = self.common_views.associated.create_item()
        item[PR_MESSAGE_CLASS_W] = 'IPM.Microsoft.WunderBar.Link'
        item[PR_WLINK_ENTRYID] = _bdec(folder.entryid)
        item[PR_WLINK_STORE_ENTRYID] = _bdec(folder.store.entryid)

    def _subprops(self, value):
        result = {}
        pos = 0
        while pos < len(value):
            id_ = value[pos]
            cb = value[pos + 1]
            result[id_] = value[pos + 2:pos + 2 + cb]
            pos += 2 + cb
        return result

    def searches(self):
        """Return all permanent search folders."""
        findroot = self.root.folder('FINDER_ROOT') # TODO

        # extract special type of guid from search folder
        # PR_EXTENDED_FOLDER_FLAGS to match against
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
            SPropValue(PR_MESSAGE_CLASS_W, 'IPM.Microsoft.WunderBar.SFInfo')),
            TBL_BATCH)

        for row in table.QueryRows(2147483647, 0):
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
        item[PR_MESSAGE_CLASS_W] = 'IPM.Microsoft.WunderBar.SFInfo'
        item[PR_WB_SF_ID] = id_

    @property
    def home_server(self):
        if self.user:
            return self.user.home_server
        else:
            try:
                for node in self.server.nodes(): # TODO faster?
                    if (node.guid == \
                        _benc(self.prop(PR_MAPPING_SIGNATURE).value)):
                        return node.name
            except MAPIErrorNotFound:
                return self.server.name

    @property
    def type_(self):
        """Store type (*private*, *public*, *archive*)."""
        table = self.server.sa.OpenUserStoresTable(MAPI_UNICODE)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_EC_STOREGUID,
            SPropValue(PR_EC_STOREGUID, _bdec(self.guid))), TBL_BATCH)
        for row in table.QueryRows(1, 0):
            storetype = PpropFindProp(row, PR_EC_STORETYPE)
            if storetype:
                return {
                    ECSTORE_TYPE_PRIVATE: 'private',
                    ECSTORE_TYPE_ARCHIVE: 'archive',
                    ECSTORE_TYPE_PUBLIC: 'public',
                }[storetype.Value]

    @property
    def freebusy(self):
        """:class:`Freebusy <Freebusy>` information."""
        return FreeBusy(self)

    def _name_id(self, name_tuple):
        # TODO: use _pidlid_proptag everywhere
        id_ = self._name_id_cache.get(name_tuple)
        if id_ is None:
            named_props = [MAPINAMEID(*name_tuple)]
            # TODO use MAPI_CREATE, or too dangerous because of
            # potential db overflow?
            id_ = self.mapiobj.GetIDsFromNames(named_props, 0)[0]
            self._name_id_cache[name_tuple] = id_
        return id_

    def _pidlid_proptag(self, pidlid):
        id_ = self._pidlid_cache.get(pidlid)
        if id_ is None:
            id_ = _name_to_proptag(pidlid, self.mapiobj)[0]
            self._pidlid_cache[pidlid] = id_
        return id_

    def __eq__(self, s): # TODO check same server?
        if isinstance(s, Store):
            return self.guid == s.guid
        return False

    def subscribe(self, sink, **kwargs):
        """Subscribe to store notifications

        :param sink: Sink instance with callbacks to process notifications
        :param object_types: Tracked objects (*item*, *folder*)
        :param folder_types: Tracked folders (*mail*, *contacts*, *calendar*)
        :param event_types: Event types (*created*, *updated*, *deleted*)
        """
        _notification.subscribe(self, None, sink, **kwargs)

    def unsubscribe(self, sink):
        _notification.unsubscribe(self, sink)

    def __ne__(self, s):
        return not self == s

    def __iter__(self):
        return self.folders()

    def __unicode__(self):
        return "Store('%s')" % self.guid
