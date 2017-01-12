"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import codecs

import email.parser
import email.utils

import traceback

from functools import wraps

try:
    import cPickle as pickle
except ImportError:
    import pickle

from MAPI.Util import *
from MAPI.Util.Generators import *

import _MAPICore

import inetmapi
import icalmapi

from .compat import (
    unhex as _unhex, is_str as _is_str, repr as _repr, pickle_load as _pickle_load,
    pickle_loads as _pickle_loads, fake_unicode as _unicode
)
from .utils import (
    create_prop as _create_prop, prop as _prop, props as _props, stream as _stream,
    bestbody as _bestbody,
)

from .attachment import Attachment
from .body import Body
from .recurrence import Recurrence, Occurrence
from .prop import Property
from .address import Address
from .table import Table

from .errors import *
from .defs import *

class PersistentList(list):
    def __init__(self, mapiobj, proptag, *args, **kwargs):
        self.mapiobj = mapiobj
        self.proptag = proptag
        for attr in ('append', 'extend', 'insert', 'pop', 'remove', 'reverse', 'sort'):
            setattr(self, attr, self._autosave(getattr(self, attr)))
        list.__init__(self, *args, **kwargs)
    def _autosave(self, func):
        @wraps(func)
        def _func(*args, **kwargs):
            ret = func(*args, **kwargs)
            self.mapiobj.SetProps([SPropValue(self.proptag, self)])
            self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
            return ret
        return _func

