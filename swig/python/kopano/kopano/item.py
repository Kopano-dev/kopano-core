"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import codecs
import datetime
import email.parser
import email.utils
from functools import wraps
import random
import sys
import struct
import time
import traceback
import warnings

try:
    import cPickle as pickle
except ImportError:
    import pickle

import inetmapi
import icalmapi

from MAPI import (
    KEEP_OPEN_READWRITE, PT_MV_BINARY, PT_BOOLEAN, MSGFLAG_READ,
    CLEAR_READ_FLAG, PT_MV_STRING8, PT_BINARY, PT_LONG,
    MAPI_CREATE, MAPI_MODIFY, MAPI_DEFERRED_ERRORS,
    ATTACH_BY_VALUE, ATTACH_EMBEDDED_MSG, STGM_WRITE, STGM_TRANSACTED,
    MAPI_UNICODE, MAPI_TO, MAPI_CC, MAPI_BCC, MAPI_E_NOT_FOUND,
    MAPI_E_NOT_ENOUGH_MEMORY, PT_SYSTIME
)
from MAPI.Defs import HrGetOneProp, CHANGE_PROP_TYPE, bin2hex
from MAPI.Struct import (
    SPropValue, MAPIErrorNotFound, MAPIErrorUnknownEntryid,
    MAPIErrorInterfaceNotSupported, MAPIErrorUnconfigured, MAPIErrorNoAccess,
)
from MAPI.Tags import (
    PR_BODY, PR_DISPLAY_NAME_W, PR_MESSAGE_CLASS_W,
    PR_CONTAINER_CLASS, PR_ENTRYID, PR_EC_HIERARCHYID,
    PR_SOURCE_KEY, PR_SUBJECT_W, PR_ATTACH_LONG_FILENAME_W,
    PR_MESSAGE_SIZE, PR_BODY_W, PR_CREATION_TIME,
    PR_MESSAGE_DELIVERY_TIME, PR_LAST_MODIFICATION_TIME,
    PR_MESSAGE_FLAGS, PR_PARENT_ENTRYID, PR_IMPORTANCE,
    PR_ATTACH_NUM, PR_ATTACH_METHOD, PR_ATTACH_DATA_BIN,
    PR_TRANSPORT_MESSAGE_HEADERS_W, PR_ATTACH_DATA_OBJ, PR_ICON_INDEX,
    PR_EC_IMAP_EMAIL, PR_SENTMAIL_ENTRYID, PR_DELETE_AFTER_SUBMIT,
    PR_SENDER_ADDRTYPE_W, PR_SENDER_NAME_W, PR_SENDER_EMAIL_ADDRESS_W,
    PR_SENDER_ENTRYID, PR_SENT_REPRESENTING_ADDRTYPE_W,
    PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W,
    PR_SENT_REPRESENTING_ENTRYID, PR_MESSAGE_RECIPIENTS,
    PR_MESSAGE_ATTACHMENTS, PR_RECIPIENT_TYPE, PR_ADDRTYPE_W,
    PR_EMAIL_ADDRESS_W, PR_SMTP_ADDRESS_W, PR_NULL, PR_HTML,
    PR_RTF_COMPRESSED, PR_SEARCH_KEY, PR_SENDER_SEARCH_KEY,
    PR_START_DATE, PR_END_DATE, PR_OWNER_APPT_ID, PR_RESPONSE_REQUESTED,
    PR_SENT_REPRESENTING_SEARCH_KEY, PR_ATTACHMENT_FLAGS,
    PR_ATTACHMENT_HIDDEN, PR_ATTACHMENT_LINKID, PR_ATTACH_FLAGS,
    PR_NORMALIZED_SUBJECT_W,
)

from MAPI.Tags import IID_IAttachment, IID_IStream, IID_IMAPITable, IID_IMailUser, IID_IMessage

from .compat import (
    hex as _hex, unhex as _unhex, is_str as _is_str, repr as _repr,
    pickle_load as _pickle_load, pickle_loads as _pickle_loads,
    fake_unicode as _unicode, is_file as _is_file,
    encode as _encode
)

from .defs import (
    NAMED_PROPS_ARCHIVER, NAMED_PROP_CATEGORY, ADDR_PROPS,
    PSETID_Archive
)
from .errors import Error, NotFoundError, _DeprecationWarning

