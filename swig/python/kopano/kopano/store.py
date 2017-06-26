"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import codecs
import uuid
import sys

from MAPI import (
    MAPI_UNICODE, MAPI_MODIFY, KEEP_OPEN_READWRITE, PT_MV_BINARY,
    RELOP_EQ, TBL_BATCH, ECSTORE_TYPE_PUBLIC, FOLDER_SEARCH,
    MAPI_ASSOCIATED, MAPI_DEFERRED_ERRORS
)
from MAPI.Defs import (
    bin2hex, HrGetOneProp, CHANGE_PROP_TYPE, PpropFindProp
)
from MAPI.Tags import (
    PR_ENTRYID, PR_MDB_PROVIDER, ZARAFA_STORE_PUBLIC_GUID,
    PR_STORE_RECORD_KEY, PR_EC_HIERARCHYID, PR_REM_ONLINE_ENTRYID,
    PR_IPM_PUBLIC_FOLDERS_ENTRYID, PR_IPM_SUBTREE_ENTRYID,
    PR_FINDER_ENTRYID, PR_ADDITIONAL_REN_ENTRYIDS,
    PR_IPM_APPOINTMENT_ENTRYID, PR_IPM_OUTBOX_ENTRYID,
    PR_IPM_CONTACT_ENTRYID, PR_COMMON_VIEWS_ENTRYID,
    PR_IPM_DRAFTS_ENTRYID, PR_IPM_WASTEBASKET_ENTRYID,
    PR_IPM_JOURNAL_ENTRYID, PR_IPM_NOTE_ENTRYID,
    PR_IPM_SENTMAIL_ENTRYID, PR_IPM_TASK_ENTRYID,
    PR_IPM_OL2007_ENTRYIDS, PR_MESSAGE_SIZE_EXTENDED,
    PR_LAST_LOGON_TIME, PR_LAST_LOGOFF_TIME,
    PR_MAILBOX_OWNER_ENTRYID, PR_EC_STOREGUID, PR_EC_STORETYPE,
    PR_EC_USERNAME_W, PR_EC_COMPANY_NAME_W, PR_MESSAGE_CLASS,
    PR_SUBJECT, PR_WLINK_FLAGS, PR_WLINK_ORDINAL,
    PR_WLINK_STORE_ENTRYID, PR_WLINK_TYPE, PR_WLINK_ENTRYID,
    PR_EXTENDED_FOLDER_FLAGS, PR_WB_SF_ID, PR_FREEBUSY_ENTRYIDS,
    PR_SCHDINFO_DELEGATE_ENTRYIDS, PR_SCHDINFO_DELEGATE_NAMES,
    PR_DELEGATE_FLAGS, PR_MAPPING_SIGNATURE,
)
from MAPI.Struct import (
    SPropertyRestriction, SPropValue,
    MAPIErrorNotFound, MAPIErrorInvalidEntryid
)

from .defs import (
    RSF_PID_SUGGESTED_CONTACTS, RSF_PID_RSS_SUBSCRIPTION,
    NAMED_PROPS_ARCHIVER
)

from .errors import NotFoundError
from .base import Base
from .autoaccept import AutoAccept
from .outofoffice import OutOfOffice
from .prop import Property
from .delegation import Delegation
from .freebusy import FreeBusy

from .compat import (
    hex as _hex, unhex as _unhex, decode as _decode, encode as _encode,
    repr as _repr
)

if sys.hexversion >= 0x03000000:
    from . import server as _server
    from . import user as _user
    from . import folder as _folder
    from . import item as _item
    from . import utils as _utils
else:
    import server as _server
    import user as _user
    import folder as _folder
    import item as _item
    import utils as _utils

