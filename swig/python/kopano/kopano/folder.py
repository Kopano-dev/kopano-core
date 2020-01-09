# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import codecs
import collections
import mailbox
import sys
import time
from xml.etree import ElementTree

import icalmapi

from MAPI import (
    MAPI_MODIFY, MAPI_ASSOCIATED, ROW_ADD,
    RELOP_GT, RELOP_LT, RELOP_EQ, MAPI_CREATE,
    DEL_ASSOCIATED, DEL_FOLDERS, DEL_MESSAGES, COPY_SUBFOLDERS,
    BOOKMARK_BEGINNING, ROW_REMOVE, MESSAGE_MOVE, FOLDER_MOVE,
    FOLDER_GENERIC, MAPI_UNICODE, FL_SUBSTRING, FL_IGNORECASE,
    SEARCH_RECURSIVE, SEARCH_REBUILD, PT_MV_BINARY, PT_BINARY,
    MAPI_DEFERRED_ERRORS
)
from MAPI.Tags import (
    PR_ENTRYID, IID_IMAPIFolder, SHOW_SOFT_DELETES, PR_SOURCE_KEY,
    PR_PARENT_ENTRYID, PR_EC_HIERARCHYID, PR_FOLDER_CHILD_COUNT,
    PR_DISPLAY_NAME_W, PR_CONTAINER_CLASS_W, PR_CONTENT_UNREAD,
    PR_MESSAGE_DELIVERY_TIME, MNID_ID, PT_SYSTIME, PT_BOOLEAN,
    DELETE_HARD_DELETE, PR_MESSAGE_SIZE, PR_ACL_TABLE, PR_MESSAGE_CLASS_W,
    PR_MEMBER_ID, PR_RULES_TABLE, IID_IExchangeModifyTable,
    IID_IMAPITable, PR_CONTAINER_CONTENTS, PR_RULES_DATA, PR_STORE_ENTRYID,
    PR_FOLDER_ASSOCIATED_CONTENTS, PR_CONTAINER_HIERARCHY,
    PR_SUBJECT_W, PR_BODY_W, PR_DISPLAY_TO_W, PR_CREATION_TIME,
    CONVENIENT_DEPTH, PR_CONTENT_COUNT, PR_ASSOC_CONTENT_COUNT,
    PR_DELETED_MSG_COUNT, PR_LAST_MODIFICATION_TIME, PR_MESSAGE_ATTACHMENTS,
    PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID, PR_CHANGE_KEY, PR_EXCEPTION_STARTTIME,
    PR_EXCEPTION_ENDTIME, PR_RULE_ID, PR_RULE_STATE, ST_ENABLED,
    PR_RULE_PROVIDER_DATA, PR_RULE_SEQUENCE, PR_RULE_NAME_W,
    PR_RULE_CONDITION, PT_LONG, PR_ATTR_HIDDEN,
)
from MAPI.Defs import (
    HrGetOneProp, CHANGE_PROP_TYPE
)
from MAPI.Struct import (
    MAPIErrorNoAccess, MAPIErrorNotFound, MAPIErrorNoSupport,
    MAPIErrorInvalidEntryid, MAPIErrorCollision, SPropValue,
    MAPINAMEID, SOrRestriction, SAndRestriction, SPropertyRestriction,
    SSubRestriction, SContentRestriction, ROWENTRY
)
from MAPI.Time import unixtime

from .properties import Properties
from .permission import Permission, _permissions_dumps, _permissions_loads
from .restriction import Restriction
from .recurrence import Occurrence
from .rule import Rule
from .table import Table
from .property_ import Property
from .defs import (
    PSETID_Appointment, UNESCAPED_SLASH_RE,
    ENGLISH_FOLDER_MAP, NAME_RIGHT, NAMED_PROPS_ARCHIVER
)
from .errors import NotFoundError, ArgumentError, DuplicateError
from .query import _query_to_restriction

from .compat import (
    bdec as _bdec, benc as _benc
)

try:
    from . import log as _log
except ImportError: # pragma: no cover
    _log = sys.modules[__package__ + '.log']
try:
    from . import user as _user
except ImportError: # pragma: no cover
    _user = sys.modules[__package__ + '.user']
try:
    from . import store as _store
except ImportError: # pragma: no cover
    _store = sys.modules[__package__ + '.store']
try:
    from . import item as _item
except ImportError: # pragma: no cover
    _item = sys.modules[__package__ + '.item']
try:
    from . import utils as _utils
except ImportError: # pragma: no cover
    _utils = sys.modules[__package__ + '.utils']
try:
    from . import ics as _ics
except ImportError: # pragma: no cover
    _ics = sys.modules[__package__ + '.ics']
try:
    from . import notification as _notification
except ImportError: # pragma: no cover
    _notification = sys.modules[__package__ + '.notification']

def _unbase64(s):
    return codecs.decode(codecs.encode(s, 'ascii'), 'base64')

def _base64(s):
    return codecs.decode(codecs.encode(s, 'base64').strip(), 'ascii')

# TODO generalize, autogenerate basic item getters/setters?
PROPMAP = {
    'subject': PR_SUBJECT_W,
    'received': PR_MESSAGE_DELIVERY_TIME,
    'created': PR_CREATION_TIME,
}