from .attachment import Attachment
from .body import Body
from .base import Base
from .recurrence import Recurrence, Occurrence
from .meetingrequest import MeetingRequest
from .address import Address
from .table import Table

if sys.hexversion >= 0x03000000:
    from . import folder as _folder
    from . import store as _store
    from . import user as _user
    from . import utils as _utils
    from . import prop as _prop
else:
    import folder as _folder
    import store as _store
    import user as _user
    import utils as _utils
    import prop as _prop

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

class Item(Base):
    """Item class"""

    def __init__(self, parent=None, eml=None, ics=None, vcf=None, load=None, loads=None, attachments=True, create=False, mapiobj=None, entryid=None, content_flag=None):

        self.emlfile = None
        self._architem = None
        self._folder = None
        self.mapiobj = mapiobj
        self._entryid = entryid
        self._content_flag = content_flag

        if isinstance(parent, _folder.Folder):
            self._folder = parent
            self.store = parent.store
            self.server = parent.server
        elif isinstance(parent, _store.Store):
            self.store = store
            self.server = store.server

        if create:
            self.mapiobj = self.folder.mapiobj.CreateMessage(None, 0)
            self.store = self.folder.store
            self.server = server = self.store.server # XXX

            if _is_file(eml):
                eml = eml.read()
            if _is_file(ics):
                ics = ics.read()
            if _is_file(vcf):
                vcf = vcf.read()

            self.emlfile = eml

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
                vcm = icalmapi.create_vcftomapi(self.mapiobj)
                vcm.parse_vcf(vcf)
                vcm.get_item(self.mapiobj)
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
    def mapiobj(self):
        if not self._mapiobj:
            flags = MAPI_MODIFY | self._content_flag or 0

            self.mapiobj = _utils.openentry_raw(
                self.store.mapiobj, self._entryid, flags
            )

        return self._mapiobj

    @mapiobj.setter
    def mapiobj(self, mapiobj):
        self._mapiobj = mapiobj

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

                try:
                    arch_store = self.server._store2(arch_storeid)
                except MAPIErrorUnconfigured:
                    raise Error('could not open archive store (check SSL settings?)')

                self._architem = arch_store.OpenEntry(item_entryid, None, 0)
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
        except NotFoundError:
            return u''

    @subject.setter
    def subject(self, x):
        self.mapiobj.SetProps([SPropValue(PR_SUBJECT_W, _unicode(x))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def normalized_subject(self):
        """ Normalized item subject or *None* if no subject """

        try:
            return self.prop(PR_NORMALIZED_SUBJECT_W).value
        except NotFoundError:
            return u''

    @property
    def body(self):
        """ Item :class:`body <Body>` """

        warnings.warn('item.body is deprecated', _DeprecationWarning)
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
        except NotFoundError:
            pass

    @property
    def received(self):
        """ Datetime instance with item delivery time """

        try:
            return self.prop(PR_MESSAGE_DELIVERY_TIME).value
        except NotFoundError:
            pass

    @property
    def last_modified(self):
        try:
            return self.prop(PR_LAST_MODIFICATION_TIME).value
        except NotFoundError:
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
        except NotFoundError:
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
        if self._folder:
            return self._folder
        try:
            return _folder.Folder(self.store, _hex(HrGetOneProp(self.mapiobj, PR_PARENT_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def importance(self):
        """ Importance """

        # TODO: userfriendly repr of value
        try:
            return self.prop(PR_IMPORTANCE).value
        except NotFoundError:
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

    @property
    def private(self):
        """ PidLidPrivate - hint to hide the item from other users """

        try:
            return self.prop('common:34054').value
        except NotFoundError:
            return False

    @private.setter
    def private(self, value):
        return self.create_prop('common:34054', value, proptype=PT_BOOLEAN)

    @property
    def reminder(self):
        """ PidLidReminderSet - Specifies whether a reminder is set on the object """
        try:
            return self.prop('common:34051').value
        except NotFoundError:
            return False

    @reminder.setter
    def reminder(self, value):
        return self.create_prop('common:34051', value, proptype=PT_BOOLEAN)

    def attachments(self, embedded=False):
        """ Return item :class:`attachments <Attachment>`

        :param embedded: include embedded attachments
        """

        mapiitem = self._arch_item
        table = Table(
            self.server,
            mapiitem.GetAttachmentTable(MAPI_DEFERRED_ERRORS),
            columns=[PR_ATTACH_NUM, PR_ATTACH_METHOD]
        )

        for row in table.rows():
            if row[1].value == ATTACH_BY_VALUE or (embedded and row[1].value == ATTACH_EMBEDDED_MSG):
                yield Attachment(mapiitem, row[0].value)

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
        stream = attach.OpenProperty(PR_ATTACH_DATA_BIN, IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_MODIFY | MAPI_CREATE)
        stream.Write(data)
        stream.Commit(0)
        attach.SaveChanges(KEEP_OPEN_READWRITE)
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE) # XXX needed?
        return Attachment(mapiobj=attach)

    def header(self, name):
        """ Return transport message header with given name """

        return self.headers().get(name)

    def headers(self):
        """ Return transport message headers """

        try:
            message_headers = self.prop(PR_TRANSPORT_MESSAGE_HEADERS_W)
            headers = email.parser.Parser().parsestr(_encode(message_headers.value), headersonly=True)
            return headers
        except NotFoundError:
            return {}

    @property
    def text(self):
        """ Plain text representation """

        try:
            return _utils.stream(self._arch_item, PR_BODY_W) # under windows them be utf-16le?
        except MAPIErrorNotFound:
            return u''

    @text.setter
    def text(self, x):
        self.create_prop(PR_BODY_W, _unicode(x))

    @property
    def html(self):
        """ HTML representation """

        try:
            return _utils.stream(self._arch_item, PR_HTML)
        except MAPIErrorNotFound:
            return ''

    @html.setter
    def html(self, x):
        self.create_prop(PR_HTML, x)

    @property
    def rtf(self):
        """ RTF representation """

        try:
            return _utils.stream(self._arch_item, PR_RTF_COMPRESSED)
        except MAPIErrorNotFound:
            return ''

    @property
    def body_type(self):
        """ original body type: 'text', 'html', 'rtf' or 'None' if it cannot be determined """
        tag = _prop.bestbody(self.mapiobj)
        if tag == PR_BODY_W:
            return 'text'
        elif tag == PR_HTML:
            return 'html'
        elif tag == PR_RTF_COMPRESSED:
            return 'rtf'

    def eml(self, received_date=False):
        """ Return .eml version of item """
        if self.emlfile is None:
            try:
                self.emlfile = _utils.stream(self.mapiobj, PR_EC_IMAP_EMAIL)
            except MAPIErrorNotFound:
                sopt = inetmapi.sending_options()
                sopt.no_recipients_workaround = True
                sopt.add_received_date = received_date
                self.emlfile = inetmapi.IMToINet(self.store.server.mapisession, None, self.mapiobj, sopt)
        return self.emlfile

    def vcf(self): # XXX don't we have this builtin somewhere? very basic for now
        vic = icalmapi.create_mapitovcf()
        vic.add_message(self.mapiobj)
        return vic.finalize()

    def ics(self, charset="UTF-8"):
        mic = icalmapi.CreateMapiToICal(self.server.ab, charset)
        mic.AddMessage(self.mapiobj, "", 0)
        method, data = mic.Finalize(0)
        return data

    def send(self):
        item = self
        if self.message_class == 'IPM.Appointment':
            # XXX: check if start/end is set
            # XXX: Check if we can copy the calendar item.
            # XXX: Update the calendar item, for tracking status
            # XXX: Set the body of the message like WebApp / OL does.
            item = self.store.outbox.create_item(subject=self.subject, to=self.to, start=self.start, end=self.end, body=self.body)
            # Set meeting request props
            item.message_class = 'IPM.Schedule.Meeting.Request'
            item.create_prop(PR_START_DATE, self.start)
            item.create_prop(PR_END_DATE, self.end)
            item.create_prop(PR_RESPONSE_REQUESTED, True)
            item.private = False
            item.reminder = False
            item.create_prop('appointment:33321', True, PT_BOOLEAN) # PidLidFInvited
            item.create_prop('appointment:33320', self.start, PT_SYSTIME) # basedate????!
            item.create_prop('appointment:33316', False, PT_BOOLEAN) # intendedbusystatus???
            item.create_prop('appointment:33315', False, PT_BOOLEAN) # XXX item.recurring
            item.create_prop('appointment:33303', 3, PT_LONG) # XXX meetingtype?!
            item.create_prop('appointment:33301', False, PT_BOOLEAN) # XXX item.alldayevent?!
            duration = int((self.end - self.start).total_seconds() / 60) # XXX: total time in minutes
            item.create_prop('appointment:33293', self.start, PT_SYSTIME) # XXX start
            item.create_prop('appointment:33294', self.end, PT_SYSTIME) # XXX end
            item.create_prop('appointment:33299', duration, PT_LONG) # XXX item.alldayevent?!
            item.create_prop('appointment:33285', 1, PT_LONG) # XXX busystatus???
            item.create_prop('appointment:33281', 0, PT_LONG) # XXX updatecounter???
            item.create_prop('appointment:33280', 0, PT_BOOLEAN) # XXX sendasical
            item.create_prop(PR_OWNER_APPT_ID, random.randrange(2**32))
            item.create_prop(PR_ICON_INDEX, 1026) # XXX: meeting request icon index.. const?
            goid = self._generate_goid()
            item.create_prop('meeting:3', goid, PT_BINARY)
            item.create_prop('meeting:35', goid, PT_BINARY)

        props = []
        props.append(SPropValue(PR_SENTMAIL_ENTRYID, _unhex(item.folder.store.sentmail.entryid)))
        props.append(SPropValue(PR_DELETE_AFTER_SUBMIT, True))
        item.mapiobj.SetProps(props)
        item.mapiobj.SubmitMessage(0)

    def _generate_goid(self):
        """
        Generate a meeting request Global Object ID.

        The Global Object ID is a MAPI property that any MAPI client uses to
        correlate meeting updates and responses with a particular meeting on
        the calendar. The Global Object ID is the same across all copies of the
        item.

        The Global Object ID consists of the following data:

        * byte array id (16 bytes) - identifiers the BLOB as a GLOBAL Object ID.
        * year (YH + YL) - original year of the instance represented by the
        exception. The value is in big-endian format.
        * M (byte) - original month of the instance represented by the exception.
        * D (byte) - original day of the instance represented by the exception.
        * Creation time - 8 byte date
        * X - reversed byte array of size 8.
        * size - LONG, the length of the data field.
        * data - a byte array (16 bytes) that uniquely identifers the meeting object.
        """

        from MAPI.Time import NANOSECS_BETWEEN_EPOCH

        # XXX: in MAPI.Time?
        def mapi_time(t):
            return int(t) * 10000000 + NANOSECS_BETWEEN_EPOCH

        # byte array id
        classid = '040000008200e00074c5b7101a82e008'
        goid = classid.decode('hex')
        # YEARHIGH, YEARLOW, MONTH, DATE
        goid += struct.pack('>H2B', 0, 0, 0)
        # Creation time, lowdatetime, highdatetime
        now = mapi_time(time.time())
        goid += struct.pack('II', now & 0xffffffff, now >> 32)
        # Reserved, 8 zeros
        goid += struct.pack('L', 0)
        # data size
        goid += struct.pack('I', 16)
        # Unique data
        for _ in range(0, 16):
            goid += struct.pack('B', random.getrandbits(8))
        return goid

    @property
    def sender(self):
        """ Sender :class:`Address` """

        addrprops = (PR_SENDER_ADDRTYPE_W, PR_SENDER_NAME_W, PR_SENDER_EMAIL_ADDRESS_W, PR_SENDER_ENTRYID, PR_SENDER_SEARCH_KEY)
        args = [self.get_prop(p).value if self.get_prop(p) else None for p in addrprops]
        return Address(self.server, *args)

    @property
    def from_(self):
        """ From :class:`Address` """

        addrprops = (PR_SENT_REPRESENTING_ADDRTYPE_W, PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, PR_SENT_REPRESENTING_ENTRYID, PR_SENT_REPRESENTING_SEARCH_KEY)
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
                args = [row[p].value if p in row else None for p in (PR_ADDRTYPE_W, PR_DISPLAY_NAME_W, PR_EMAIL_ADDRESS_W, PR_ENTRYID, PR_SEARCH_KEY)]
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

    @start.setter
    def start(self, val):
        return self.create_prop('common:34070', val, proptype=PT_SYSTIME).value

    @property
    def end(self): # XXX optimize, guid
        return self.prop('common:34071').value

    @end.setter
    def end(self, val):
        return self.create_prop('common:34071', val, proptype=PT_SYSTIME).value

    @property
    def location(self):
        try:
            return self.prop('appointment:33288').value
        except NotFoundError:
            pass

    @property
    def recurring(self):
        return self.prop('appointment:33315').value

    @property
    def recurrence(self):
        return Recurrence(self)

    def occurrences(self, start=None, end=None):
        if self.recurring:
            for occ in self.recurrence.occurrences(start=start, end=end):
                yield occ
        else:
            if (not start or self.end > start) and (not end or self.start < end):
                yield Occurrence(self, max(self.start, start), min(self.end, end))

    @property
    def meetingrequest(self):
        return MeetingRequest(self)

    def _addr_props(self, addr):
        if isinstance(addr, _user.User):
            pr_addrtype = 'ZARAFA'
            pr_dispname = addr.name
            pr_email = addr.email
            pr_entryid = _unhex(addr.userid)
        elif isinstance(addr, Address):
            pr_addrtype = addr.addrtype
            pr_dispname = addr.name
            pr_email = addr.email
            pr_entryid = addr.entryid
        else:
            addr = _unicode(addr)
            pr_addrtype = 'SMTP'
            pr_dispname, pr_email = email.utils.parseaddr(addr)
            pr_dispname = pr_dispname or u'nobody'
            pr_entryid = self.server.ab.CreateOneOff(pr_dispname, u'SMTP', _unicode(pr_email), MAPI_UNICODE)
        return pr_addrtype, pr_dispname, pr_email, pr_entryid

    @to.setter
    def to(self, addrs):
        if _is_str(addrs):
            addrs = _unicode(addrs).split(';')
        elif isinstance(addrs, _user.User):
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

        if isinstance(items, (Attachment, _prop.Property)):
            items = [items]
        else:
            items = list(items)

        attach_ids = [item.number for item in items if isinstance(item, Attachment)]
        proptags = [item.proptag for item in items if isinstance(item, _prop.Property)]
        if proptags:
            self.mapiobj.DeleteProps(proptags)
        for attach_id in attach_ids:
            self._arch_item.DeleteAttach(attach_id, 0, None, 0)

        # XXX: refresh the mapiobj since PR_ATTACH_NUM is updated when opening
        # a message? PR_HASATTACH is also updated by the server.
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def match(self, restriction):
        return restriction.match(self)

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
            key = b'SMTP:' + email_addr.upper().encode('ascii')
            if searchkey in tag_data: # XXX probably need to create, also email
                tag_data[searchkey][1] = key
            else:
                props.append([searchkey, key, None])

    def _dump(self, attachments=True, archiver=True, skip_broken=False):
        # props
        props = []
        tag_data = {}
        bestbody = _prop.bestbody(self.mapiobj)
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
                    try:
                        msg = att.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_DEFERRED_ERRORS | MAPI_MODIFY)
                    except MAPIErrorNoAccess:
                        # XXX the following may fail for embedded items in certain public stores, while
                        # the above does work (opening read-only doesn't work, but read-write works! wut!?)
                        msg = att.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_DEFERRED_ERRORS)
                    item = Item(mapiobj=msg)
                    item.server = self.server # XXX
                    data = item._dump() # recursion
                    atts.append(([[a, b, None] for a, b in row.items()], data))
                elif method == ATTACH_BY_VALUE and attachments:
                    try:
                        data = _utils.stream(att, PR_ATTACH_DATA_BIN)
                    except MAPIErrorNotFound:
                        service = self.server.service
                        log = (service or self.server).log
                        if log:
                            log.warn("no data found for attachment of item with entryid %s" % self.entryid)
                        data = ''
                    atts.append(([[a, b, None] for a, b in row.items()], data))
            except Exception as e: # XXX generalize so usable in more places
                service = self.server.service
                log = (service or self.server).log
                if log:
                    log.error('could not serialize attachment for item with entryid %s' % self.entryid)
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
            if (proptag >> 16) not in (PR_BODY >> 16, PR_HTML >> 16, PR_RTF_COMPRESSED >> 16) or \
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
                stream = attach.OpenProperty(PR_ATTACH_DATA_BIN, IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_MODIFY | MAPI_CREATE)
                stream.Write(data)
                stream.Commit(0)
            attach.SaveChanges(KEEP_OPEN_READWRITE)
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE) # XXX needed?

    def load(self, f, attachments=True):
        self._load(_pickle_load(f), attachments)

    def loads(self, s, attachments=True):
        self._load(_pickle_loads(s), attachments)

    @property
    def embedded(self): # XXX deprecate?
        return self.items().next()

    def create_item(self, message_flags=None):
        """ Create embedded :class:`item <Item>` """

        (id_, attach) = self.mapiobj.CreateAttach(None, 0)

        attach.SetProps([
            SPropValue(PR_ATTACH_METHOD, ATTACH_EMBEDDED_MSG),
            SPropValue(PR_ATTACHMENT_FLAGS, 2),
            SPropValue(PR_ATTACHMENT_HIDDEN, True),
            SPropValue(PR_ATTACHMENT_LINKID, 0),
            SPropValue(PR_ATTACH_FLAGS, 0),
        ])

        msg = attach.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY)
        if message_flags is not None:
            msg.SetProps([SPropValue(PR_MESSAGE_FLAGS, message_flags)])

        msg.SaveChanges(KEEP_OPEN_READWRITE)
        attach.SaveChanges(KEEP_OPEN_READWRITE)
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE) # XXX needed?

        item = Item(mapiobj=msg)
        item.server = self.server
        item._attobj = attach
        return item

    def items(self, recurse=True):
        """ Return all embedded :class:`items <Item>` """

        for row in self.table(PR_MESSAGE_ATTACHMENTS).dict_rows(): # XXX should we use GetAttachmentTable?
            num = row[PR_ATTACH_NUM]
            method = row[PR_ATTACH_METHOD] # XXX default
            if method == ATTACH_EMBEDDED_MSG:
                att = self.mapiobj.OpenAttach(num, IID_IAttachment, 0)
                msg = att.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_MODIFY | MAPI_DEFERRED_ERRORS)

                item = Item(mapiobj=msg)
                item._attobj = att # XXX
                item.server = self.server # XXX
                yield item

                if recurse:
                    for subitem in item.items():
                        yield subitem

    @property
    def primary_item(self):
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_REF_ITEM_ENTRYID = CHANGE_PROP_TYPE(ids[4], PT_BINARY)
        entryid = HrGetOneProp(self.mapiobj, PROP_REF_ITEM_ENTRYID).Value

        try:
            return self.folder.primary_store.item(entryid=codecs.encode(entryid, 'hex'))
        except NotFoundError:
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
        at = Table(
            self.server, mapiobj.GetAttachmentTable(0),
            columns=[PR_ATTACH_NUM]
        )
        for row in at.rows():
            mapiobj.DeleteAttach(row[0].value, 0, None, 0)

        # copy contents into it
        exclude_props = [PROP_REF_STORE_ENTRYID, PROP_REF_ITEM_ENTRYID, PROP_REF_PREV_ENTRYID, PROP_FLAGS]
        self.mapiobj.CopyTo(None, exclude_props, 0, None, IID_IMessage, mapiobj, 0)

        mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

        # update backref
        if new:
            entryid = HrGetOneProp(mapiobj, PR_ENTRYID).Value
            self.mapiobj.SetProps([SPropValue(PROP_REF_ITEM_ENTRYID, entryid)])
            self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def copy(self, folder, _delete=False):
        """ Copy item to folder; return copied item

        :param folder: target folder
        """

        mapiobj = folder.mapiobj.CreateMessage(None, 0)
        self.mapiobj.CopyTo([], [], 0, None, IID_IMessage, mapiobj, 0)
        if _delete:
            self.folder.delete(self)
        item = Item(mapiobj=mapiobj)
        item.store = self.store # XXX
        item.server = self.server
        return item

    def move(self, folder):
        """ Move item to folder; return moved item

        :param folder: target folder
        """

        return self.copy(folder, _delete=True)

    def __eq__(self, i): # XXX check same store?
        if isinstance(i, Item):
            return self.entryid == i.entryid
        return False

    def __ne__(self, i):
        return not self == i

    def __iter__(self):
        return self.props()

    def __unicode__(self):
        return u'Item(%s)' % self.subject