class Item(object):
    """Item class"""

    def __init__(self, parent=None, eml=None, ics=None, vcf=None, load=None, loads=None, attachments=True, create=False, mapiobj=None):
        # TODO: self.folder fix this!
        self.emlfile = eml
        self._folder = None

        from .folder import Folder
        if isinstance(parent, Folder):
            self._folder = parent
        # XXX
        self._architem = None

        if mapiobj:
            self.mapiobj = mapiobj

            from .store import Store
            if isinstance(parent, Store):
                self.server = parent.server
            # XXX

        elif create:
            self.mapiobj = self.folder.mapiobj.CreateMessage(None, 0)
            self.store = self.folder.store
            self.server = server = self.store.server # XXX

            if eml is not None:
                # options for CreateMessage: 0 / MAPI_ASSOCIATED
                dopt = inetmapi.delivery_options()
                inetmapi.IMToMAPI(server.mapisession, self.folder.store.mapiobj, None, self.mapiobj, self.emlfile, dopt)
                pass

            elif ics is not None:
                icm = icalmapi.CreateICalToMapi(self.mapiobj, server.ab, False)
                icm.ParseICal(ics, 'utf-8', '', None, 0)
                icm.GetItem(0, 0, self.mapiobj)

            elif vcf is not None:
                import vobject
                v = vobject.readOne(vcf)
                fullname, email = _unicode(v.fn.value), str(v.email.value)
                self.mapiobj.SetProps([ # XXX fix/remove non-essential props, figure out hardcoded numbers
                    SPropValue(PR_ADDRTYPE, 'SMTP'), SPropValue(PR_BODY, ''),
                    SPropValue(PR_LOCALITY, ''), SPropValue(PR_STATE_OR_PROVINCE, ''),
                    SPropValue(PR_BUSINESS_FAX_NUMBER, ''), SPropValue(PR_COMPANY_NAME, ''),
                    SPropValue(0x8130001F, fullname), SPropValue(0x8132001E, 'SMTP'),
                    SPropValue(0x8133001E, email), SPropValue(0x8134001E, ''),
                    SPropValue(0x81350102, server.ab.CreateOneOff('', 'SMTP', email, 0)), # XXX
                    SPropValue(PR_GIVEN_NAME, ''), SPropValue(PR_MIDDLE_NAME, ''),
                    SPropValue(PR_NORMALIZED_SUBJECT, ''), SPropValue(PR_TITLE, ''),
                    SPropValue(PR_TRANSMITABLE_DISPLAY_NAME, ''),
                    SPropValue(PR_DISPLAY_NAME_W, fullname),
                    SPropValue(0x80D81003, [0]), SPropValue(0x80D90003, 1), 
                    SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Contact'),
                ])

            elif load is not None:
                self.load(load, attachments=attachments)
            elif loads is not None:
                self.loads(loads, attachments=attachments)

            else:
                try:
                    container_class = HrGetOneProp(self.folder.mapiobj, PR_CONTAINER_CLASS).Value
                except MAPIErrorNotFound:
                    self.mapiobj.SetProps([SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Note')])
                else:
                    if container_class == 'IPF.Contact': # XXX just skip first 4 chars? 
                        self.mapiobj.SetProps([SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Contact')]) # XXX set default props
                    elif container_class == 'IPF.Appointment':
                        self.mapiobj.SetProps([SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Appointment')]) # XXX set default props

            self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def _arch_item(self): # open archive store explicitly so we can handle otherwise silenced errors (MAPI errors in mail bodies for example)
        if self._architem is None:
            if self.stubbed:
                ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0)
                PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)
                PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

                # support for multiple archives was a mistake, and is not and _should not_ be used. so we just pick nr 0.
                arch_storeid = HrGetOneProp(self.mapiobj, PROP_STORE_ENTRYIDS).Value[0]
                item_entryid = HrGetOneProp(self.mapiobj, PROP_ITEM_ENTRYIDS).Value[0]

                self._architem = self.server._store2(arch_storeid).OpenEntry(item_entryid, None, 0)
            else:
                self._architem = self.mapiobj
        return self._architem

    @property
    def entryid(self):
        """ Item entryid """

        return bin2hex(HrGetOneProp(self.mapiobj, PR_ENTRYID).Value)

    @property
    def hierarchyid(self):
        return HrGetOneProp(self.mapiobj, PR_EC_HIERARCHYID).Value

    @property
    def sourcekey(self):
        """ Item sourcekey """

        if not hasattr(self, '_sourcekey'): # XXX more general caching solution
            self._sourcekey = bin2hex(HrGetOneProp(self.mapiobj, PR_SOURCE_KEY).Value)
        return self._sourcekey

    @property
    def subject(self):
        """ Item subject or *None* if no subject """

        try:
            return self.prop(PR_SUBJECT_W).value
        except MAPIErrorNotFound:
            return u''

    @subject.setter
    def subject(self, x):
        self.mapiobj.SetProps([SPropValue(PR_SUBJECT_W, _unicode(x))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def body(self):
        """ Item :class:`body <Body>` """

        return Body(self) # XXX return None if no body..?

    @property
    def size(self):
        """ Item size """

        return self.prop(PR_MESSAGE_SIZE).value

    @property
    def message_class(self):
        return self.prop(PR_MESSAGE_CLASS_W).value

    @message_class.setter
    def message_class(self, messageclass):
        # FIXME: Add all possible PR_MESSAGE_CLASS values
        """
        MAPI Message classes:
        * IPM.Note.SMIME.MultipartSigned - smime signed email
        * IMP.Note                       - normal email
        * IPM.Note.SMIME                 - smime encypted email
        * IPM.StickyNote                 - note
        * IPM.Appointment                - appointment
        * IPM.Task                       - task
        """
        self.mapiobj.SetProps([SPropValue(PR_MESSAGE_CLASS_W, _unicode(messageclass))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @body.setter
    def body(self, x):
        self.mapiobj.SetProps([SPropValue(PR_BODY_W, _unicode(x))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def created(self):
        try:
            return self.prop(PR_CREATION_TIME).value
        except MAPIErrorNotFound:
            pass

    @property
    def received(self):
        """ Datetime instance with item delivery time """

        try:
            return self.prop(PR_MESSAGE_DELIVERY_TIME).value
        except MAPIErrorNotFound:
            pass

    @property
    def last_modified(self):
        try:
            return self.prop(PR_LAST_MODIFICATION_TIME).value
        except MAPIErrorNotFound:
            pass

    @property
    def stubbed(self):
        """ Is item stubbed by archiver? """

        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX cache folder.GetIDs..?
        PROP_STUBBED = CHANGE_PROP_TYPE(ids[2], PT_BOOLEAN)
        try:
            return HrGetOneProp(self.mapiobj, PROP_STUBBED).Value # False means destubbed
        except MAPIErrorNotFound:
            return False

    @property
    def read(self):
        """ Return boolean which shows if a message has been read """

        return self.prop(PR_MESSAGE_FLAGS).value & MSGFLAG_READ > 0

    @read.setter
    def read(self, value):
        if value:
            self.mapiobj.SetReadFlag(0)
        else:
            self.mapiobj.SetReadFlag(CLEAR_READ_FLAG)

    @property
    def categories(self):
        proptag = self.mapiobj.GetIDsFromNames([NAMED_PROP_CATEGORY], MAPI_CREATE)[0]
        proptag = CHANGE_PROP_TYPE(proptag, PT_MV_STRING8)
        try:
            value = self.prop(proptag).value
        except MAPIErrorNotFound:
            value = []
        return PersistentList(self.mapiobj, proptag, value)

    @categories.setter
    def categories(self, value):
        proptag = self.mapiobj.GetIDsFromNames([NAMED_PROP_CATEGORY], MAPI_CREATE)[0]
        proptag = CHANGE_PROP_TYPE(proptag, PT_MV_STRING8)
        self.mapiobj.SetProps([SPropValue(proptag, list(value))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def folder(self):
        """ Parent :class:`Folder` of an item """
        from .folder import Folder

        if self._folder:
            return self._folder
        try:
            return Folder(self.store, HrGetOneProp(self.mapiobj, PR_PARENT_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def importance(self):
        """ Importance """

        # TODO: userfriendly repr of value
        try:
            return self.prop(PR_IMPORTANCE).value
        except MAPIErrorNotFound:
            pass

    @importance.setter
    def importance(self, value):
        """ Set importance

        PR_IMPORTANCE_LOW
        PR_IMPORTANCE_MEDIUM
        PR_IMPORTANCE_HIGH
        """

        self.mapiobj.SetProps([SPropValue(PR_IMPORTANCE, value)])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def create_prop(self, proptag, value, proptype=None):
        return _create_prop(self, self.mapiobj, proptag, value, proptype)

    def prop(self, proptag):
        return _prop(self, self.mapiobj, proptag)

    def get_prop(self, proptag):
        try:
            return self.prop(proptag)
        except MAPIErrorNotFound:
            pass

    def props(self, namespace=None):
        return _props(self.mapiobj, namespace)

    def attachments(self, embedded=False):
        """ Return item :class:`attachments <Attachment>`

        :param embedded: include embedded attachments
        """

        mapiitem = self._arch_item
        table = mapiitem.GetAttachmentTable(MAPI_DEFERRED_ERRORS)
        table.SetColumns([PR_ATTACH_NUM, PR_ATTACH_METHOD], TBL_BATCH)
        while True:
            rows = table.QueryRows(50, 0)
            if len(rows) == 0:
                break
            for row in rows:
                if row[1].Value == ATTACH_BY_VALUE or (embedded and row[1].Value == ATTACH_EMBEDDED_MSG):
                    att = mapiitem.OpenAttach(row[0].Value, IID_IAttachment, 0)
                    yield Attachment(att)

    def create_attachment(self, name, data):
        """Create a new attachment

        :param name: the attachment name
        :param data: string containing the attachment data
        """

        # XXX: use file object instead of data?
        (id_, attach) = self.mapiobj.CreateAttach(None, 0)
        name = _unicode(name)
        props = [SPropValue(PR_ATTACH_LONG_FILENAME_W, name), SPropValue(PR_ATTACH_METHOD, ATTACH_BY_VALUE)]
        attach.SetProps(props)
        stream = attach.OpenProperty(PR_ATTACH_DATA_BIN, IID_IStream, STGM_WRITE|STGM_TRANSACTED, MAPI_MODIFY | MAPI_CREATE)
        stream.Write(data)
        stream.Commit(0)
        attach.SaveChanges(KEEP_OPEN_READWRITE)
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE) # XXX needed?
        return Attachment(attach)

    def header(self, name):
        """ Return transport message header with given name """

        return self.headers().get(name)

    def headers(self):
        """ Return transport message headers """

        try:
            message_headers = self.prop(PR_TRANSPORT_MESSAGE_HEADERS_W)
            headers = email.parser.Parser().parsestr(message_headers.value, headersonly=True)
            return headers
        except MAPIErrorNotFound:
            return {}

    def eml(self):
        """ Return .eml version of item """
        if self.emlfile is None:
            try:
                self.emlfile = _stream(self.mapiobj, PR_EC_IMAP_EMAIL)
            except MAPIErrorNotFound:
                sopt = inetmapi.sending_options()
                sopt.no_recipients_workaround = True
                self.emlfile = inetmapi.IMToINet(self.store.server.mapisession, None, self.mapiobj, sopt)
        return self.emlfile

    def vcf(self): # XXX don't we have this builtin somewhere? very basic for now
        import vobject
        v = vobject.vCard()
        v.add('n')
        v.n.value = vobject.vcard.Name(family='', given='') # XXX
        v.add('fn')
        v.fn.value = ''
        v.add('email')
        v.email.value = ''
        v.email.type_param = 'INTERNET'
        try:
            v.fn.value = HrGetOneProp(self.mapiobj, 0x8130001E).Value
        except MAPIErrorNotFound:
            pass
        try:
            v.email.value = HrGetOneProp(self.mapiobj, 0x8133001E).Value
        except MAPIErrorNotFound:
            pass
        return v.serialize()

    def ics(self, charset="UTF-8"):
        mic = icalmapi.CreateMapiToICal(self.server.ab, charset)
        mic.AddMessage(self.mapiobj, "", 0)
        method, data = mic.Finalize(0)
        return data

    def send(self):
        props = []
        props.append(SPropValue(PR_SENTMAIL_ENTRYID, _unhex(self.folder.store.sentmail.entryid)))
        props.append(SPropValue(PR_DELETE_AFTER_SUBMIT, True))
        self.mapiobj.SetProps(props)
        self.mapiobj.SubmitMessage(0)

    @property
    def sender(self):
        """ Sender :class:`Address` """

        addrprops = (PR_SENDER_ADDRTYPE_W, PR_SENDER_NAME_W, PR_SENDER_EMAIL_ADDRESS_W, PR_SENDER_ENTRYID)
        args = [self.get_prop(p).value if self.get_prop(p) else None for p in addrprops]
        return Address(self.server, *args)

    @property
    def from_(self):
        """ From :class:`Address` """

        addrprops= (PR_SENT_REPRESENTING_ADDRTYPE_W, PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, PR_SENT_REPRESENTING_ENTRYID)
        args = [self.get_prop(p).value if self.get_prop(p) else None for p in addrprops]
        return Address(self.server, *args)

    @from_.setter
    def from_(self, addr):
        pr_addrtype, pr_dispname, pr_email, pr_entryid = self._addr_props(addr)
        self.mapiobj.SetProps([
            SPropValue(PR_SENT_REPRESENTING_ADDRTYPE_W, _unicode(pr_addrtype)), # XXX pr_addrtype should be unicode already
            SPropValue(PR_SENT_REPRESENTING_NAME_W, pr_dispname),
            SPropValue(PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, pr_email),
            SPropValue(PR_SENT_REPRESENTING_ENTRYID, pr_entryid),
        ])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def table(self, name, restriction=None, order=None, columns=None):
        return Table(self.server, self.mapiobj.OpenProperty(name, IID_IMAPITable, MAPI_UNICODE, 0), name, restriction=restriction, order=order, columns=columns)

    def tables(self):
        yield self.table(PR_MESSAGE_RECIPIENTS)
        yield self.table(PR_MESSAGE_ATTACHMENTS)

    def recipients(self, _type=None):
        """ Return recipient :class:`addresses <Address>` """

        for row in self.table(PR_MESSAGE_RECIPIENTS):
            row = dict([(x.proptag, x) for x in row])
            if not _type or row[PR_RECIPIENT_TYPE].value == _type:
                args = [row[p].value if p in row else None for p in (PR_ADDRTYPE_W, PR_DISPLAY_NAME_W, PR_EMAIL_ADDRESS_W, PR_ENTRYID)]
                yield Address(self.server, *args)

    @property
    def to(self):
        return self.recipients(_type=MAPI_TO)

    @property
    def cc(self):
        return self.recipients(_type=MAPI_CC)

    @property
    def bcc(self):
        return self.recipients(_type=MAPI_BCC)

    @property
    def start(self): # XXX optimize, guid
        return self.prop('common:34070').value

    @property
    def end(self): # XXX optimize, guid
        return self.prop('common:34071').value

    @property
    def recurring(self):
        return self.prop('appointment:33315').value

    @property
    def recurrence(self):
        return Recurrence(self)

    def occurrences(self, start=None, end=None):
        try:
            if self.recurring:
                recurrences = self.recurrence.recurrences
                if start and end:
                    recurrences = recurrences.between(start, end)
                for d in recurrences:
                    occ = Occurrence(self, d, d+datetime.timedelta(hours=1)) # XXX
                    if (not start or occ.start >= start) and (not end or occ.end < end): # XXX slow for now; overlaps with start, end?
                        yield occ
            else:
                occ = Occurrence(self, self.start, self.end)
                if (not start or occ.start >= start) and (not end or occ.end < end):
                    yield occ
        except MAPIErrorNotFound: # XXX shouldn't happen
            pass

    def _addr_props(self, addr):
        from .user import User

        if isinstance(addr, User):
            pr_addrtype = 'ZARAFA'
            pr_dispname = addr.name
            pr_email = addr.email
            pr_entryid = _unhex(addr.userid)
        else:
            addr = _unicode(addr)
            pr_addrtype = 'SMTP'
            pr_dispname, pr_email = email.utils.parseaddr(addr)
            pr_dispname = pr_dispname or u'nobody'
            pr_entryid = self.server.ab.CreateOneOff(pr_dispname, u'SMTP', _unicode(pr_email), MAPI_UNICODE)
        return pr_addrtype, pr_dispname, pr_email, pr_entryid

    @to.setter
    def to(self, addrs):
        from .user import User

        if _is_str(addrs):
            addrs = _unicode(addrs).split(';')
        elif isinstance(addrs, User):
            addrs = [addrs]
        names = []
        for addr in addrs:
            pr_addrtype, pr_dispname, pr_email, pr_entryid = self._addr_props(addr)
            names.append([
                SPropValue(PR_RECIPIENT_TYPE, MAPI_TO), 
                SPropValue(PR_DISPLAY_NAME_W, pr_dispname),
                SPropValue(PR_ADDRTYPE_W, _unicode(pr_addrtype)),
                SPropValue(PR_EMAIL_ADDRESS_W, _unicode(pr_email)),
                SPropValue(PR_ENTRYID, pr_entryid),
            ])
        self.mapiobj.ModifyRecipients(0, names)
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE) # XXX needed?

    def delete(self, items):
        """Delete properties or attachments from an Item

        :param items: The Attachments or Properties
        """

        if isinstance(items, (Attachment, Property)):
            items = [items]
        else:
            items = list(items)

        attach_ids = [item.number for item in items if isinstance(item, Attachment)]
        proptags = [item.proptag for item in items if isinstance(item, Property)]
        if proptags:
            self.mapiobj.DeleteProps(proptags)
        for attach_id in attach_ids:
            self._arch_item.DeleteAttach(attach_id, 0, None, 0)

        # XXX: refresh the mapiobj since PR_ATTACH_NUM is updated when opening
        # a message? PR_HASATTACH is also updated by the server.
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def _convert_to_smtp(self, props, tag_data):
        if not hasattr(self.server, '_smtp_cache'): # XXX speed hack, discuss
            self.server._smtp_cache = {}
        for addrtype, email, entryid, name, searchkey in ADDR_PROPS:
            if addrtype not in tag_data or entryid not in tag_data or name not in tag_data: 
                continue
            if tag_data[addrtype][1] in (u'SMTP', u'MAPIPDL'): # XXX MAPIPDL==distlist.. can we just dump this?
                continue
            eid = tag_data[entryid][1]
            if eid in self.server._smtp_cache:
                email_addr = self.server._smtp_cache[eid]
            else:
                try:
                    mailuser = self.server.ab.OpenEntry(eid, IID_IMailUser, 0)
                    email_addr = HrGetOneProp(mailuser, PR_SMTP_ADDRESS_W).Value
                except MAPIErrorUnknownEntryid: # XXX corrupt data? keep going but log problem
                    continue
                except MAPIErrorNotFound: # XXX deleted user, or no email address? or user with multiple entryids..heh?
                    continue
                except MAPIErrorInterfaceNotSupported: # XXX ZARAFA group?
                    continue
                self.server._smtp_cache[eid] = email_addr
            tag_data[addrtype][1] = u'SMTP'
            if email in tag_data:
                tag_data[email][1] = email_addr
            else:
                props.append([email, email_addr, None])
            tag_data[entryid][1] = self.server.ab.CreateOneOff(tag_data[name][1], u'SMTP', email_addr, MAPI_UNICODE)
            key = b'SMTP:'+email_addr.upper().encode('ascii')
            if searchkey in tag_data: # XXX probably need to create, also email
                tag_data[searchkey][1] = key
            else:
                props.append([searchkey, key, None])

    def _dump(self, attachments=True, archiver=True, skip_broken=False):
        # props
        props = []
        tag_data = {}
        bestbody = _bestbody(self.mapiobj)
        for prop in self.props():
            if (bestbody != PR_NULL and prop.proptag in (PR_BODY_W, PR_HTML, PR_RTF_COMPRESSED) and prop.proptag != bestbody):
                continue
            if prop.named: # named prop: prop.id_ system dependant..
                data = [prop.proptag, prop.mapiobj.Value, self.mapiobj.GetNamesFromIDs([prop.proptag], None, 0)[0]]
                if not archiver and data[2].guid == PSETID_Archive:
                    continue
            else:
                data = [prop.proptag, prop.mapiobj.Value, None]
            props.append(data)
            tag_data[prop.proptag] = data
        self._convert_to_smtp(props, tag_data)

        # recipients
        recs = []
        for row in self.table(PR_MESSAGE_RECIPIENTS):
            rprops = []
            tag_data = {}
            for prop in row:
                data = [prop.proptag, prop.mapiobj.Value, None]
                rprops.append(data)
                tag_data[prop.proptag] = data
            recs.append(rprops)
            self._convert_to_smtp(rprops, tag_data)

        # attachments
        atts = []
        # XXX optimize by looking at PR_MESSAGE_FLAGS?
        for row in self.table(PR_MESSAGE_ATTACHMENTS).dict_rows(): # XXX should we use GetAttachmentTable?
            try:
                num = row[PR_ATTACH_NUM]
                method = row[PR_ATTACH_METHOD] # XXX default
                att = self.mapiobj.OpenAttach(num, IID_IAttachment, 0)
                if method == ATTACH_EMBEDDED_MSG:
                    msg = att.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_DEFERRED_ERRORS)
                    item = Item(mapiobj=msg)
                    item.server = self.server # XXX
                    data = item._dump() # recursion
                    atts.append(([[a, b, None] for a, b in row.items()], data))
                elif method == ATTACH_BY_VALUE and attachments:
                    data = _stream(att, PR_ATTACH_DATA_BIN)
                    atts.append(([[a, b, None] for a, b in row.items()], data))
            except Exception as e: # XXX generalize so usable in more places
                service = self.server.service
                log = (service or self.server).log
                if log:
                    log.error('could not serialize attachment')
                if skip_broken:
                    if log:
                        log.error(traceback.format_exc(e))
                    if service and service.stats:
                        service.stats['errors'] += 1
                else:
                    raise

        return {
            b'props': props,
            b'recipients': recs,
            b'attachments': atts,
        }

    def dump(self, f, attachments=True, archiver=True):
        pickle.dump(self._dump(attachments=attachments, archiver=archiver), f, pickle.HIGHEST_PROTOCOL)

    def dumps(self, attachments=True, archiver=True, skip_broken=False):
        return pickle.dumps(self._dump(attachments=attachments, archiver=archiver, skip_broken=skip_broken), pickle.HIGHEST_PROTOCOL)

    def _load(self, d, attachments):
        # props
        props = []
        for proptag, value, nameid in d[b'props']:
            if nameid is not None:
                proptag = self.mapiobj.GetIDsFromNames([nameid], MAPI_CREATE)[0] | (proptag & 0xffff)
            if (proptag >> 16) not in (PR_BODY>>16, PR_HTML>>16, PR_RTF_COMPRESSED>>16) or \
               value not in (MAPI_E_NOT_FOUND, MAPI_E_NOT_ENOUGH_MEMORY): # XXX why do we backup these errors
                props.append(SPropValue(proptag, value))
        self.mapiobj.SetProps(props)

        # recipients
        recipients = [[SPropValue(proptag, value) for (proptag, value, nameid) in row] for row in d[b'recipients']]
        self.mapiobj.ModifyRecipients(0, recipients)

        # attachments
        for props, data in d[b'attachments']:
            props = [SPropValue(proptag, value) for (proptag, value, nameid) in props]
            (id_, attach) = self.mapiobj.CreateAttach(None, 0)
            attach.SetProps(props)
            if isinstance(data, dict): # embedded message
                msg = attach.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY)
                item = Item(mapiobj=msg)
                item._load(data, attachments) # recursion
            elif attachments:
                stream = attach.OpenProperty(PR_ATTACH_DATA_BIN, IID_IStream, STGM_WRITE|STGM_TRANSACTED, MAPI_MODIFY | MAPI_CREATE)
                stream.Write(data)
                stream.Commit(0)
            attach.SaveChanges(KEEP_OPEN_READWRITE)
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE) # XXX needed?

    def load(self, f, attachments=True):
        self._load(_pickle_load(f), attachments)

    def loads(self, s, attachments=True):
        self._load(_pickle_loads(s), attachments)

    @property
    def embedded(self): # XXX multiple?
        for row in self.table(PR_MESSAGE_ATTACHMENTS).dict_rows(): # XXX should we use GetAttachmentTable?
            num = row[PR_ATTACH_NUM]
            method = row[PR_ATTACH_METHOD] # XXX default
            if method == ATTACH_EMBEDDED_MSG:
                att = self.mapiobj.OpenAttach(num, IID_IAttachment, 0)
                msg = att.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_MODIFY | MAPI_DEFERRED_ERRORS)
                item = Item(mapiobj=msg)
                item.server = self.server # XXX
                return item

    @property
    def primary_item(self):
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_REF_ITEM_ENTRYID = CHANGE_PROP_TYPE(ids[4], PT_BINARY)
        entryid = HrGetOneProp(self.mapiobj, PROP_REF_ITEM_ENTRYID).Value

        try:
            return self.folder.primary_store.item(entryid=codecs.encode(entryid, 'hex'))
        except MAPIErrorNotFound:
            pass

    def restore(self):
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX cache folder.GetIDs..?
        PROP_STUBBED = CHANGE_PROP_TYPE(ids[2], PT_BOOLEAN)
        PROP_REF_STORE_ENTRYID = CHANGE_PROP_TYPE(ids[3], PT_BINARY)
        PROP_REF_ITEM_ENTRYID = CHANGE_PROP_TYPE(ids[4], PT_BINARY)
        PROP_REF_PREV_ENTRYID = CHANGE_PROP_TYPE(ids[5], PT_BINARY)
        PROP_FLAGS = CHANGE_PROP_TYPE(ids[6], PT_LONG)

        # get/create primary item
        primary_item = self.primary_item
        if not primary_item:
            mapiobj = self.folder.primary_folder.mapiobj.CreateMessage(None, 0)
            new = True
        else:
            mapiobj = primary_item.mapiobj
            new = False

        if not new and not primary_item.stubbed:
            return
        # cleanup primary item
        mapiobj.DeleteProps([PROP_STUBBED, PR_ICON_INDEX])
        at = mapiobj.GetAttachmentTable(0)
        at.SetColumns([PR_ATTACH_NUM], TBL_BATCH)
        while True:
            rows = at.QueryRows(20, 0)
            if len(rows) == 0:
                break
            for row in rows:
                mapiobj.DeleteAttach(row[0].Value, 0, None, 0)

        # copy contents into it
        exclude_props = [PROP_REF_STORE_ENTRYID, PROP_REF_ITEM_ENTRYID, PROP_REF_PREV_ENTRYID, PROP_FLAGS]
        self.mapiobj.CopyTo(None, exclude_props, 0, None, IID_IMessage, mapiobj, 0)

        mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

        # update backref
        if new:
            entryid = HrGetOneProp(mapiobj, PR_ENTRYID).Value
            self.mapiobj.SetProps([SPropValue(PROP_REF_ITEM_ENTRYID, entryid)])
            self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def __unicode__(self):
        return u'Item(%s)' % self.subject

    def __repr__(self):
        return _repr(self)