class Folder(Properties):
    """Folder class

    Abstraction for collections of :class:`items <Item>`. For example,
    folders, calendars and contact folders.

    Includes all functionality from :class:`Properties`.
    """

    def __init__(self, store=None, entryid=None, associated=False,
                 deleted=False, mapiobj=None, _check_mapiobj=True, cache={}):
        if store:
            self.store = store
            self.server = store.server
        # TODO else, determine store dynamically?
        if mapiobj:
            self._mapiobj = mapiobj
            self._entryid = HrGetOneProp(self.mapiobj, PR_ENTRYID).Value
        elif entryid:
            try:
                self._entryid = _bdec(entryid)
            except:
                raise ArgumentError('invalid entryid: %r' % entryid)

        self._content_flag = MAPI_ASSOCIATED if associated else \
            (SHOW_SOFT_DELETES if deleted else 0)
        self._sourcekey = None
        self._mapiobj = None

        if _check_mapiobj: # raise error for specific key
            self.mapiobj

        self._cache = cache
        self._iter = None

    @property
    def mapiobj(self):
        """Underlying MAPI object."""
        if self._mapiobj:
            return self._mapiobj

        try:
            self._mapiobj = self.store.mapiobj.OpenEntry(self._entryid,
                IID_IMAPIFolder, MAPI_MODIFY)
        except (MAPIErrorNotFound, MAPIErrorInvalidEntryid):
            try:
                self._mapiobj = self.store.mapiobj.OpenEntry(self._entryid,
                    IID_IMAPIFolder, MAPI_MODIFY | SHOW_SOFT_DELETES)
            # TODO check too late?
            except (MAPIErrorNotFound, MAPIErrorInvalidEntryid):
                raise NotFoundError(
                    "no folder with entryid '%s'" % _benc(self._entryid))
        except MAPIErrorNoAccess: # TODO
            self._mapiobj = self.store.mapiobj.OpenEntry(self._entryid,
                IID_IMAPIFolder, 0)

        return self._mapiobj

    @mapiobj.setter
    def mapiobj(self, mapiobj):
        self._mapiobj = mapiobj

    @property
    def entryid(self):
        """Folder entryid."""
        return _benc(self._entryid)

    @property
    def guid(self):
        return self.entryid[56:88] # TODO assumes hex

    @property
    def sourcekey(self):
        """Folder sourcekey."""
        if not self._sourcekey:
            self._sourcekey = \
                _benc(HrGetOneProp(self.mapiobj, PR_SOURCE_KEY).Value)
        return self._sourcekey

    @property
    def parent(self):
        """:class:`Parent <Folder>` folder"""
        parent_eid = self._get_fast(PR_PARENT_ENTRYID)
        if parent_eid:
            parent_eid = _benc(parent_eid)
            if parent_eid != self.entryid: # root parent is itself
                return Folder(self.store, parent_eid, _check_mapiobj=False)

    @property
    def hierarchyid(self):
        """Folder hierarchy (SQL) id."""

        return self.prop(PR_EC_HIERARCHYID).value

    @property
    def subfolder_count(self):
        """Direct subfolder count."""
        return self.prop(PR_FOLDER_CHILD_COUNT).value

    @property
    def subfolder_count_recursive(self):
        """Subfolder count (recursive)."""
        # TODO faster?
        flags = MAPI_UNICODE | self._content_flag | CONVENIENT_DEPTH | \
            MAPI_DEFERRED_ERRORS
        mapitable = self.mapiobj.GetHierarchyTable(flags)

        table = Table(
            self.server,
            self.mapiobj,
            mapitable,
            PR_CONTAINER_HIERARCHY,
        )

        return table.count

    @property
    def name(self):
        """Folder name."""
        name = self._get_fast(PR_DISPLAY_NAME_W)

        if name is not None:
            return name.replace('/', '\\/')
        else:
            # root folder doesn't have PR_DISPLAY_NAME_W
            if self.entryid == self.store.root.entryid:
                return 'ROOT'
            else:
                return ''

    @property
    def path(self):
        """Folder path."""
        names = []
        parent = self
        subtree_entryid = self.store.subtree.entryid
        root_entryid = self.store.root.entryid
        # parent entryids coming from a table are not converted like normal
        # parent entryids for certains public store folders (via getprops,
        # or store PR_IPM_SUBTREE_ENTRYID), so we match with this property,
        # which is also not converted
        pub_entryid = \
            _benc(self.store.get(PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID, b''))

        while parent:
            parent_eid = parent.entryid
            if parent_eid in (subtree_entryid, pub_entryid):
                return '/'.join(reversed(names)) if names else None
            elif parent_eid == root_entryid:
                return
            else:
                names.append(parent.name)
                parent = parent.parent

    @name.setter
    def name(self, name):
        self.mapiobj.SetProps([SPropValue(PR_DISPLAY_NAME_W, str(name))])
        _utils._save(self.mapiobj)

    @property
    def container_class(self): # TODO return '' by default?
        """
        Describes the type of items a folder holds. Possible values:

        * IPF.Appointment
        * IPF.Contact
        * IPF.Journal
        * IPF.Note
        * IPF.StickyNote
        * IPF.Task

        https://msdn.microsoft.com/en-us/library/aa125193(v=exchg.65).aspx
        """
        try:
            return self.prop(PR_CONTAINER_CLASS_W).value
        except NotFoundError:
            pass

    @container_class.setter
    def container_class(self, value):
        if value is None:
            return
        prop = SPropValue(PR_CONTAINER_CLASS_W, str(value))
        self.mapiobj.SetProps([prop])
        _utils._save(self.mapiobj)

    @property
    def hidden(self):
        try:
            return self.prop(PR_ATTR_HIDDEN).value
        except NotFoundError:
            return False

    @hidden.setter
    def hidden(self, value):
        prop = SPropValue(PR_ATTR_HIDDEN, value)
        self.mapiobj.SetProps([prop])
        _utils._save(self.mapiobj)

    @property
    def unread(self):
        """Unread item count."""
        return self._get_fast(PR_CONTENT_UNREAD)

    def item(self, entryid=None, sourcekey=None):
        """ Return :class:`Item` with given entryid or sourcekey

        :param entryid: item entryid (optional)
        :param sourcekey: item sourcekey (optional)
        """
        if entryid is not None:
            eid = _utils._bdec_eid(entryid)

        elif sourcekey is not None: # TODO 1 SQL per row with empty cache!
            try:
                restriction = SPropertyRestriction(RELOP_EQ, PR_SOURCE_KEY,
                    SPropValue(PR_SOURCE_KEY, _bdec(sourcekey)))
            except:
                raise ArgumentError("invalid sourcekey: %r" % sourcekey)
            table = self.mapiobj.GetContentsTable(MAPI_DEFERRED_ERRORS)
            table.SetColumns([PR_ENTRYID, PR_SOURCE_KEY], 0)
            table.Restrict(restriction, 0)
            rows = list(table.QueryRows(-1, 0))
            if not rows:
                raise NotFoundError("no item with sourcekey '%s'" % sourcekey)
            eid = rows[0][0].Value

        else:
            raise ArgumentError("no entryid or sourcekey specified")

        # open message with entryid
        try:
            mapiobj = _utils.openentry_raw(self.store.mapiobj, eid,
                self._content_flag)
        except MAPIErrorNotFound:
            if sourcekey is not None:
                raise NotFoundError("no item with sourcekey '%s'" % sourcekey) from None
            else:
                raise NotFoundError("no item with entryid '%s'" % entryid) from None
        except MAPIErrorInvalidEntryid:
            raise ArgumentError("invalid entryid: %r" % entryid) from None

        item = _item.Item(self, mapiobj=mapiobj)
        return item

    def items(self, restriction=None, page_start=None, page_limit=None,
              order=None, query=None):
        """Return all :class:`items <Item>` in folder, reverse sorted on
        received date.

        :param restriction: apply :class:`restriction <Restriction>`
        :param order: order by (limited set of) attributes, e.g. 'subject',
            '-subject' (reverse order), or ('subject', '-received').
        :param page_start: skip this many items from the start
        :param page_limit: return up to this many items
        :param query: use search query
        """
        # TODO determine preload columns dynamically, based on usage pattern
        columns = [
            PR_ENTRYID,
            PR_MESSAGE_DELIVERY_TIME,
            PR_SUBJECT_W, # watch out: table unicode data is max 255 chars
            PR_LAST_MODIFICATION_TIME,
            PR_CHANGE_KEY,
            PR_MESSAGE_CLASS_W,
            PR_BODY_W, # body preview, not entire body!
            PR_SOURCE_KEY,
        ]

        if query is not None:
            if self.container_class == 'IPF.Contact':
                type_ = 'contact'
            else:
                type_ = 'message'
            restriction = _query_to_restriction(query, type_, self.store)

        try:
            table = Table(
                self.server,
                self.mapiobj,
                self.mapiobj.GetContentsTable(self._content_flag | \
                    MAPI_DEFERRED_ERRORS),
                PR_CONTAINER_CONTENTS,
                columns=columns,
                restriction=restriction,
            )
        except MAPIErrorNoSupport:
            return

        # TODO MAPI has more than just ascend/descend
        if order is None:
            order = '-received'
        if not isinstance(order, tuple):
            order = (order,)
        sorttags = []
        for term in order:
            if term.startswith('-'):
                sorttags.append(-PROPMAP[term[1:]])
            else:
                sorttags.append(PROPMAP[term])
        table.sort(tuple(sorttags))

        for row in table.rows(page_start=page_start, page_limit=page_limit):
            item = _item.Item(
                self,
                entryid=row[0].value,
                content_flag=self._content_flag,
                cache=dict(zip(columns, row))
            )
            yield item

    def occurrences(self, start=None, end=None, page_start=None,
            page_limit=None, order=None):
        """For applicable folder types (e.g., calendars), return
        all :class:`occurrences <Occurrence>`.

        :param start: start time (optional)
        :param end: end time (optional)
        """
        count = 0
        pos = 0
        if start and end:
            startstamp = time.mktime(start.timetuple())
            endstamp = time.mktime(end.timetuple())

            # TODO use shortcuts and default type (database) to avoid
            # MAPI snake wrestling
            NAMED_PROPS = [MAPINAMEID(PSETID_Appointment, MNID_ID, x)
                for x in (33285, 33293, 33294, 33315, 33301, 33333, 33334, 33331, 33302)]
            ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS, MAPI_CREATE)
            busystatus = ids[0] | PT_LONG
            startdate = ids[1] | PT_SYSTIME
            enddate = ids[2] | PT_SYSTIME
            recurring = ids[3] | PT_BOOLEAN
            all_day = ids[4] | PT_BOOLEAN
            clip_start = ids[5] | PT_SYSTIME
            clip_end = ids[6] | PT_SYSTIME
            tzinfo = ids[7] | PT_BINARY
            blob = ids[8] | PT_BINARY

            restriction = SOrRestriction([
                # non-recurring: normal start/end
                SAndRestriction([
                    SPropertyRestriction(RELOP_GT, enddate,
                        SPropValue(enddate, unixtime(startstamp))),
                    SPropertyRestriction(RELOP_LT, startdate,
                        SPropValue(startdate, unixtime(endstamp))),
                ]),
                # recurring: range start/end
                SAndRestriction([
                    SPropertyRestriction(RELOP_GT, clip_end,
                        SPropValue(clip_end, unixtime(startstamp))),
                    SPropertyRestriction(RELOP_LT, clip_start,
                        SPropValue(clip_start, unixtime(endstamp))),
                ]),
                # exceptions: exception start/end in attachment
                SAndRestriction([
                    SPropertyRestriction(RELOP_EQ, recurring,
                        SPropValue(recurring, True)),
                    SSubRestriction(
                        PR_MESSAGE_ATTACHMENTS,
                        SAndRestriction([
                            SPropertyRestriction(RELOP_LT,
                                PR_EXCEPTION_STARTTIME,
                                SPropValue(PR_EXCEPTION_STARTTIME,
                                    unixtime(endstamp))),
                            SPropertyRestriction(RELOP_GT,
                                PR_EXCEPTION_ENDTIME,
                                SPropValue(PR_EXCEPTION_ENDTIME,
                                    unixtime(startstamp))),
                        ])
                    )
                ])
            ])

            columns = [
                PR_ENTRYID,
                PR_SUBJECT_W, # watch out: table unicode data is max 255 chars
                PR_LAST_MODIFICATION_TIME,
                PR_CHANGE_KEY,
                startdate,
                enddate,
                recurring,
                all_day,
                busystatus,
                tzinfo,
                blob, # watch out: can be larger than 255 chars.
            ]

            table = Table(
                self.server,
                self.mapiobj,
                self.mapiobj.GetContentsTable(MAPI_DEFERRED_ERRORS),
                PR_CONTAINER_CONTENTS,
                columns=columns,
            )
            table.mapitable.Restrict(restriction, 0)
            for row in table.rows():
                item = _item.Item(
                    self,
                    entryid=row[0].value,
                    content_flag=self._content_flag,
                    cache=dict(zip(columns, row))
                )
                for occurrence in item.occurrences(start, end):
                    if page_start is None or pos >= page_start:
                        yield occurrence
                        count += 1
                    if page_limit is not None and count >= page_limit:
                        break
                    pos += 1
                if page_limit is not None and count >= page_limit:
                    break

        else:
            for item in self:
                for occurrence in item.occurrences(start, end):
                    if page_start is None or pos >= page_start:
                        yield occurrence
                        count += 1
                    if page_limit is not None and count >= page_limit:
                        break
                    pos += 1
                if page_limit is not None and count >= page_limit:
                    break

    def create_item(self, eml=None, ics=None, vcf=None, load=None, loads=None,
            attachments=True, save=True, **kwargs): # TODO associated
        """Create :class:`item <Item>`.

        :param eml: pass eml data/file (optional)
        :param ics: pass iCal data/file (optional)
        :param vcf: pass vcf data/file (optional)
        :param load: pass serialized item file (optional)
        :param loads: pass serialized item data (optional)
        """
        read = kwargs.get('read') # needs to be set before first save
        item = _item.Item(self, eml=eml, ics=ics, vcf=vcf, load=load,
            loads=loads, attachments=attachments, create=True, save=save,
            read=read)
        for key, val in kwargs.items():
            if key != 'read':
                setattr(item, key, val)
        return item

    # TODO: always hard delete or but we should also provide 'softdelete' which
    # moves the item to the wastebasket
    def empty(self, recurse=True, associated=False):
        """Delete folder contents (items and subfolders)

        :param recurse: delete subfolders (default True)
        :param associated: delete associated contents (default False)
        """
        if recurse:
            flags = DELETE_HARD_DELETE
            if associated:
                flags |= DEL_ASSOCIATED
            self.mapiobj.EmptyFolder(0, None, flags)
        else:
            self.delete(self.items()) # TODO look at associated flag!

    @property
    def size(self): # TODO bit slow perhaps? :P
        """Folder storage size."""
        try:
            table = Table(
                self.server,
                self.mapiobj,
                self.mapiobj.GetContentsTable(self._content_flag | \
                    MAPI_DEFERRED_ERRORS),
                PR_CONTAINER_CONTENTS,
                columns=[PR_MESSAGE_SIZE],
            )
        except MAPIErrorNoSupport:
            return 0

        table.mapitable.SeekRow(BOOKMARK_BEGINNING, 0)
        size = 0
        for row in table.rows():
            size += row[0].value
        return size

    @property
    def count(self):
        """Folder item count."""
        # reopen to get up-to-date counters..
        # TODO use flags when opening folder?
        folder = self.store.folder(entryid=self.entryid)

        if self._content_flag == 0:
            proptag = PR_CONTENT_COUNT
        elif self._content_flag == MAPI_ASSOCIATED:
            proptag = PR_ASSOC_CONTENT_COUNT
        elif self._content_flag == SHOW_SOFT_DELETES:
            proptag = PR_DELETED_MSG_COUNT
        return folder.get(proptag, 0)

    def recount(self):
        self.server.sa.ResetFolderCount(_bdec(self.entryid))

    def _get_entryids(self, items):
        item_entryids = [_bdec(item.entryid)
            for item in items if isinstance(item, _item.Item)]
        folder_entryids = [_bdec(item.entryid)
            for item in items if isinstance(item, Folder)]
        perms = [item for item in items if isinstance(item, Permission)]
        props = [item for item in items if isinstance(item, Property)]
        occs = [item for item in items if isinstance(item, Occurrence)]
        rules = [item for item in items if isinstance(item, Rule)]
        return item_entryids, folder_entryids, perms, props, occs, rules

    def delete(self, objects, soft=False): # TODO associated
        """Delete items, subfolders, properties or permissions from folder.

        :param objects: The object(s) to delete
        :param soft: soft-delete items/folders (default False)
        """
        objects = _utils.arg_objects(
            objects,
            (_item.Item, Folder, Permission, Property, Occurrence, Rule),
            'Folder.delete'
        )
        item_entryids, folder_entryids, perms, props, occs, rules = \
            self._get_entryids(objects)
        if item_entryids:
            if soft:
                self.mapiobj.DeleteMessages(item_entryids, 0, None, 0)
            else:
                self.mapiobj.DeleteMessages(item_entryids, 0, None,
                    DELETE_HARD_DELETE)
        for entryid in folder_entryids:
            if soft:
                self.mapiobj.DeleteFolder(entryid, 0, None,
                    DEL_FOLDERS | DEL_MESSAGES)
            else:
                self.mapiobj.DeleteFolder(entryid, 0, None,
                    DEL_FOLDERS | DEL_MESSAGES | DELETE_HARD_DELETE)
        for perm in perms:
            acl_table = self.mapiobj.OpenProperty(PR_ACL_TABLE,
                IID_IExchangeModifyTable, 0, 0)
            acl_table.ModifyTable(0, [ROWENTRY(ROW_REMOVE,
                [SPropValue(PR_MEMBER_ID, perm.mapirow[PR_MEMBER_ID])])])
        for rule in rules:
            rule_table = self.mapiobj.OpenProperty(PR_RULES_TABLE,
                IID_IExchangeModifyTable, MAPI_UNICODE, 0)
            rule_table.ModifyTable(0, [ROWENTRY(ROW_REMOVE,
                [SPropValue(PR_RULE_ID, rule.mapirow[PR_RULE_ID])])])

        if props:
            self.mapiobj.DeleteProps([prop.proptag for prop in props])
        for occ in occs:
            if occ.item.recurring:
                occ.item.delete(occ)
            else:
                self.delete(occ.item)

    def copy(self, objects, folder, _delete=False):
        """Copy items or subfolders to folder.

        :param objects: The items or subfolders to copy
        :param folder: The target folder
        """
        objects = _utils.arg_objects(
            objects, (_item.Item, Folder), 'Folder.copy')
        item_entryids, folder_entryids, _, _, _, _ = \
            self._get_entryids(objects)
        # TODO copy/move perms?? TODO error for perms/props
        if item_entryids:
            self.mapiobj.CopyMessages(item_entryids, IID_IMAPIFolder,
                folder.mapiobj, 0, None, (MESSAGE_MOVE if _delete else 0))
        for entryid in folder_entryids:
            self.mapiobj.CopyFolder(entryid, IID_IMAPIFolder, folder.mapiobj,
                None, 0, None,
                COPY_SUBFOLDERS | (FOLDER_MOVE if _delete else 0))

    def move(self, objects, folder):
        """Move items or subfolders to folder

        :param objects: The item(s) or subfolder(s) to move
        :param folder: The target folder
        """
        self.copy(objects, folder, _delete=True)

    def folder(self, path=None, entryid=None, recurse=False, create=False):
        """Return :class:`Folder` with given path or entryid

        :param path: Folder path (optional)
        :param entryid: Folder entryid (optional)
        :param create: Create folder if it doesn't exist (default False)
        """
        if entryid is not None:
            try:
                return Folder(self.store, entryid)
            except (MAPIErrorInvalidEntryid, MAPIErrorNotFound, TypeError):
                raise NotFoundError('no folder with entryid "%s"' % entryid)
        elif path is None:
            raise ArgumentError('missing argument to identify folder')

        if path is None:
            raise ArgumentError('no path or entryid specified')
        # TODO MAPI folders may contain '/' (and '\') in their names..
        if '/' in path.replace('\\/', ''):
            subfolder = self
            for name in UNESCAPED_SLASH_RE.split(path):
                subfolder = subfolder.folder(name, create=create,
                    recurse=False)
            return subfolder
        # TODO depth==0?
        if self == self.store.subtree and path in ENGLISH_FOLDER_MAP:
            f = getattr(self.store, ENGLISH_FOLDER_MAP[path], None)
            if f:
                path = f.name

        if create:
            name = path.replace('\\/', '/')
            try:
                mapifolder = self.mapiobj.CreateFolder(FOLDER_GENERIC,
                    str(name), '', None, MAPI_UNICODE)
                return Folder(self.store, _benc(HrGetOneProp(mapifolder,
                    PR_ENTRYID).Value))
            except MAPIErrorCollision:
                pass

        name = path.replace('\\/', '/')
        restriction = Restriction(SPropertyRestriction(RELOP_EQ,
            PR_DISPLAY_NAME_W, SPropValue(PR_DISPLAY_NAME_W, str(name))))

        folders = list(self.folders(recurse=recurse, restriction=restriction))
        if not folders:
            raise NotFoundError("no such folder: '%s'" % path)

        return folders[0]

    def get_folder(self, path=None, entryid=None):
        """Return :class:`folder <Folder>` with given name/entryid or
        *None* if not found

        :param path: Folder path (optional)
        :param entryid: Folder entryid (optional)
        """
        try:
            return self.folder(path, entryid=entryid)
        except NotFoundError:
            pass

    def folders(self, recurse=True, restriction=None, page_start=None,
                page_limit=None, order=None):
        """ Return all :class:`sub-folders <Folder>` in folder

        :param recurse: include all sub-folders
        :param restriction: apply :class:`restriction <Restriction>`
        :param page_start: skip this many items from the start
        :param page_limit: return up to this many items
        """
        # TODO implement order arg

        columns = [
            PR_ENTRYID,
            PR_PARENT_ENTRYID,
            PR_DISPLAY_NAME_W,
            PR_LAST_MODIFICATION_TIME,
            PR_CONTENT_UNREAD,
        ]

        flags = MAPI_UNICODE | self._content_flag
        if recurse:
            flags |= CONVENIENT_DEPTH | MAPI_DEFERRED_ERRORS

        try:
            mapitable = self.mapiobj.GetHierarchyTable(flags)

            table = Table(
                self.server,
                self.mapiobj,
                mapitable,
                PR_CONTAINER_HIERARCHY,
                columns=columns,
                restriction=restriction,
            )
        except MAPIErrorNoSupport: # TODO webapp search folder?
            return

        # determine all folders
        folders = {}
        names = {}
        children = collections.defaultdict(list)

        for row in table.rows(page_start=page_start, page_limit=page_limit):
            folder = Folder(
                self.store,
                _benc(row[0].value),
                _check_mapiobj=False,
                cache=dict(zip(columns, row))
            )

            folders[_benc(row[0].value)] = folder, _benc(row[1].value)
            names[_benc(row[0].value)] = row[2].value
            children[_benc(row[1].value)].append((_benc(row[0].value), folder))

        # yield depth-first TODO improve server?
        def folders_recursive(fs, depth=0):
            for feid, f in sorted(fs, key=lambda data: names[data[0]]):
                f.depth = depth
                yield f
                for f in folders_recursive(children[feid], depth + 1):
                    yield f

        rootfolders = []
        for eid, (folder, parenteid) in folders.items():
            if parenteid not in folders:
                rootfolders.append((eid, folder))
        for f in folders_recursive(rootfolders):
            yield f

            # SHOW_SOFT_DELETES filters out subfolders of soft-deleted folders
            # TODO slow
            if self._content_flag == SHOW_SOFT_DELETES:
                for g in f.folders():
                    g.depth += f.depth + 1
                    yield g

    def create_folder(self, path=None, **kwargs):
        """Create folder with given (relative) path

        :param path: Folder path
        """
        if path is None:
            path = kwargs.get('path', kwargs.get('name'))
        if self.get_folder(path):
            raise DuplicateError("folder '%s' already exists" % path)
        folder = self.folder(path, create=True)
        for key, val in kwargs.items():
            setattr(folder, key, val)
        return folder

    def rules(self):
        """Return all folder :class:`rules <Rule>`."""
        rule_table = self.mapiobj.OpenProperty(PR_RULES_TABLE,
            IID_IExchangeModifyTable, MAPI_UNICODE, 0)
        table = Table(
            self.server,
            self.mapiobj,
            rule_table.GetTable(0),
            PR_RULES_TABLE,
        )
        for row in table.dict_rows():
            yield Rule(row, rule_table)

    def create_rule(self, name=None, restriction=None):
        """Create rule

        :param name: Rule name (optional)
        :param restriction: Rule :class:`restriction <Restriction>` (optional)
        """
        if name:
            name = str(name)

        propid = len(list(self.rules()))+1
        row = [
            SPropValue(PR_RULE_STATE, ST_ENABLED), # TODO configurable
            SPropValue(PR_RULE_PROVIDER_DATA,
                codecs.decode('010000000000000074da402772c2e440', 'hex')),
            SPropValue(PR_RULE_SEQUENCE, propid), # TODO max+1?
            SPropValue(0x6681001f, 'RuleOrganizer'), # TODO name
            SPropValue(PR_RULE_ID, propid), # TODO max+1
        ]
        if name:
            row.append(SPropValue(PR_RULE_NAME_W, name))
        if restriction:
            row.append(SPropValue(PR_RULE_CONDITION, restriction.mapiobj))

        table = self.mapiobj.OpenProperty(PR_RULES_TABLE,
            IID_IExchangeModifyTable, MAPI_UNICODE, 0)
        table.ModifyTable(0, [ROWENTRY(ROW_ADD, row)])

        mapirow = dict((p.ulPropTag, p.Value) for p in row)
        return Rule(mapirow, table)

    def dumps(self):
        """Serialize folder contents."""
        data = {}

        data['settings'] = self.settings_dumps()
        data['items'] = [item.dumps() for item in self]

        return _utils.pickle_dumps(data)

    def loads(self, data):
        """Deserialize folder contents

        :param data: Serialized data
        """
        data = _utils.pickle_loads(data)

        self.settings_loads(data['settings'])

        for data in data['items']:
            self.create_item(loads=data)

    def settings_dumps(self):
        """Serialize folder settings (rules, permissions)."""
        data = {}

        data['rules'] = self.rules_dumps()
        data['permissions'] = self.permissions_dumps()

        return _utils.pickle_dumps(data)

    def settings_loads(self, data):
        """Deserialize folder settings (rules, permissions).

        :param data: Serialized data
        """
        data = _utils.pickle_loads(data)

        self.rules_loads(data['rules'])
        self.permissions_loads(data['permissions'])

    def rules_dumps(self, stats=None):
        """Serialize folder rules."""
        server = self.server
        log = server.log

        ruledata = None
        with _log.log_exc(log, stats):
            try:
                ruledata = self.prop(PR_RULES_DATA).value
            except (MAPIErrorNotFound, NotFoundError):
                pass
            else:
                etxml = ElementTree.fromstring(ruledata)
                for actions in etxml.findall('./item/item/actions'):
                    for movecopy in actions.findall('.//moveCopy'):
                        try:
                            s = movecopy.findall('store')[0]
                            store = server.mapisession.OpenMsgStore(0,
                                _unbase64(s.text), None, 0)
                            entryid = \
                                HrGetOneProp(store, PR_STORE_ENTRYID).Value
                            store = server.store(entryid=_benc(entryid))
                            if store.public:
                                s.text = 'public'
                            else:
                                s.text = store.user.name \
                                    if store != self.store else ''
                            f = movecopy.findall('folder')[0]
                            path = store.folder(
                                entryid=_benc(_unbase64(f.text))).path
                            f.text = path
                        except Exception as e:
                            log.warning(
                                "could not resolve rule target: %s", str(e))
                try:
                    ruledata = ElementTree.tostring(etxml)
                except ElementTree.ParseError as e:
                    log.warning("Unable to backup rules for folder %s : %s", self.name,  str(e))
                    return None

        return _utils.pickle_dumps({
            b'data': ruledata
        })

    def rules_loads(self, data, stats=None):
        """Deserialize folder rules.

        :param data: Serialized data
        """
        server = self.server
        log = server.log

        with _log.log_exc(log, stats):
            data = _utils.pickle_loads(data)
            if isinstance(data, dict):
                data = data[b'data']
            if data:
                etxml = ElementTree.fromstring(data)
                for actions in etxml.findall('./item/item/actions'):
                    for movecopy in actions.findall('.//moveCopy'):
                        try:
                            s = movecopy.findall('store')[0]
                            if s.text == 'public':
                                store = server.public_store
                            else:
                                store = server.user(s.text).store \
                                    if s.text else self.store
                            s.text = _base64(_bdec(store.entryid))
                            f = movecopy.findall('folder')[0]
                            f.text = \
                                _base64(_bdec(store.folder(f.text).entryid))
                        except Exception as e:
                            log.warning(
                                "could not resolve rule target: %s", str(e))
                etxml = ElementTree.tostring(etxml)
                self[PR_RULES_DATA] = etxml

    # TODO associated, PR_CONTAINER_CONTENTS?
    def table(self, name, restriction=None, order=None, columns=None):
        return Table(
            self.server,
            self.mapiobj,
            self.mapiobj.OpenProperty(name, IID_IMAPITable, MAPI_UNICODE, 0),
            name,
            restriction=restriction,
            order=order,
            columns=columns,
        )

    def tables(self): # TODO associated, rules
        yield self.table(PR_CONTAINER_CONTENTS)
        yield self.table(PR_FOLDER_ASSOCIATED_CONTENTS)
        yield self.table(PR_CONTAINER_HIERARCHY)

    @property
    def state(self):
        """Folder state (for use with ICS synchronization)."""
        return _ics.state(self.mapiobj, self._content_flag == MAPI_ASSOCIATED)

    def sync(self, importer, state=None, log=None, max_changes=None,
            associated=False, window=None, begin=None, end=None, stats=None):
        """Perform synchronization against folder

        :param importer: importer instance with callbacks to process changes
        :param state: start from this state; if not given sync from scratch
        :log: logger instance to receive important warnings/errors
        """
        if state is None:
            state = _benc(8 * b'\0')
        importer.store = self.store
        return _ics.sync(self.store.server, self.mapiobj, importer, state,
            max_changes, associated, window=window, begin=begin, end=end,
            stats=stats)

    def sync_hierarchy(self, importer, state=None, stats=None):
        """Perform hierarchy synchronization against folder. In other words,
        receive changes to the folder and its subfolders (recursively).

        :param importer: importer instance with callbacks to process changes
        :param state: start from this state; if not given sync from scratch
        """
        if state is None:
            state = _benc(8 * b'\0')
        importer.store = self.store
        return _ics.sync_hierarchy(self.store.server, self.mapiobj, importer,
            state, stats)

    # TODO merge mbox/maildir functionality
    def readmbox(self, location):
        """Import all :class:`items <Item>` from mailbox (python mailbox
        module, mbox variant).

        :param location: mailbox location
        """
        for message in mailbox.mbox(location):
            _item.Item(self, eml=message.as_bytes(unixfrom=True), create=True)

    def mbox(self, location):
        """Export all :class:`items <Item>` to mailbox (using python mailbox
        module, mbox variant).

        :param location: mailbox location
        """
        mboxfile = mailbox.mbox(location)
        mboxfile.lock()
        for item in self.items():
            mboxfile.add(item.eml())
        mboxfile.unlock()

    def read_maildir(self, location):
        """Import all :class:`items <Item>` from mailbox (python mailbox
        module, maildir variant).

        :param location: mailbox location
        """
        for message in mailbox.MH(location):
            _item.Item(self, eml=message.as_bytes(unixfrom=True), create=True)

    def maildir(self, location='.'):
        """Export all :class:`items <Item>` to mailbox (using python mailbox
        module, maildir variant).

        :param location: mailbox location
        """
        destination = mailbox.MH(location + '/' + self.name)
        destination.lock()
        for item in self.items():
            destination.add(item.eml())
        destination.unlock()

    def read_ics(self, ics):
        """Import all :class:`items <Item>` from iCal calendar

        :param ics: the iCal data
        """
        icm = icalmapi.CreateICalToMapi(self.mapiobj, self.server.ab, False)
        icm.ParseICal(ics, 'utf-8', '', None, 0)
        for i in range(0, icm.GetItemCount()):
            mapiobj = self.mapiobj.CreateMessage(None, 0)
            icm.GetItem(i, 0, mapiobj)
            _utils._save(mapiobj)

    def ics(self, charset="UTF-8"):
        """Export all calendar items in the folder to iCal data"""
        mic = icalmapi.CreateMapiToICal(self.server.ab, charset)
        for item in self.items():
            if item.message_class.startswith('IPM.Appointment'):
                mic.AddMessage(item.mapiobj, "", 0)
        data = mic.Finalize(0)[1]
        return data

    @property
    def associated(self):
        """Folder containing hidden items."""
        return Folder(self.store, self.entryid, associated=True)

    @property
    def deleted(self):
        """Folder containing soft-deleted items."""
        return Folder(self.store, self.entryid, deleted=True)

    def permissions(self):
        """Return all :class:`permissions <Permission>` set for this folder."""
        return _utils.permissions(self)

    def permission(self, member, create=False):
        """Return :class:`permission <Permission>` for user or group set for
        this folder.

        :param member: user or group
        :param create: create new permission for this folder
        """
        return _utils.permission(self, member, create)

    def rights(self, member):
        """Determine rights on this folder for a given :class:`user <User>`
        or :class:`group <Group>`.

        :param member: User or group
        """
        # TODO admin-over-user, Store.rights (inheritance)
        if member == self.store.user:
            return NAME_RIGHT.keys()
        parent = self
        feids = set() # avoid loops
        while parent and parent.entryid not in feids:
            try:
                return parent.permission(member).rights
            except NotFoundError:
                if isinstance(member, _user.User):
                    for group in member.groups():
                        try:
                            return parent.permission(group).rights
                        except NotFoundError:
                            pass
                    # TODO company
            feids.add(parent.entryid)
            parent = parent.parent
        return []

    def search(self, text, recurse=False):
        searchfolder = self.store.create_searchfolder()
        searchfolder.search_start(self, text, recurse)
        searchfolder.search_wait()
        for item in searchfolder:
            yield item
        self.store.findroot.mapiobj.DeleteFolder(
            _bdec(searchfolder.entryid), 0, None, 0) # TODO store.findroot

    def search_start(self, folders, text, recurse=False):
        # specific restriction format, needed to reach indexer
        restriction = SOrRestriction([
            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE,
                PR_SUBJECT_W,
                SPropValue(PR_SUBJECT_W, str(text))),
            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE,
                PR_BODY_W,
                SPropValue(PR_BODY_W, str(text))),
            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE,
                PR_DISPLAY_TO_W,
                SPropValue(PR_DISPLAY_TO_W, str(text))),
            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE,
                PR_DISPLAY_NAME_W,
                SPropValue(PR_DISPLAY_NAME_W, str(text))),
            # TODO add all default fields..
            # BUT perform full-text search by default!
        ])
        if isinstance(folders, Folder):
            folders = [folders]

        search_flags = 0
        if recurse:
            search_flags = SEARCH_RECURSIVE
        self.mapiobj.SetSearchCriteria(restriction,
            [_bdec(f.entryid) for f in folders], search_flags)

    def search_wait(self):
        while True:
            (restrict, _, state) = self.mapiobj.GetSearchCriteria(0)
            if not state & SEARCH_REBUILD:
                break

    @property
    def archive_folder(self):
        """Archive :class:`Folder`."""
        # TODO merge namedprops stuff
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, MAPI_CREATE)
        PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)
        PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

        try:
            # support for multiple archives was a mistake, and is not and
            # _should not_ be used. so we just pick nr 0.
            arch_storeid = \
                HrGetOneProp(self.mapiobj, PROP_STORE_ENTRYIDS).Value[0]
            arch_folderid = \
                HrGetOneProp(self.mapiobj, PROP_ITEM_ENTRYIDS).Value[0]
        except MAPIErrorNotFound:
            return

        archive_store = self.server._store2(arch_storeid)
        return _store.Store(mapiobj=archive_store, server=self.server).folder(
            entryid=_benc(arch_folderid))

    @property
    def primary_store(self):
        """Primary :class:`store <Store>` (for archive folders)."""
        # TODO merge namedprops stuff
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, MAPI_CREATE)
        PROP_REF_STORE_ENTRYID = CHANGE_PROP_TYPE(ids[3], PT_BINARY)
        try:
            entryid = HrGetOneProp(self.mapiobj, PROP_REF_STORE_ENTRYID).Value
        except MAPIErrorNotFound:
            return

        return _store.Store(entryid=_benc(entryid), server=self.server)

    @property
    def primary_folder(self):
        """Primary :class:`folder <Folder>` (for archive folders)."""
        # TODO merge namedprops stuff
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, MAPI_CREATE)
        PROP_REF_ITEM_ENTRYID = CHANGE_PROP_TYPE(ids[4], PT_BINARY)
        try:
            entryid = HrGetOneProp(self.mapiobj, PROP_REF_ITEM_ENTRYID).Value
        except MAPIErrorNotFound:
            return

        if self.primary_store:
            try:
                return self.primary_store.folder(entryid=_benc(entryid))
            except NotFoundError:
                pass

    @property
    def created(self):
        """Folder creation date."""
        return self._get_fast(PR_CREATION_TIME)

    @property
    def last_modified(self):
        """Folder last modification date."""
        return self._get_fast(PR_LAST_MODIFICATION_TIME)

    def subscribe(self, sink, **kwargs): # TODO document args
        """Subscribe to folder notifications

        :param sink: Sink instance with callbacks to process notifications
        :param object_types: Tracked objects (*item*, *folder*)
        :param folder_types: Tracked folders (*mail*, *contacts*, *calendar*)
        :param event_types: Event types (*created*, *updated*, *deleted*)
        """
        _notification.subscribe(self.store, self, sink, **kwargs)

    def unsubscribe(self, sink):
        """Unsubscribe from folder notifications

        :param sink: Previously subscribed sink instance
        """
        _notification.unsubscribe(self.store, sink)

    def event(self, eventid):
        eventid = _bdec(eventid)
        leid = _utils.unpack_short(eventid, 1)
        item = self.item(_benc(eventid[3:3 + leid]))
        if eventid[0] == 0:
            return item
        else:
            return item.occurrence(_benc(eventid[1:]))

    @property
    def type_(self):
        """Folder type (mail, contacts, calendar or just folder)."""
        if self.container_class in (None, 'IPF.Note'):
            return 'mail'
        elif self.container_class == 'IPF.Contact':
            return 'contacts'
        elif self.container_class == 'IPF.Appointment':
            return 'calendar'
        else:
            return 'folder'

    def permissions_dumps(self, **kwargs):
        """Serialize permissions."""
        return _permissions_dumps(self, **kwargs)

    def permissions_loads(self, data, **kwargs):
        """Deserialize permissions.

        :param data: Serialized data
        """
        return _permissions_loads(self, data, **kwargs)

    def __eq__(self, f): # TODO check same store?
        if isinstance(f, Folder):
            return self._entryid == f._entryid
        return False

    def __ne__(self, f):
        return not self == f

    def __iter__(self):
        self._iter = self.items()
        return self

    def __next__(self):
        if not self._iter:
            self._iter = self.items()
        return next(self._iter)

    def next(self):
        return self.__next__()

    def __unicode__(self): # TODO associated?
        try: # folder deleted via ICS
            name = self.name
        except AttributeError:
            name = ''

        return 'Folder(%s)' % name
