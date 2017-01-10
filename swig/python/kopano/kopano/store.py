"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import codecs
import uuid

from .folder import Folder
from .autoaccept import AutoAccept
from .ooo import Outofoffice
from .prop import Property
from .item import Item
from .errors import *
from .defs import *

from .utils import (
    extract_ipm_ol2007_entryids as _extract_ipm_ol2007_entryids, unhex as _unhex,
    decode as _decode, openentry_raw as _openentry_raw, create_prop as _create_prop,
    prop as _prop, props as _props, permissions as _permissions, permission as _permission
)

try:
    import libcommon # XXX distribute with python-mapi? or rewrite functionality here?
except ImportError:
    pass

from MAPI.Util import *

class Store(object):
    """Store class"""

    def __init__(self, guid=None, entryid=None, mapiobj=None, server=None):
        from . import Server
        self.server = server or Server()

        if guid:
            mapiobj = self.server._store(guid)
        elif entryid:
            mapiobj = self.server._store2(entryid)
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
    def hierarchyid(self):
        return  self.prop(PR_EC_HIERARCHYID).value

    @property
    def root(self):
        """ :class:`Folder` designated as store root """

        return Folder(self, HrGetOneProp(self._root, PR_ENTRYID).Value)

    @property
    def subtree(self):
        """ :class:`Folder` designated as IPM.Subtree """

        try:
            if self.public:
                ipmsubtreeid = HrGetOneProp(self.mapiobj, PR_IPM_PUBLIC_FOLDERS_ENTRYID).Value
            else:
                ipmsubtreeid = HrGetOneProp(self.mapiobj, PR_IPM_SUBTREE_ENTRYID).Value

            return Folder(self, ipmsubtreeid)
        except MAPIErrorNotFound:
            pass

    @property
    def findroot(self):
        """ :class:`Folder` designated as search-results root """

        try:
            return Folder(self, HrGetOneProp(self.mapiobj, PR_FINDER_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def inbox(self):
        """ :class:`Folder` designated as inbox """

        try:
            return Folder(self, self.mapiobj.GetReceiveFolder(u'IPM', MAPI_UNICODE)[0])
        except MAPIErrorNotFound:
            pass

    @property
    def junk(self):
        """ :class:`Folder` designated as junk """

        # PR_ADDITIONAL_REN_ENTRYIDS is a multi-value property, 4th entry is the junk folder
        try:
            return Folder(self, HrGetOneProp(self._root, PR_ADDITIONAL_REN_ENTRYIDS).Value[4])
        except MAPIErrorNotFound:
            pass

    @property
    def calendar(self):
        """ :class:`Folder` designated as calendar """

        try:
            return Folder(self, HrGetOneProp(self._root, PR_IPM_APPOINTMENT_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def outbox(self):
        """ :class:`Folder` designated as outbox """

        try:
            return Folder(self, HrGetOneProp(self.mapiobj, PR_IPM_OUTBOX_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def contacts(self):
        """ :class:`Folder` designated as contacts """

        try:
            return Folder(self, HrGetOneProp(self._root, PR_IPM_CONTACT_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def common_views(self):
        """ :class:`Folder` contains folders for managing views for the message store """

        try:
            return Folder(self, self.prop(PR_COMMON_VIEWS_ENTRYID).value)
        except MAPIErrorNotFound:
            pass

    @property
    def drafts(self):
        """ :class:`Folder` designated as drafts """

        try:
            return Folder(self, HrGetOneProp(self._root, PR_IPM_DRAFTS_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def wastebasket(self):
        """ :class:`Folder` designated as wastebasket """

        try:
            return Folder(self, HrGetOneProp(self.mapiobj, PR_IPM_WASTEBASKET_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def journal(self):
        """ :class:`Folder` designated as journal """

        try:
            return Folder(self, HrGetOneProp(self._root, PR_IPM_JOURNAL_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def notes(self):
        """ :class:`Folder` designated as notes """

        try:
            return Folder(self, HrGetOneProp(self._root, PR_IPM_NOTE_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def sentmail(self):
        """ :class:`Folder` designated as sentmail """

        try:
            return Folder(self, HrGetOneProp(self.mapiobj, PR_IPM_SENTMAIL_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def tasks(self):
        """ :class:`Folder` designated as tasks """

        try:
            return Folder(self, HrGetOneProp(self._root, PR_IPM_TASK_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def suggested_contacts(self):
        """ :class`Folder` designated as Suggested contacts"""

        try:
            entryid = _extract_ipm_ol2007_entryids(self.inbox.prop(PR_IPM_OL2007_ENTRYIDS).value, RSF_PID_SUGGESTED_CONTACTS)
            return Folder(self, _unhex(entryid))
        except MAPIErrorNotFound:
            pass

    @property
    def rss(self):
        """ :class`Folder` designated as RSS items"""

        try:
            entryid = _extract_ipm_ol2007_entryids(self.inbox.prop(PR_IPM_OL2007_ENTRYIDS).value, RSF_PID_RSS_SUBSCRIPTION)
            return Folder(self, _unhex(entryid))
        except MAPIErrorNotFound:
            pass

    def delete(self, props):
        """Delete properties from a Store

        :param props: The properties to remove
        """

        if isinstance(props, Property):
            props = [props]

        self.mapiobj.DeleteProps([p.proptag for p in props])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def folder(self, path=None, entryid=None, recurse=False, create=False): # XXX sloowowowww
        """ Return :class:`Folder` with given path or entryid; raise exception if not found

        """

        if entryid is not None:
            try:
                return Folder(self, _unhex(entryid))
            except (MAPIErrorInvalidEntryid, MAPIErrorNotFound):
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
        filter_names = None
        if parse and getattr(self.server.options, 'folders', None):
            for path in self.server.options.folders:
                yield self.folder(_decode(path)) # XXX can optparse output unicode?
            return

        if self.subtree:
            for folder in self.subtree.folders(recurse=recurse):
                yield folder

    def item(self, entryid):
        """ Return :class:`Item` with given entryid; raise exception of not found """ # XXX better exception?

        item = Item() # XXX copy-pasting..
        item.store = self
        item.server = self.server
        item.mapiobj = _openentry_raw(self.mapiobj, _unhex(entryid), MAPI_MODIFY) # XXX soft-deleted item?
        return item

    @property
    def size(self):
        """ Store size """

        return self.prop(PR_MESSAGE_SIZE_EXTENDED).value

    def config_item(self, name):
        item = Item()
        item.mapiobj = libcommon.GetConfigMessage(self.mapiobj, name)
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
        """ Return :class:`Outofoffice` """

        # FIXME: If store is public store, return None?
        return Outofoffice(self)

    @property
    def autoaccept(self):
        return AutoAccept(self)

    @property
    def user(self):
        """ Store :class:`owner <User>` """
        from .user import User

        try:
            userid = HrGetOneProp(self.mapiobj, PR_MAILBOX_OWNER_ENTRYID).Value # XXX
            return User(self.server.sa.GetUser(userid, MAPI_UNICODE).Username, self.server)
        except MAPIErrorNotFound:
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

        return Store(entryid=entryid, server=self.server) # XXX server?

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
            for row in table.QueryRows(1,0):
                storetype = PpropFindProp(row, PR_EC_STORETYPE)
                if storetype.Value == ECSTORE_TYPE_PUBLIC:
                    companyname = PpropFindProp(row, PR_EC_USERNAME_W) # XXX bug in ECUserStoreTable.cpp?
                    if not companyname:
                        companyname = PpropFindProp(row, PR_EC_COMPANY_NAME_W) # XXX
                else:
                    companyname = PpropFindProp(row, PR_EC_COMPANY_NAME_W)
                return self.server.company(companyname.Value)
        else:
            return self.server.companies().next()

    @property
    def orphan(self):
        if self.public:
            pubstore = self.company.public_store
            return (pubstore is None or pubstore.guid != self.guid)
        else:
            return (self.user.store is None or self.user.store.guid != self.guid)

    def create_prop(self, proptag, value, proptype=None):
        return _create_prop(self, self.mapiobj, proptag, value, proptype)
    def prop(self, proptag):
        return _prop(self, self.mapiobj, proptag)

    def props(self, namespace=None):
        return _props(self.mapiobj, namespace=namespace)

    def create_searchfolder(self, text=None): # XXX store.findroot.create_folder()?
        mapiobj = self.findroot.mapiobj.CreateFolder(FOLDER_SEARCH, str(uuid.uuid4()), 'comment', None, 0)
        return Folder(self, mapiobj=mapiobj)

    def permissions(self):
        return _permissions(self)

    def permission(self, member, create=False):
        return _permission(self, member, create)

    def favorites(self):
        """Returns all favorite folders"""

        table = self.common_views.mapiobj.GetContentsTable(MAPI_ASSOCIATED)
        table.SetColumns([PR_MESSAGE_CLASS, PR_SUBJECT, PR_WLINK_ENTRYID, PR_WLINK_FLAGS, PR_WLINK_ORDINAL, PR_WLINK_STORE_ENTRYID, PR_WLINK_TYPE], 0)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, SPropValue(PR_MESSAGE_CLASS, "IPM.Microsoft.WunderBar.Link")), TBL_BATCH)

        for row in table.QueryRows(-1, 0):
            entryid = bin2hex(row[2].Value)
            store_entryid = bin2hex(row[5].Value)

            if store_entryid == self.entryid: # XXX: Handle favorites from public stores
                try:
                    yield self.folder(entryid=bin2hex(row[2].Value))
                except NotFoundError:
                    pass

    def _subprops(self, value):
        result = {}
        pos = 0
        while pos < len(value):
            id_ = ord(value[pos])
            cb = ord(value[pos+1])
            result[id_] = value[pos+2:pos+2+cb]
            pos += 2 + cb
        return result

    def searches(self):
        """Returns all permanent search folders """

        findroot = self.root.folder('FINDER_ROOT') # XXX

        # extract special type of guid from search folder PR_FOLDER_DISPLAY_FLAGS to match against
        guid_folder = {}
        for folder in findroot.folders():
            try:
                prop = folder.prop(PR_FOLDER_DISPLAY_FLAGS)
                subprops = self._subprops(prop.value)
                guid_folder[subprops[2]] = folder
            except MAPIErrorNotFound:
                pass

        # match common_views SFInfo records against these guids
        table = self.common_views.mapiobj.GetContentsTable(MAPI_ASSOCIATED)
        table.SetColumns([PR_MESSAGE_CLASS, PR_WB_SF_ID], MAPI_UNICODE)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, SPropValue(PR_MESSAGE_CLASS, "IPM.Microsoft.WunderBar.SFInfo")), TBL_BATCH)

        for row in table.QueryRows(-1, 0):
            try:
                yield guid_folder[row[1].Value]
            except KeyError :
                pass

    def __eq__(self, s): # XXX check same server?
        if isinstance(s, Store):
            return self.guid == s.guid
        return False

    def __ne__(self, s):
        return not self == s

    def __unicode__(self):
        return u"Store('%s')" % self.guid

    def __repr__(self):
        return _repr(self)
