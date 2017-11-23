"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import collections
import mailbox
import sys
import time

import icalmapi

from MAPI import (
    MAPI_MODIFY, MAPI_ASSOCIATED, KEEP_OPEN_READWRITE,
    RELOP_GT, RELOP_LT, RELOP_EQ,
    DEL_ASSOCIATED, DEL_FOLDERS, DEL_MESSAGES,
    BOOKMARK_BEGINNING, ROW_REMOVE, MESSAGE_MOVE, FOLDER_MOVE,
    FOLDER_GENERIC, MAPI_UNICODE, FL_SUBSTRING, FL_IGNORECASE,
    SEARCH_RECURSIVE, SEARCH_REBUILD, PT_MV_BINARY, PT_BINARY
)
from MAPI.Tags import (
    PR_ENTRYID, IID_IMAPIFolder, SHOW_SOFT_DELETES, PR_SOURCE_KEY,
    PR_PARENT_ENTRYID, PR_EC_HIERARCHYID, PR_FOLDER_CHILD_COUNT,
    PR_DISPLAY_NAME_W, PR_CONTAINER_CLASS_W, PR_CONTENT_UNREAD,
    PR_MESSAGE_DELIVERY_TIME, MNID_ID, PT_SYSTIME, PT_BOOLEAN,
    DELETE_HARD_DELETE, PR_MESSAGE_SIZE, PR_ACL_TABLE,
    PR_MEMBER_ID, PR_RULES_TABLE, IID_IExchangeModifyTable,
    IID_IMAPITable, PR_CONTAINER_CONTENTS, PR_RULE_STATE,
    PR_FOLDER_ASSOCIATED_CONTENTS, PR_CONTAINER_HIERARCHY,
    PR_SUBJECT_W, PR_BODY_W, PR_DISPLAY_TO_W, PR_CREATION_TIME,
    CONVENIENT_DEPTH, PR_DEPTH, PR_CONTENT_COUNT, PR_ASSOC_CONTENT_COUNT,
    PR_DELETED_MSG_COUNT, PR_LAST_MODIFICATION_TIME,
    PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID,
)
from MAPI.Defs import (
    HrGetOneProp, CHANGE_PROP_TYPE
)
from MAPI.Struct import (
    MAPIErrorNoAccess, MAPIErrorNotFound, MAPIErrorNoSupport,
    MAPIErrorInvalidEntryid, MAPIErrorCollision, SPropValue,
    MAPINAMEID, SOrRestriction, SAndRestriction, SPropertyRestriction,
    SContentRestriction, ROWENTRY
)
from MAPI.Time import unixtime

from .properties import Properties
from .permission import Permission
from .restriction import Restriction
from .rule import Rule
from .table import Table
from .property_ import Property
from .defs import (
    PSETID_Appointment, UNESCAPED_SLASH_RE,
    ENGLISH_FOLDER_MAP, NAME_RIGHT, NAMED_PROPS_ARCHIVER
)
from .errors import NotFoundError, Error, _DeprecationWarning

from .compat import hex as _hex, unhex as _unhex, fake_unicode as _unicode

if sys.hexversion >= 0x03000000:
    try:
        from . import user as _user
    except ImportError:
        _user = sys.modules[__package__+'.user']
    try:
        from . import store as _store
    except ImportError:
        _store = sys.modules[__package__+'.store']
    try:
        from . import item as _item
    except ImportError:
        _item = sys.modules[__package__+'.item']
    try:
        from . import utils as _utils
    except ImportError:
        _utils = sys.modules[__package__+'.utils']
    try:
        from . import ics as _ics
    except ImportError:
        _ics = sys.modules[__package__+'.ics']
else:
    import user as _user
    import store as _store
    import item as _item
    import utils as _utils
    import ics as _ics

# TODO generalize, autogenerate basic item getters/setters?
PROPMAP = {
    'subject': PR_SUBJECT_W,
    'received':  PR_MESSAGE_DELIVERY_TIME,
}