class Store(Base):
    """Store class"""

    def __init__(self, guid=None, entryid=None, mapiobj=None, server=None):
        self.server = server or _server.Server()

        if guid:
            mapiobj = self.server._store(guid)
        elif entryid:
            try:
                mapiobj = self.server._store2(_unhex(entryid))
            except MAPIErrorNotFound:
                raise NotFoundError("cannot open store with entryid '%s'" % entryid)

        self.mapiobj = mapiobj
        # XXX: fails if store is orphaned and guid is given..
        self._root = self.mapiobj.OpenEntry(None, None, 0)

    @property
    def entryid(self):
        """ Store entryid """

        return bin2hex(self.prop(PR_ENTRYID).value)

    @property
    def public(self):
        return self.prop(PR_MDB_PROVIDER).mapiobj.Value == ZARAFA_STORE_PUBLIC_GUID

    @property
    def guid(self):
        """ Store GUID """

        return bin2hex(self.prop(PR_STORE_RECORD_KEY).value)

    @property
    def name(self):
        """ User name ('public' for public store), or GUID """

        user = self.user
        if user:
            return user.name
        elif self.public:
            return 'public'
        else:
            return self.guid

    @property
    def hierarchyid(self):
        return self.prop(PR_EC_HIERARCHYID).value

    @property
    def root(self):
        """ :class:`Folder` designated as store root """

        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self._root, PR_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def subtree(self):
        """ :class:`Folder` designated as IPM.Subtree """

        try:
            if self.public:
                ipmsubtreeid = HrGetOneProp(self.mapiobj, PR_IPM_PUBLIC_FOLDERS_ENTRYID).Value
            else:
                ipmsubtreeid = HrGetOneProp(self.mapiobj, PR_IPM_SUBTREE_ENTRYID).Value

            return _folder.Folder(self, _hex(ipmsubtreeid))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def findroot(self):
        """ :class:`Folder` designated as search-results root """

        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self.mapiobj, PR_FINDER_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def reminders(self):
        """ :class:`Folder` designated as reminders """

        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self._root, PR_REM_ONLINE_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def inbox(self):
        """ :class:`Folder` designated as inbox """

        try:
            return _folder.Folder(self, _hex(self.mapiobj.GetReceiveFolder(u'IPM', MAPI_UNICODE)[0]))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def junk(self):
        """ :class:`Folder` designated as junk """

        # PR_ADDITIONAL_REN_ENTRYIDS is a multi-value property, 4th entry is the junk folder
        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self._root, PR_ADDITIONAL_REN_ENTRYIDS).Value[4]))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def calendar(self):
        """ :class:`Folder` designated as calendar """

        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self._root, PR_IPM_APPOINTMENT_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def outbox(self):
        """ :class:`Folder` designated as outbox """

        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self.mapiobj, PR_IPM_OUTBOX_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def contacts(self):
        """ :class:`Folder` designated as contacts """

        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self._root, PR_IPM_CONTACT_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def common_views(self):
        """ :class:`Folder` contains folders for managing views for the message store """

        try:
            return _folder.Folder(self, _hex(self.prop(PR_COMMON_VIEWS_ENTRYID).value))
        except NotFoundError:
            pass

    @property
    def drafts(self):
        """ :class:`Folder` designated as drafts """

        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self._root, PR_IPM_DRAFTS_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def wastebasket(self):
        """ :class:`Folder` designated as wastebasket """

        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self.mapiobj, PR_IPM_WASTEBASKET_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def journal(self):
        """ :class:`Folder` designated as journal """

        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self._root, PR_IPM_JOURNAL_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def notes(self):
        """ :class:`Folder` designated as notes """

        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self._root, PR_IPM_NOTE_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def sentmail(self):
        """ :class:`Folder` designated as sentmail """

        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self.mapiobj, PR_IPM_SENTMAIL_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def tasks(self):
        """ :class:`Folder` designated as tasks """

        try:
            return _folder.Folder(self, _hex(HrGetOneProp(self._root, PR_IPM_TASK_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def suggested_contacts(self):
        """ :class`Folder` designated as Suggested contacts"""

        try:
            entryid = _utils.extract_ipm_ol2007_entryids(self.inbox.prop(PR_IPM_OL2007_ENTRYIDS).value, RSF_PID_SUGGESTED_CONTACTS)
            return _folder.Folder(self, entryid)
        except NotFoundError:
            pass

    @property
    def rss(self):
        """ :class`Folder` designated as RSS items"""

        try:
            entryid = _utils.extract_ipm_ol2007_entryids(self.inbox.prop(PR_IPM_OL2007_ENTRYIDS).value, RSF_PID_RSS_SUBSCRIPTION)
            return _folder.Folder(self, entryid)
        except NotFoundError:
            pass

    def delete(self, objects):
        """Delete properties or delegations from a Store

        :param props: The object(s) to remove
        """

        if isinstance(objects, (Property, Delegation)):
            objects = [objects]

        props = [o for o in objects if isinstance(o, Property)]
        if props:
            self.mapiobj.DeleteProps([p.proptag for p in props])
            self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

        delgs = [o for o in objects if isinstance(o, Delegation)]
        for d in delgs:
            d._delete()

    def folder(self, path=None, entryid=None, recurse=False, create=False):
        """ Return :class:`Folder` with given path or entryid; raise exception if not found

        """

        if entryid is not None:
            try:
                return _folder.Folder(self, entryid)
            except (MAPIErrorInvalidEntryid, MAPIErrorNotFound): # XXX move to Folder
                raise NotFoundError("no folder with entryid: '%s'" % entryid)

        return self.subtree.folder(path, recurse=recurse, create=create)

    def get_folder(self, path=None, entryid=None):
        """ Return :class:`folder <Folder>` with given name/entryid or *None* if not found """

        try:
            return self.folder(path, entryid=entryid)
        except NotFoundError:
            pass

    def folders(self, recurse=True, parse=True):
        """ Return all :class:`folders <Folder>` in store

        :param recurse: include all sub-folders
        :param system: include system folders

        """

        # parse=True
        if parse and getattr(self.server.options, 'folders', None):
            for path in self.server.options.folders:
                yield self.folder(_decode(path)) # XXX can optparse output unicode?
            return

        if self.subtree:
            for folder in self.subtree.folders(recurse=recurse):
                yield folder

    def item(self, entryid):
        """ Return :class:`Item` with given entryid; raise exception of not found """ # XXX better exception?

        item = _item.Item() # XXX copy-pasting..
        item.store = self
        item.server = self.server
        try:
            item.mapiobj = _utils.openentry_raw(self.mapiobj, _unhex(entryid), MAPI_MODIFY) # XXX soft-deleted item?
        except MAPIErrorNotFound:
            raise NotFoundError("no item with entryid '%s'" % entryid)
        return item

    @property
    def size(self):
        """ Store size """

        return self.prop(PR_MESSAGE_SIZE_EXTENDED).value

    def config_item(self, name):
        """
        Retrieve the config item for the given `name`.

        :param name: The config item name
        Return :class:`Item` the config item
        """

        table = self.subtree.mapiobj.GetContentsTable(MAPI_DEFERRED_ERRORS | MAPI_ASSOCIATED)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_SUBJECT, SPropValue(PR_SUBJECT, name)), 0)
        rows = table.QueryRows(1,0)
        # No config item found, create new message
        if len(rows) == 0:
            item = self.subtree.associated.create_item(message_class='IPM.Zarafa.Configuration', subject=name)
        else:
            mapiobj = self.subtree.mapiobj.OpenEntry(rows[0][0].Value, None, MAPI_MODIFY)
            item = _item.Item(mapiobj=mapiobj)
        return item

    @property
    def last_logon(self):
        """ Return :datetime Last logon of a user on this store """

        return self.prop(PR_LAST_LOGON_TIME).value or None

    @property
    def last_logoff(self):
        """ Return :datetime of the last logoff of a user on this store """

        return self.prop(PR_LAST_LOGOFF_TIME).value or None

    @property
    def outofoffice(self):
        """ Return :class:`OutOfOffice` """

        # FIXME: If store is public store, return None?
        return OutOfOffice(self)

    @property
    def autoaccept(self):
        return AutoAccept(self)

    @property
    def user(self):
        """ Store :class:`owner <User>` """
        try:
            userid = HrGetOneProp(self.mapiobj, PR_MAILBOX_OWNER_ENTRYID).Value # XXX
            return _user.User(self.server.sa.GetUser(userid, MAPI_UNICODE).Username, self.server)
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def archive_store(self):
        """ Archive :class:`Store` or *None* if not found """

        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)

        try:
            # support for multiple archives was a mistake, and is not and _should not_ be used. so we just pick nr 0.
            entryid = HrGetOneProp(self.mapiobj, PROP_STORE_ENTRYIDS).Value[0]
        except MAPIErrorNotFound:
            return

        return Store(entryid=_hex(entryid), server=self.server) # XXX server?

    @archive_store.setter
    def archive_store(self, store):
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)
        PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

        # XXX only for detaching atm
        if store is None:
            self.mapiobj.DeleteProps([PROP_STORE_ENTRYIDS, PROP_ITEM_ENTRYIDS])
            self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def archive_folder(self):
        """ Archive :class:`Folder` or *None* if not found """

        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

        try:
            # support for multiple archives was a mistake, and is not and _should not_ be used. so we just pick nr 0.
            arch_folderid = HrGetOneProp(self.mapiobj, PROP_ITEM_ENTRYIDS).Value[0]
        except MAPIErrorNotFound:
            return

        return self.archive_store.folder(entryid=codecs.encode(arch_folderid, 'hex'))

    @property
    def company(self):
        if self.server.multitenant:
            table = self.server.sa.OpenUserStoresTable(MAPI_UNICODE)
            table.Restrict(SPropertyRestriction(RELOP_EQ, PR_EC_STOREGUID, SPropValue(PR_EC_STOREGUID, _unhex(self.guid))), TBL_BATCH)
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
        if self.public:
            pubstore = self.company.public_store
            return (pubstore is None or pubstore.guid != self.guid)
        else:
            return (self.user.store is None or self.user.store.guid != self.guid)

    def create_searchfolder(self, text=None): # XXX store.findroot.create_folder()?
        mapiobj = self.findroot.mapiobj.CreateFolder(FOLDER_SEARCH, _encode(str(uuid.uuid4())), _encode('comment'), None, 0)
        return _folder.Folder(self, mapiobj=mapiobj)

    def permissions(self):
        return _utils.permissions(self)

    def permission(self, member, create=False):
        return _utils.permission(self, member, create)

    def _fbmsg_delgs(self):
        fbeid = self.root.prop(PR_FREEBUSY_ENTRYIDS).value[1]
        fbmsg = self.mapiobj.OpenEntry(fbeid, None, MAPI_MODIFY)

        try:
            entryids = HrGetOneProp(fbmsg, PR_SCHDINFO_DELEGATE_ENTRYIDS)
            names = HrGetOneProp(fbmsg, PR_SCHDINFO_DELEGATE_NAMES)
            flags = HrGetOneProp(fbmsg, PR_DELEGATE_FLAGS)
        except MAPIErrorNotFound:
            entryids = SPropValue(PR_SCHDINFO_DELEGATE_ENTRYIDS, [])
            names = SPropValue(PR_SCHDINFO_DELEGATE_NAMES, [])
            flags = SPropValue(PR_DELEGATE_FLAGS, [])

        return fbmsg, (entryids, names, flags)

    def delegations(self):
        fbmsg, (entryids, names, flags) = self._fbmsg_delgs()

        for entryid in entryids.Value:
            username = self.server.sa.GetUser(entryid, MAPI_UNICODE).Username
            yield Delegation(self, self.server.user(username))

    def delegation(self, user, create=False, see_private=False):
        for delegation in self.delegations():
            if delegation.user == user:
                return delegation
        if create:
            fbmsg, (entryids, names, flags) = self._fbmsg_delgs()

            entryids.Value.append(user.userid.decode('hex'))
            names.Value.append(user.name)
            flags.Value.append(1 if see_private else 0)

            fbmsg.SetProps([entryids, names, flags])
            fbmsg.SaveChanges(KEEP_OPEN_READWRITE)

            return Delegation(self, user)
        else:
            raise NotFoundError("no delegation for user '%s'" % user.name)

    def favorites(self):
        """Returns all favorite folders"""

        table = self.common_views.mapiobj.GetContentsTable(MAPI_ASSOCIATED)
        table.SetColumns([PR_MESSAGE_CLASS, PR_SUBJECT, PR_WLINK_ENTRYID, PR_WLINK_FLAGS, PR_WLINK_ORDINAL, PR_WLINK_STORE_ENTRYID, PR_WLINK_TYPE], 0)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, SPropValue(PR_MESSAGE_CLASS, "IPM.Microsoft.WunderBar.Link")), TBL_BATCH)

        for row in table.QueryRows(-1, 0):
            store_entryid = bin2hex(row[5].Value)

            try:
                if store_entryid == self.entryid: # XXX: Handle favorites from public stores
                    yield self.folder(entryid=bin2hex(row[2].Value))
                else:
                    store = Store(entryid=store_entryid, server=self.server)
                    yield store.folder(entryid=bin2hex(row[2].Value))
            except NotFoundError:
                pass

    def _subprops(self, value):
        result = {}
        pos = 0
        while pos < len(value):
            id_ = ord(value[pos])
            cb = ord(value[pos + 1])
            result[id_] = value[pos + 2:pos + 2 + cb]
            pos += 2 + cb
        return result

    def searches(self):
        """Returns all permanent search folders """

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

        # XXX why not just use PR_WLINK_ENTRYID??
        # match common_views SFInfo records against these guids
        table = self.common_views.mapiobj.GetContentsTable(MAPI_ASSOCIATED)
        table.SetColumns([PR_MESSAGE_CLASS, PR_WB_SF_ID], MAPI_UNICODE)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, SPropValue(PR_MESSAGE_CLASS, "IPM.Microsoft.WunderBar.SFInfo")), TBL_BATCH)

        for row in table.QueryRows(-1, 0):
            try:
                yield guid_folder[row[1].Value]
            except KeyError:
                pass

    @property
    def home_server(self):
        if self.user:
            return self.user.home_server
        else:
            try:
                for node in self.server.nodes(): # XXX faster?
                    if node.guid == _hex(self.prop(PR_MAPPING_SIGNATURE).value):
                        return node.name
            except MAPIErrorNotFound:
                return self.server.name

    @property
    def freebusy(self):
        return FreeBusy(self)

    def __eq__(self, s): # XXX check same server?
        if isinstance(s, Store):
            return self.guid == s.guid
        return False

    def __ne__(self, s):
        return not self == s

    def __iter__(self):
        return self.folders()

    def __unicode__(self):
        return u"Store('%s')" % self.guid