class Folder(Properties):
    """Folder class"""

    def __init__(self, store=None, entryid=None, associated=False,
        deleted=False, mapiobj=None, _check_mapiobj=True, cache={}
    ):
        if store:
            self.store = store
            self.server = store.server
        # XXX else, determine store dynamically?
        if mapiobj:
            self._mapiobj = mapiobj
            self._entryid = HrGetOneProp(self.mapiobj, PR_ENTRYID).Value
        elif entryid:
            self._entryid = _unhex(entryid)

        self.content_flag = MAPI_ASSOCIATED if associated else (SHOW_SOFT_DELETES if deleted else 0)
        self._sourcekey = None
        self._mapiobj = None

        if _check_mapiobj: # raise error for specific key
            self.mapiobj

        self._cache = cache
        self._iter = None

    @property
    def mapiobj(self):
        if self._mapiobj:
            return self._mapiobj

        try:
            self._mapiobj = self.store.mapiobj.OpenEntry(self._entryid, IID_IMAPIFolder, MAPI_MODIFY)
        except (MAPIErrorNotFound, MAPIErrorInvalidEntryid):
            try:
                self._mapiobj = self.store.mapiobj.OpenEntry(self._entryid, IID_IMAPIFolder, MAPI_MODIFY | SHOW_SOFT_DELETES)
            except (MAPIErrorNotFound, MAPIErrorInvalidEntryid):
                raise NotFoundError("cannot open folder with entryid '%s'" % _hex(self._entryid)) # XXX check too late??
        except MAPIErrorNoAccess: # XXX XXX
            self._mapiobj = self.store.mapiobj.OpenEntry(self._entryid, IID_IMAPIFolder, 0)

        return self._mapiobj

    @mapiobj.setter
    def mapiobj(self, mapiobj):
        self._mapiobj = mapiobj

    @property
    def entryid(self):
        """ Folder entryid """

        return _hex(self._entryid)

    @property
    def sourcekey(self):
        if not self._sourcekey:
            self._sourcekey = _hex(HrGetOneProp(self.mapiobj, PR_SOURCE_KEY).Value)
        return self._sourcekey

    @property
    def parent(self):
        """:class:`Parent <Folder>` folder"""
        parent_eid = _hex(self._get_fast(PR_PARENT_ENTRYID))
        if parent_eid != self.store.root.entryid: # root parent is root
            return Folder(self.store, parent_eid, _check_mapiobj=False)

    @property
    def hierarchyid(self):
        return self.prop(PR_EC_HIERARCHYID).value

    @property
    def folderid(self): # XXX deprecated (8.4.x) to be removed
        warnings.warn("Property 'folderid' is deprecated and will be removed.", _DeprecationWarning)
        return self.hierarchyid

    @property
    def subfolder_count(self):
        """Number of direct subfolders"""

        return self.prop(PR_FOLDER_CHILD_COUNT).value

    @property
    def name(self):
        """Folder name"""

        name = self._get_fast(PR_DISPLAY_NAME_W)

        if name is not None:
            return name.replace('/', '\\/')
        else:
            if self.entryid == self.store.root.entryid: # Root folder's PR_DISPLAY_NAME_W is never set
                return u'ROOT'
            else:
                return u''

    @property
    def path(self):
        """Folder path"""
        names = []
        parent = self
        subtree_entryid = self.store.subtree.entryid
        root_entryid = self.store.root.entryid
        # parent entryids coming from a table are not converted like normal
        # parent entryids for certains public store folders (via getprops,
        # or store PR_IPM_SUBTREE_ENTRYID), so we match with this property,
        # which is also not converted
        pub_entryid = _hex(self.store.get(PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID, ''))

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
        self.mapiobj.SetProps([SPropValue(PR_DISPLAY_NAME_W, _unicode(name))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def container_class(self):
        """
        Property which describes the type of items a folder holds, possible values
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
        self.mapiobj.SetProps([SPropValue(PR_CONTAINER_CLASS_W, _unicode(value))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def unread(self):
        """ Number of unread items """

        return self.prop(PR_CONTENT_UNREAD).value

    def item(self, entryid=None, sourcekey=None):
        """ Return :class:`Item` with given entryid or sourcekey

        :param entryid: item entryid
        :param sourcekey: item sourcekey
        """

        # resolve sourcekey to entryid
        if sourcekey is not None:
            restriction = SPropertyRestriction(RELOP_EQ, PR_SOURCE_KEY, SPropValue(PR_SOURCE_KEY, _unhex(sourcekey)))
            table = self.mapiobj.GetContentsTable(0)
            table.SetColumns([PR_ENTRYID, PR_SOURCE_KEY], 0)
            table.Restrict(restriction, 0)
            rows = list(table.QueryRows(-1, 0))
            if not rows:
                raise NotFoundError("no item with sourcekey '%s'" % sourcekey)
            entryid = _hex(rows[0][0].Value)

        # open message with entryid
        try:
            mapiobj = _utils.openentry_raw(self.store.mapiobj, _unhex(entryid), self.content_flag)
        except MAPIErrorNotFound:
            raise NotFoundError("no item with entryid '%s'" % entryid)

        item = _item.Item(self, mapiobj=mapiobj)
        return item

    def items(self, restriction=None, page_start=None, page_limit=None,
            order=None,
        ):
        """Return all :class:`items <Item>` in folder, reverse sorted on
        received date.

        :param restriction: apply :class:`restriction <Restriction>`
        :param order: order by (limited set of) attributes, e.g. 'subject',
            '-subject' (reverse order), or ('subject', '-received').
        :param page_start: skip this many items from the start
        :param page_limit: return up to this many items
        """

        # TODO determine preload columns dynamically, based on usage pattern
        columns = [
            PR_ENTRYID,
            PR_MESSAGE_DELIVERY_TIME,
            PR_SUBJECT_W, # watch out: table unicode data is max 255 chars
            PR_LAST_MODIFICATION_TIME
        ]

        try:
            table = Table(
                self.server,
                self.mapiobj.GetContentsTable(self.content_flag),
                PR_CONTAINER_CONTENTS,
                columns=columns,
                restriction=restriction
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
                content_flag=self.content_flag,
                cache=dict(zip(columns, row))
            )
            yield item

    def occurrences(self, start=None, end=None):
        if start and end:
            startstamp = time.mktime(start.timetuple())
            endstamp = time.mktime(end.timetuple())

            # XXX use shortcuts and default type (database) to avoid MAPI snake wrestling
            NAMED_PROPS = [MAPINAMEID(PSETID_Appointment, MNID_ID, x) for x in (33293, 33294, 33315)]
            ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS, 0)
            startdate = ids[0] | PT_SYSTIME
            enddate = ids[1] | PT_SYSTIME
            recurring = ids[2] | PT_BOOLEAN

            # only look at non-recurring items which overlap and all recurring items
            restriction = SOrRestriction([
                SAndRestriction([
                    SPropertyRestriction(RELOP_GT, enddate, SPropValue(enddate, unixtime(startstamp))),
                    SPropertyRestriction(RELOP_LT, startdate, SPropValue(startdate, unixtime(endstamp))),
                ]),
                SAndRestriction([
                    SPropertyRestriction(RELOP_EQ, recurring, SPropValue(recurring, True))
                ])
            ])

            table = Table(
                self.server, self.mapiobj.GetContentsTable(0),
                PR_CONTAINER_CONTENTS, columns=[PR_ENTRYID]
            )
            table.mapitable.Restrict(restriction, 0)
            for row in table.rows():
                entryid = _hex(row[0].value)
                for occurrence in self.item(entryid).occurrences(start, end):
                    yield occurrence

        else:
            for item in self:
                for occurrence in item.occurrences(start, end):
                    yield occurrence

    def create_item(self, eml=None, ics=None, vcf=None, load=None, loads=None, attachments=True, save=True, **kwargs): # XXX associated
        item = _item.Item(self, eml=eml, ics=ics, vcf=vcf, load=load, loads=loads, attachments=attachments, create=True, save=save)
        for key, val in kwargs.items():
            setattr(item, key, val)
        return item

    # XXX: always hard delete or but we should also provide 'softdelete' which moves the item to the wastebasket
    def empty(self, recurse=True, associated=False):
        """ Delete folder contents

        :param recurse: delete subfolders
        :param associated: delete associated contents
        """

        if recurse:
            flags = DELETE_HARD_DELETE
            if associated:
                flags |= DEL_ASSOCIATED
            self.mapiobj.EmptyFolder(0, None, flags)
        else:
            self.delete(self.items()) # XXX look at associated flag! probably also quite slow

    @property
    def size(self): # XXX bit slow perhaps? :P
        """ Folder size """

        try:
            table = Table(
                self.server,
                self.mapiobj.GetContentsTable(self.content_flag),
                PR_CONTAINER_CONTENTS, columns=[PR_MESSAGE_SIZE]
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
        """ Number of items in folder """

        folder = self.store.folder(entryid=self.entryid) # reopen to get up-to-date counters.. (XXX use flags when opening folder?)

        if self.content_flag == 0:
            proptag = PR_CONTENT_COUNT
        elif self.content_flag == MAPI_ASSOCIATED:
            proptag = PR_ASSOC_CONTENT_COUNT
        elif self.content_flag == SHOW_SOFT_DELETES:
            proptag = PR_DELETED_MSG_COUNT
        return folder.get(proptag, 0)

    def recount(self):
        self.server.sa.ResetFolderCount(_unhex(self.entryid))

    def _get_entryids(self, items):
        item_entryids = [_unhex(item.entryid) for item in items if isinstance(item, _item.Item)]
        folder_entryids = [_unhex(item.entryid) for item in items if isinstance(item, Folder)]
        perms = [item for item in items if isinstance(item, Permission)]
        props = [item for item in items if isinstance(item, Property)]
        return item_entryids, folder_entryids, perms, props

    def delete(self, objects, soft=False): # XXX associated
        """Delete items, subfolders, properties or permissions from folder.

        :param objects: The object(s) to delete
        :param soft: In case of items or folders, are they soft-deleted
        """
        objects = _utils.arg_objects(objects, (_item.Item, Folder, Permission, Property), 'Folder.delete')
        item_entryids, folder_entryids, perms, props = self._get_entryids(objects)
        if item_entryids:
            if soft:
                self.mapiobj.DeleteMessages(item_entryids, 0, None, 0)
            else:
                self.mapiobj.DeleteMessages(item_entryids, 0, None, DELETE_HARD_DELETE)
        for entryid in folder_entryids:
            if soft:
                self.mapiobj.DeleteFolder(entryid, 0, None, DEL_FOLDERS | DEL_MESSAGES)
            else:
                self.mapiobj.DeleteFolder(entryid, 0, None, DEL_FOLDERS | DEL_MESSAGES | DELETE_HARD_DELETE)
        for perm in perms:
            acl_table = self.mapiobj.OpenProperty(PR_ACL_TABLE, IID_IExchangeModifyTable, 0, 0)
            acl_table.ModifyTable(0, [ROWENTRY(ROW_REMOVE, [SPropValue(PR_MEMBER_ID, perm.mapirow[PR_MEMBER_ID])])])
        if props:
            self.mapiobj.DeleteProps([prop.proptag for prop in props])

    def copy(self, objects, folder, _delete=False):
        """Copy items or subfolders to folder.

        :param objects: The items or subfolders to copy
        :param folder: The target folder
        """
        objects = _utils.arg_objects(objects, (_item.Item, Folder), 'Folder.copy')
        item_entryids, folder_entryids, _, _ = self._get_entryids(objects) # XXX copy/move perms?? XXX error for perms/props
        if item_entryids:
            self.mapiobj.CopyMessages(item_entryids, IID_IMAPIFolder, folder.mapiobj, 0, None, (MESSAGE_MOVE if _delete else 0))
        for entryid in folder_entryids:
            self.mapiobj.CopyFolder(entryid, IID_IMAPIFolder, folder.mapiobj, None, 0, None, (FOLDER_MOVE if _delete else 0))

    def move(self, objects, folder):
        """Move items or subfolders to folder

        :param objects: The items or subfolders to move
        :param folder: The target folder
        """

        self.copy(objects, folder, _delete=True)

    def folder(self, path=None, entryid=None, recurse=False, create=False):
        """ Return :class:`Folder` with given path or entryid

            :param key: name, path or entryid
        """

        if entryid is not None:
            try:
                return Folder(self.store, entryid)
            except (MAPIErrorInvalidEntryid, MAPIErrorNotFound, TypeError):
                raise NotFoundError('cannot open folder with entryid "%s"' % entryid)

        if '/' in path.replace('\\/', ''): # XXX MAPI folders may contain '/' (and '\') in their names..
            subfolder = self
            for name in UNESCAPED_SLASH_RE.split(path):
                subfolder = subfolder.folder(name, create=create, recurse=False)
            return subfolder

        if self == self.store.subtree and path in ENGLISH_FOLDER_MAP: # XXX depth==0?
            path = getattr(self.store, ENGLISH_FOLDER_MAP[path]).name

        if create:
            name = path.replace('\\/', '/')
            try:
                mapifolder = self.mapiobj.CreateFolder(FOLDER_GENERIC, _unicode(name), u'', None, MAPI_UNICODE)
                return Folder(self.store, _hex(HrGetOneProp(mapifolder, PR_ENTRYID).Value))
            except MAPIErrorCollision:
                pass

        name = path.replace('\\/', '/')
        restriction = Restriction(SPropertyRestriction(RELOP_EQ, PR_DISPLAY_NAME_W, SPropValue(PR_DISPLAY_NAME_W, _unicode(name))))

        folders = list(self.folders(recurse=recurse, restriction=restriction))
        if len(folders) == 0:
            raise NotFoundError("no such folder: '%s'" % path)

        return folders[0]

    def get_folder(self, path=None, entryid=None):
        """ Return :class:`folder <Folder>` with given name/entryid or *None* if not found """

        try:
            return self.folder(path, entryid=entryid)
        except NotFoundError:
            pass

    def folders(self, recurse=True, restriction=None):
        """ Return all :class:`sub-folders <Folder>` in folder

        :param recurse: include all sub-folders
        """

        columns = [
            PR_ENTRYID,
            PR_PARENT_ENTRYID,
            PR_DISPLAY_NAME_W,
            PR_LAST_MODIFICATION_TIME
        ]

        flags = MAPI_UNICODE | self.content_flag
        if recurse:
            flags |= CONVENIENT_DEPTH

        try:
            mapitable = self.mapiobj.GetHierarchyTable(flags)

            table = Table(
                self.server,
                mapitable,
                PR_CONTAINER_HIERARCHY,
                columns=columns,
                restriction=restriction
            )
        except MAPIErrorNoSupport: # XXX webapp search folder?
            return

        # determine all folders
        folders = {}
        names = {}
        children = collections.defaultdict(list)

        for row in table.rows():
            folder = Folder(
                self.store,
                _hex(row[0].value),
                _check_mapiobj=False,
                cache=dict(zip(columns, row))
            )

            folders[_hex(row[0].value)] = folder, _hex(row[1].value)
            names[_hex(row[0].value)] = row[2].value
            children[_hex(row[1].value)].append((_hex(row[0].value), folder))

        # yield depth-first XXX improve server?
        def folders_recursive(fs, depth=0):
            for feid, f in sorted(fs, key=lambda data: names[data[0]]):
                f.depth = depth
                yield f
                for f in folders_recursive(children[feid], depth+1):
                    yield f

        rootfolders = []
        for eid, (folder, parenteid) in folders.items():
            if parenteid not in folders:
                rootfolders.append((eid, folder))
        for f in folders_recursive(rootfolders):
            yield f

            # SHOW_SOFT_DELETES filters out subfolders of soft-deleted folders.. XXX slow
            if self.content_flag == SHOW_SOFT_DELETES:
                for g in f.folders():
                    g.depth += f.depth+1
                    yield g

    def create_folder(self, path, **kwargs):
        folder = self.folder(path, create=True)
        for key, val in kwargs.items():
            setattr(folder, key, val)
        return folder

    def rules(self):
        rule_table = self.mapiobj.OpenProperty(PR_RULES_TABLE, IID_IExchangeModifyTable, MAPI_UNICODE, 0)
        table = Table(self.server, rule_table.GetTable(0), PR_RULES_TABLE)
        for row in table.dict_rows():
            yield Rule(row)

    def table(self, name, restriction=None, order=None, columns=None): # XXX associated, PR_CONTAINER_CONTENTS?
        return Table(self.server, self.mapiobj.OpenProperty(name, IID_IMAPITable, MAPI_UNICODE, 0), name, restriction=restriction, order=order, columns=columns)

    def tables(self): # XXX associated, rules
        yield self.table(PR_CONTAINER_CONTENTS)
        yield self.table(PR_FOLDER_ASSOCIATED_CONTENTS)
        yield self.table(PR_CONTAINER_HIERARCHY)

    @property
    def state(self):
        """ Current folder state """

        return _ics.state(self.mapiobj, self.content_flag == MAPI_ASSOCIATED)

    def sync(self, importer, state=None, log=None, max_changes=None, associated=False, window=None, begin=None, end=None, stats=None):
        """ Perform synchronization against folder

        :param importer: importer instance with callbacks to process changes
        :param state: start from this state; if not given sync from scratch
        :log: logger instance to receive important warnings/errors
        """

        if state is None:
            state = _hex(8 * b'\0')
        importer.store = self.store
        return _ics.sync(self.store.server, self.mapiobj, importer, state, log, max_changes, associated, window=window, begin=begin, end=end, stats=stats)

    def hierarchy_sync(self, importer, state=None):
        if state is None:
            state = _hex(8 * b'\0')
        importer.store = self.store
        return _ics.hierarchy_sync(self.store.server, self.mapiobj, importer, state)

    def readmbox(self, location):
        for message in mailbox.mbox(location):
            if sys.hexversion >= 0x03000000:
                _item.Item(self, eml=message.as_bytes(unixfrom=True), create=True)
            else:
                _item.Item(self, eml=message.as_string(unixfrom=True), create=True)

    def mbox(self, location): # FIXME: inconsistent with maildir()
        mboxfile = mailbox.mbox(location)
        mboxfile.lock()
        for item in self.items():
            mboxfile.add(item.eml())
        mboxfile.unlock()

    def maildir(self, location='.'):
        destination = mailbox.MH(location + '/' + self.name)
        destination.lock()
        for item in self.items():
            destination.add(item.eml())
        destination.unlock()

    def read_maildir(self, location):
        for message in mailbox.MH(location):
            if sys.hexversion >= 0x03000000:
                _item.Item(self, eml=message.as_bytes(unixfrom=True), create=True)
            else:
                _item.Item(self, eml=message.as_string(unixfrom=True), create=True)

    def read_ics(self, ics):
        """Import a complete ics calendar into the current folder

        :param ics: the ics file to import
        """
        icm = icalmapi.CreateICalToMapi(self.mapiobj, self.server.ab, False)
        icm.ParseICal(ics, 'utf-8', '', None, 0)
        for i in range(0, icm.GetItemCount()):
            mapiobj = self.mapiobj.CreateMessage(None, 0)
            icm.GetItem(i, 0, mapiobj)
            mapiobj.SaveChanges(0)

    def ics(self, charset="UTF-8"):
        """Export all calendar items in the folder to an ics file

        :return: ics calendar string
        """
        mic = icalmapi.CreateMapiToICal(self.server.ab, charset)
        for item in self.items():
            if item.message_class.startswith('IPM.Appointment'):
                mic.AddMessage(item.mapiobj, "", 0)
        data = mic.Finalize(0)[1]
        return data

    @property
    def associated(self):
        """ Associated folder containing hidden items """

        return Folder(self.store, self.entryid, associated=True)

    @property
    def deleted(self):
        return Folder(self.store, self.entryid, deleted=True)

    def permissions(self):
        """Return all :class:`permissions <Permission>` set for this folder."""
        return _utils.permissions(self)

    def permission(self, member, create=False):
        """Return :class:`permission <Permission>` for user or group set for this folder.

        :param member: user or group
        :param create: create new permission for this folder
        """
        return _utils.permission(self, member, create)

    def rights(self, member):
        if member == self.store.user: # XXX admin-over-user, Store.rights (inheritance)
            return NAME_RIGHT.keys()
        parent = self
        feids = set() # avoid loops
        while parent.entryid not in feids:
            try:
                return parent.permission(member).rights
            except NotFoundError:
                if isinstance(member, _user.User):
                    for group in member.groups():
                        try:
                            return parent.permission(group).rights
                        except NotFoundError:
                            pass
                    # XXX company
            feids.add(parent.entryid)
            parent = parent.parent
        return []

    def search(self, text, recurse=False):
        searchfolder = self.store.create_searchfolder()
        searchfolder.search_start(self, text, recurse)
        searchfolder.search_wait()
        for item in searchfolder:
            yield item
        self.store.findroot.mapiobj.DeleteFolder(_unhex(searchfolder.entryid), 0, None, 0) # XXX store.findroot

    def search_start(self, folders, text, recurse=False):
        # specific restriction format, needed to reach indexer
        restriction = SOrRestriction([
            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT_W, SPropValue(PR_SUBJECT_W, _unicode(text))),
            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_BODY_W, SPropValue(PR_BODY_W, _unicode(text))),
            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_DISPLAY_TO_W, SPropValue(PR_DISPLAY_TO_W, _unicode(text))),
            # XXX add all default fields.. BUT perform full-text search by default!
        ])
        if isinstance(folders, Folder):
            folders = [folders]

        search_flags = 0
        if recurse:
            search_flags = SEARCH_RECURSIVE
        self.mapiobj.SetSearchCriteria(restriction, [_unhex(f.entryid) for f in folders], search_flags)

    def search_wait(self):
        while True:
            (restrict, list, state) = self.mapiobj.GetSearchCriteria(0)
            if not state & SEARCH_REBUILD:
                break

    @property
    def archive_folder(self):
        """ Archive :class:`Folder` """

        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)
        PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

        try:
            # support for multiple archives was a mistake, and is not and _should not_ be used. so we just pick nr 0.
            arch_storeid = HrGetOneProp(self.mapiobj, PROP_STORE_ENTRYIDS).Value[0]
            arch_folderid = HrGetOneProp(self.mapiobj, PROP_ITEM_ENTRYIDS).Value[0]
        except MAPIErrorNotFound:
            return

        archive_store = self.server._store2(arch_storeid)
        return _store.Store(mapiobj=archive_store, server=self.server).folder(entryid=_hex(arch_folderid))

    @property
    def primary_store(self):
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_REF_STORE_ENTRYID = CHANGE_PROP_TYPE(ids[3], PT_BINARY)
        try:
            entryid = HrGetOneProp(self.mapiobj, PROP_REF_STORE_ENTRYID).Value
        except MAPIErrorNotFound:
            return

        return _store.Store(entryid=_hex(entryid), server=self.server)

    @property
    def primary_folder(self):
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_REF_ITEM_ENTRYID = CHANGE_PROP_TYPE(ids[4], PT_BINARY)
        entryid = HrGetOneProp(self.mapiobj, PROP_REF_ITEM_ENTRYID).Value

        if self.primary_store:
            try:
                return self.primary_store.folder(entryid=_hex(entryid))
            except NotFoundError:
                pass

    @property
    def created(self):
        try:
            return self.prop(PR_CREATION_TIME).value
        except NotFoundError:
            pass

    @property
    def last_modified(self):
        return self._get_fast(PR_LAST_MODIFICATION_TIME)

    def __eq__(self, f): # XXX check same store?
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

    def __unicode__(self): # XXX associated?
        return u'Folder(%s)' % self.name
