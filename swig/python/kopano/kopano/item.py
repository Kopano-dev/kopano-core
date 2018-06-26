"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import datetime
import email.parser as email_parser
import email.utils as email_utils
import functools
import os
import sys
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
    CLEAR_READ_FLAG, PT_MV_UNICODE, PT_BINARY, PT_LONG,
    MAPI_CREATE, MAPI_MODIFY, MAPI_DEFERRED_ERRORS,
    ATTACH_BY_VALUE, ATTACH_EMBEDDED_MSG, STGM_WRITE, STGM_TRANSACTED,
    MAPI_UNICODE, MAPI_TO, MAPI_CC, MAPI_BCC, MAPI_E_NOT_FOUND,
    MAPI_E_NOT_ENOUGH_MEMORY, PT_SYSTIME, MAPI_ASSOCIATED,
    WrapCompressedRTFStream, MODRECIP_ADD, MODRECIP_REMOVE,
    RELOP_EQ
)

from MAPI.Defs import (
    HrGetOneProp, CHANGE_PROP_TYPE
)

from MAPI.Struct import (
    SPropValue, MAPIErrorNotFound, MAPIErrorUnknownEntryid,
    MAPIErrorInterfaceNotSupported, MAPIErrorUnconfigured, MAPIErrorNoAccess,
    MAPIErrorNoRecipients, SPropertyRestriction
)

from MAPI.Tags import (
    PR_BODY, PR_DISPLAY_NAME_W, PR_MESSAGE_CLASS_W, PR_CHANGE_KEY,
    PR_CONTAINER_CLASS_W, PR_ENTRYID, PR_EC_HIERARCHYID, PR_HASATTACH,
    PR_SOURCE_KEY, PR_SUBJECT_W, PR_ATTACH_LONG_FILENAME_W,
    PR_MESSAGE_SIZE, PR_BODY_W, PR_CREATION_TIME, PR_CLIENT_SUBMIT_TIME,
    PR_MESSAGE_DELIVERY_TIME, PR_LAST_MODIFICATION_TIME,
    PR_MESSAGE_FLAGS, PR_PARENT_ENTRYID, PR_IMPORTANCE,
    PR_ATTACH_NUM, PR_ATTACH_METHOD, PR_ATTACH_DATA_BIN,
    PR_TRANSPORT_MESSAGE_HEADERS, PR_ATTACH_DATA_OBJ, PR_ICON_INDEX,
    PR_EC_IMAP_EMAIL, PR_SENTMAIL_ENTRYID, PR_DELETE_AFTER_SUBMIT,
    PR_SENDER_ADDRTYPE_W, PR_SENDER_NAME_W, PR_SENDER_EMAIL_ADDRESS_W,
    PR_SENDER_ENTRYID, PR_SENT_REPRESENTING_ADDRTYPE_W,
    PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W,
    PR_SENT_REPRESENTING_ENTRYID, PR_MESSAGE_RECIPIENTS,
    PR_MESSAGE_ATTACHMENTS, PR_RECIPIENT_TYPE, PR_ADDRTYPE_W,
    PR_EMAIL_ADDRESS_W, PR_SMTP_ADDRESS_W, PR_NULL, PR_HTML,
    PR_RTF_COMPRESSED, PR_SENDER_SEARCH_KEY,
    PR_START_DATE, PR_END_DATE, PR_OWNER_APPT_ID, PR_RESPONSE_REQUESTED,
    PR_SENT_REPRESENTING_SEARCH_KEY, PR_ATTACHMENT_FLAGS,
    PR_ATTACHMENT_HIDDEN, PR_ATTACHMENT_LINKID, PR_ATTACH_FLAGS,
    PR_NORMALIZED_SUBJECT_W, PR_INTERNET_MESSAGE_ID_W, PR_CONVERSATION_ID,
    PR_READ_RECEIPT_REQUESTED, PR_ORIGINATOR_DELIVERY_REPORT_REQUESTED,
    PR_REPLY_RECIPIENT_ENTRIES, PR_EC_BODY_FILTERED, PR_SENSITIVITY,
    PR_SEARCH_KEY, PR_LAST_VERB_EXECUTED, PR_LAST_VERB_EXECUTION_TIME,
    PR_ROWID
)

from MAPI.Tags import (
    IID_IAttachment, IID_IStream, IID_IMAPITable, IID_IMailUser, IID_IMessage,
)

from .pidlid import (
    PidLidAppointmentStateFlags, PidLidCleanGlobalObjectId,
    PidLidAppointmentStartWhole, PidLidAppointmentEndWhole
)

from .compat import (
    is_str as _is_str, pickle_load as _pickle_load,
    pickle_loads as _pickle_loads, fake_unicode as _unicode,
    is_file as _is_file, benc as _benc, bdec as _bdec,
    default as _default,
)

from .defs import (
    NAMED_PROPS_ARCHIVER, NAMED_PROP_CATEGORY, ADDR_PROPS,
    PSETID_Archive, URGENCY, REV_URGENCY, ASF_MEETING, ASF_RECEIVED,
    ASF_CANCELED
)
from .errors import (
    Error, NotFoundError, _DeprecationWarning
)

from .attachment import Attachment
from .properties import Properties
from .recurrence import Occurrence
from .restriction import Restriction
from .meetingrequest import MeetingRequest, _copytags, _create_meetingrequest
from .address import Address
from .table import Table
from .contact import Contact
from .appointment import Appointment

if sys.hexversion >= 0x03000000:
    try:
        from . import folder as _folder
    except ImportError: # pragma: no cover
        _folder = sys.modules[__package__+'.folder']
    try:
        from . import store as _store
    except ImportError: # pragma: no cover
        _store = sys.modules[__package__ + '.store']
    try:
        from . import user as _user
    except ImportError: # pragma: no cover
        _user = sys.modules[__package__ + '.user']
    try:
        from . import utils as _utils
    except ImportError: # pragma: no cover
        _utils = sys.modules[__package__ + '.utils']
    from . import property_ as _prop
else: # pragma: no cover
    import folder as _folder
    import store as _store
    import user as _user
    import utils as _utils
    import property_ as _prop


class PersistentList(list):
    def __init__(self, mapiobj, proptag, *args, **kwargs):
        self.mapiobj = mapiobj
        self.proptag = proptag
        for attr in ('append', 'extend', 'insert', 'pop', 'remove', 'reverse', 'sort'):
            setattr(self, attr, self._autosave(getattr(self, attr)))
        list.__init__(self, *args, **kwargs)

    def _autosave(self, func):
        @functools.wraps(func)
        def _func(*args, **kwargs):
            ret = func(*args, **kwargs)
            data = [_unicode(x) for x in self]
            self.mapiobj.SetProps([SPropValue(self.proptag, data)])
            _utils._save(self.mapiobj)
            return ret
        return _func

class Item(Properties, Contact, Appointment):
    """Item class"""

    def __init__(self, folder=None, eml=None, ics=None, vcf=None, load=None,
                 loads=None, attachments=True, create=False, mapiobj=None,
                 entryid=None, content_flag=None, cache={}, save=True):
        self._eml = None
        self._architem = None
        self._folder = None
        self.mapiobj = mapiobj
        self._entryid = entryid
        self._content_flag = content_flag or 0
        self._cache = cache

        if folder:
            self._folder = folder
            self.store = folder.store
            self.server = folder.server

        if create:
            self.mapiobj = self.folder.mapiobj.CreateMessage(None, MAPI_ASSOCIATED if self.folder.content_flag & MAPI_ASSOCIATED else 0)
            self.store = self.folder.store
            self.server = server = self.store.server # XXX

            if _is_file(eml):
                eml = eml.read()
            if _is_file(ics):
                ics = ics.read()
            if _is_file(vcf):
                vcf = vcf.read()

            self._eml = eml

            if eml is not None:
                # options for CreateMessage: 0 / MAPI_ASSOCIATED
                dopt = inetmapi.delivery_options()
                inetmapi.IMToMAPI(server.mapisession, self.folder.store.mapiobj, None, self.mapiobj, self._eml, dopt)
                pass
            elif ics is not None:
                icm = icalmapi.CreateICalToMapi(self.mapiobj, server.ab, False)
                icm.ParseICal(ics, 'utf-8', 'UTC', self.user.mapiobj, 0)
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
                    container_class = HrGetOneProp(self.folder.mapiobj, PR_CONTAINER_CLASS_W).Value
                except MAPIErrorNotFound:
                    self.mapiobj.SetProps([SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Note')])
                else:
                    if container_class == 'IPF.Contact': # XXX just skip first 4 chars?
                        self.mapiobj.SetProps([SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Contact')]) # XXX set default props
                    elif container_class == 'IPF.Appointment':
                        self.mapiobj.SetProps([SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Appointment')]) # XXX set default props
                        self.from_ = self.store.user
                        self[PidLidAppointmentStateFlags] = 1
            if save:
                _utils._save(self.mapiobj)

    @property
    def mapiobj(self):
        if not self._mapiobj:
            self.mapiobj = _utils.openentry_raw(self.store.mapiobj, self._entryid, self._content_flag)
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
        if self._entryid:
            return _benc(self._entryid)

        return _benc(self._get_fast(PR_ENTRYID, must_exist=True))

    @property
    def guid(self):
        """ Item guid """

        return self.entryid[56:88]

    @property
    def hierarchyid(self):
        return HrGetOneProp(self.mapiobj, PR_EC_HIERARCHYID).Value

    @property
    def sourcekey(self):
        """ Item sourcekey """

        if not hasattr(self, '_sourcekey'): # XXX more general caching solution
            self._sourcekey = _benc(self[PR_SOURCE_KEY])
        return self._sourcekey

    @property
    def searchkey(self):
        """ Item searchkey """

        if not hasattr(self, '_searchkey'): # XXX more general caching solution
            self._searchkey = _benc(HrGetOneProp(self.mapiobj, PR_SEARCH_KEY).Value)
        return self._searchkey

    @property
    def changekey(self):
        """ Item changekey """

        return _benc(self._get_fast(PR_CHANGE_KEY))

    @property
    def subject(self):
        """ Item subject """

        return self._get_fast(PR_SUBJECT_W, _default(u''))

    @subject.setter
    def subject(self, x):
        if x is None:
            prop = self.get_prop(PR_SUBJECT_W)
            if prop:
                self.delete(prop)
        else:
            self._set_fast(PR_SUBJECT_W, _unicode(x))

    @property
    def name(self):
        """ Item (display) name """

        try:
            return self.prop(PR_DISPLAY_NAME_W).value
        except NotFoundError:
            return u''

    @name.setter
    def name(self, x):
        self.mapiobj.SetProps([SPropValue(PR_DISPLAY_NAME_W, _unicode(x))])
        _utils._save(self.mapiobj)

    @property
    def normalized_subject(self):
        """ Normalized item subject """

        try:
            return self.prop(PR_NORMALIZED_SUBJECT_W).value
        except NotFoundError:
            return u''

    @property
    def body_preview(self):
        """ Item body preview (plaintext, up to 255 characters) """

        text = self._get_fast(PR_BODY_W, capped=True)
        if text:
            text = text[:255]
        return text

    @property
    def size(self):
        """ Item size """

        return self.prop(PR_MESSAGE_SIZE).value

    @property
    def message_class(self):
        return self._get_fast(PR_MESSAGE_CLASS_W, must_exist=True)

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
        _utils._save(self.mapiobj)

    @property
    def created(self):
        """Creation time."""
        try:
            return self.prop(PR_CREATION_TIME).value
        except NotFoundError:
            pass

    @property
    def received(self):
        """Delivery time."""
        return self._get_fast(PR_MESSAGE_DELIVERY_TIME)

    @received.setter
    def received(self, value):
        self._cache.pop(PR_MESSAGE_DELIVERY_TIME, None) # TODO generalize
        self.prop(PR_MESSAGE_DELIVERY_TIME, create=True).value = value

    @property
    def sent(self):
        """Client submit time."""
        try:
            return self.prop(PR_CLIENT_SUBMIT_TIME).value
        except NotFoundError:
            pass

    @property
    def last_modified(self):
        """Last modification time."""
        return self._get_fast(PR_LAST_MODIFICATION_TIME)

    @property
    def stubbed(self):
        """Item is stubbed by archiver."""
        # TODO caching folder.GetIDs..?
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0)
        PROP_STUBBED = CHANGE_PROP_TYPE(ids[2], PT_BOOLEAN)
        try:
            # False means destubbed
            return HrGetOneProp(self.mapiobj, PROP_STUBBED).Value
        except MAPIErrorNotFound:
            return False

    # TODO draft status, how to determine? GetMessageStatus?

    @property
    def read(self):
        """Item is read."""
        return self.prop(PR_MESSAGE_FLAGS).value & MSGFLAG_READ > 0

    @read.setter
    def read(self, value):
        if value:
            self.mapiobj.SetReadFlag(0)
        else:
            self.mapiobj.SetReadFlag(CLEAR_READ_FLAG)

    @property
    def read_receipt(self):
        """A read receipt was requested."""
        return self.get(PR_READ_RECEIPT_REQUESTED, False)

    @read_receipt.setter
    def read_receipt(self, value):
        self.create_prop(PR_READ_RECEIPT_REQUESTED, value)

    @property
    def delivery_receipt(self):
        """A delivery receipt was requested."""
        return self.get(PR_ORIGINATOR_DELIVERY_REPORT_REQUESTED, False)

    @property
    def categories(self):
        """Categories."""
        proptag = self.mapiobj.GetIDsFromNames([NAMED_PROP_CATEGORY], MAPI_CREATE)[0]
        proptag = CHANGE_PROP_TYPE(proptag, PT_MV_UNICODE)
        try:
            value = self.prop(proptag).value
        except NotFoundError:
            value = []
        return PersistentList(self.mapiobj, proptag, value)

    @categories.setter
    def categories(self, value):
        proptag = self.mapiobj.GetIDsFromNames([NAMED_PROP_CATEGORY], MAPI_CREATE)[0]
        proptag = CHANGE_PROP_TYPE(proptag, PT_MV_UNICODE)
        data = [_unicode(x) for x in value]
        self.mapiobj.SetProps([SPropValue(proptag, data)])
        _utils._save(self.mapiobj)

    @property
    def folder(self):
        """ Parent :class:`Folder` of an item """
        if self._folder:
            return self._folder
        try:
            return _folder.Folder(self.store, _benc(HrGetOneProp(self.mapiobj, PR_PARENT_ENTRYID).Value))
        except (MAPIErrorNotFound, NotFoundError):
            pass

    @property
    def importance(self):
        """ Importance """

        warnings.warn('item.importance is deprecated (use item.urgency)', _DeprecationWarning)
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

        warnings.warn('item.importance is deprecated (use item.urgency)', _DeprecationWarning)
        self.mapiobj.SetProps([SPropValue(PR_IMPORTANCE, value)])
        _utils._save(self.mapiobj)

    @property
    def urgency(self): # TODO rename back to 'importance' with core 9?
        """Urgency ('low', 'normal' or 'high')"""
        try:
            return URGENCY[self[PR_IMPORTANCE]]
        except NotFoundError:
            return u'normal'

    # TODO urgency setter
    @urgency.setter
    def urgency(self, value):
        self.create_prop(PR_IMPORTANCE, REV_URGENCY[value])

    @property
    def user(self):
        return self.store.user

    @property
    def sensitivity(self):
        """Sensitivity ('normal', 'personal', 'private' or 'confidential')."""
        try:
            return {
                0: u'normal',
                1: u'personal',
                2: u'private',
                3: u'confidential',
            }[self[PR_SENSITIVITY]]
        except NotFoundError:
            return u'normal'

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

    @property
    def messageid(self):
        return self.get(PR_INTERNET_MESSAGE_ID_W)

    @property
    def conversationid(self):
        try:
            return _benc(self[PR_CONVERSATION_ID])
        except NotFoundError:
            pass

    def attachments(self, embedded=False, page_start=None, page_limit=None,
                    order=None):
        """ Return item :class:`attachments <Attachment>`

        :param embedded: include embedded attachments
        """

        mapiitem = self._arch_item
        table = Table(
            self.server,
            self.mapiobj,
            mapiitem.GetAttachmentTable(MAPI_DEFERRED_ERRORS),
            columns=[PR_ATTACH_NUM, PR_ATTACH_METHOD],
        )

        for row in table.rows():
            if row[1].value == ATTACH_BY_VALUE or (embedded and row[1].value == ATTACH_EMBEDDED_MSG):
                yield Attachment(self, mapiitem, row[0].value)

    def attachment(self, entryid):
        # TODO can we use something existing for entryid, like PR_ENTRYID? check exchange?

        # TODO performance, restriction on PR_RECORD_KEY part?
        for attachment in self.attachments(embedded=True):
            if attachment.entryid == entryid:
                return attachment
        else:
            raise NotFoundError("no attachment with entryid '%s'" % entryid)

    def create_attachment(self, name=None, data=None, filename=None, **kwargs):
        """Create a new attachment

        :param name: the attachment name
        :param data: string containing the attachment data
        :param filename: string
        """

        if filename:
            data = open(filename, 'rb').read()
            name = os.path.basename(filename)

        (id_, attach) = self.mapiobj.CreateAttach(None, 0)
        name = _unicode(name)
        props = [SPropValue(PR_ATTACH_LONG_FILENAME_W, name), SPropValue(PR_ATTACH_METHOD, ATTACH_BY_VALUE)]
        attach.SetProps(props)
        stream = attach.OpenProperty(PR_ATTACH_DATA_BIN, IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_MODIFY | MAPI_CREATE)
        stream.Write(data)
        stream.Commit(0)

        _utils._save(attach)
        _utils._save(self.mapiobj) # XXX needed?

        att = Attachment(self, mapiitem=self.mapiobj, mapiobj=attach)

        for key, val in kwargs.items():
            setattr(att, key, val)

        return att

    @property
    def has_attachments(self):
        return self.get(PR_HASATTACH, False)

    def header(self, name):
        """ Return transport message header with given name """

        return self.headers().get(name)

    def headers(self):
        """ Return transport message headers """

        try:
            message_headers = self.prop(PR_TRANSPORT_MESSAGE_HEADERS)
            if sys.hexversion >= 0x03000000:
                headers = email_parser.BytesParser().parsebytes(message_headers.value, headersonly=True)
            else:
                headers = email_parser.Parser().parsestr(message_headers.value, headersonly=True)
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
            return b''

    @html.setter
    def html(self, x):
        self.create_prop(PR_HTML, x)

    @property
    def filtered_html(self):
        """ Filtered HTML representation """
        try:
            return self.prop(PR_EC_BODY_FILTERED).value
        except NotFoundError:
            pass

    @property
    def rtf(self):
        """ RTF representation """

        try:
            return _utils.stream(self._arch_item, PR_RTF_COMPRESSED)
        except MAPIErrorNotFound:
            return b''

    @rtf.setter
    def rtf(self, x):
        stream = self._arch_item.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_MODIFY | MAPI_CREATE)
        uncompressed = WrapCompressedRTFStream(stream, MAPI_MODIFY)
        uncompressed.Write(x)
        uncompressed.Commit(0)
        stream.Commit(0)

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

    def _generate_eml(self, received_date):
        sopt = inetmapi.sending_options()
        sopt.no_recipients_workaround = True
        sopt.add_received_date = received_date
        return inetmapi.IMToINet(self.store.server.mapisession, None, self.mapiobj, sopt)

    def eml(self, received_date=False, stored=True):
        """ convert the object to a RFC 2822 mail

        :param received_date: add delivery date as received date
        :param stored: use the stored PR_EC_IMAP_EMAIL instead of calling inetmapi to convert
        """
        if not stored:
            return self._generate_eml(received_date)

        if self._eml is None:
            try:
                self._eml = _utils.stream(self.mapiobj, PR_EC_IMAP_EMAIL)
            except MAPIErrorNotFound:
                self._eml = self._generate_eml(received_date)
        return self._eml

    def vcf(self): # XXX don't we have this builtin somewhere? very basic for now
        vic = icalmapi.create_mapitovcf()
        vic.add_message(self.mapiobj)
        return vic.finalize()

    def ics(self, charset="UTF-8"):
        mic = icalmapi.CreateMapiToICal(self.server.ab, charset)
        mic.AddMessage(self.mapiobj, "", 0)
        _, data = mic.Finalize(0)
        return data

    def _generate_reply_body(self):
        """Create a reply body"""
        # TODO(jelle): HTML formatted text support.
        body = u"\r\n".join(u"> " + line for line in self.text.split('\r\n'))

        if self.sent and self.from_:
            body = u"\r\nOn {} at {}, {} wrote:\r\n{}".format(
                self.sent.strftime('%d-%m-%Y'),
                self.sent.strftime('%H:%M'),
                self.from_.name,
                body)

        return body

    def _create_source_message_info(self, action):
        """Create source message info, value of the record, based on action
        type (reply, replyall, forward) and add 48byte entryid at the end.

        :param action: the reply action
        :type action: str
        :return:
        :rtype: str
        """

        id_map = {
            'reply': '0501000066000000',
            'replyall': '0501000067000000',
            'forward': '0601000068000000',
        }

        try:
            return "01000E000C000000{0}0200000030000000{1}".format(id_map[action], self.entryid)
        except KeyError:
            return ""

    def reply(self, folder=None, all=False):
        # TODO(jelle): support reply all.
        if not folder:
            folder = self.store.drafts
        # TODO(jelle): Remove multiple Re:'s, should only be one Re: <subject> left
        subject = 'Re: {}'.format(self.subject)
        body = self._generate_reply_body()
        # TODO(jelle): pass an Address in to?
        item = folder.create_item(subject=subject, body=body, from_=self.store.user,
                                  to=self.from_.email, message_class=self.message_class)

        source_message_info = self._create_source_message_info('reply').encode('ascii')
        item.create_prop('common:0x85CE', source_message_info, PT_BINARY)

        return item

    def send(self, copy_to_sentmail=True, cancel=False):
        item = self
        if self.message_class in (
            'IPM.Appointment',
            'IPM.OLE.CLASS.{00061055-0000-0000-C000-000000000046}' # exception message
        ):
            if (self.get(PidLidAppointmentStartWhole) is None or \
                self.get(PidLidAppointmentEndWhole) is None):
                raise Error('appointment requires start and end date')

            item = _create_meetingrequest(self, cancel=cancel)

        icon_index = {
            b'66': 261,  # reply
            b'67': 261,  # reply all
            b'68': 262,  # forward
        }
        # TOOD(jelle): check if property exists?
        try:
            source_message = item.prop('common:0x85CE').value
            msgtype = source_message[24:26]
            orig = self.store.item(entryid=source_message[48:])
            orig.create_prop(PR_ICON_INDEX, icon_index[msgtype])
            orig.create_prop(PR_LAST_VERB_EXECUTED, int(msgtype, 16))
            orig.create_prop(PR_LAST_VERB_EXECUTION_TIME, datetime.datetime.now())
        except NotFoundError:
            pass

        props = []

        if copy_to_sentmail:
            props.append(SPropValue(PR_SENTMAIL_ENTRYID, _bdec(item.store.sentmail.entryid)))
        props.append(SPropValue(PR_DELETE_AFTER_SUBMIT, True))
        item.mapiobj.SetProps(props)

        try:
            item.mapiobj.SubmitMessage(0)
        except MAPIErrorNoRecipients:
            if self.message_class != 'IPM.Appointment':
                raise Error('cannot send item without recipients')

    @property
    def sender(self):
        """ Sender :class:`Address` """

        addrprops = (PR_SENDER_ADDRTYPE_W, PR_SENDER_NAME_W, PR_SENDER_EMAIL_ADDRESS_W, PR_SENDER_ENTRYID, PR_SENDER_SEARCH_KEY)
        args = [self.get_prop(p).value if self.get_prop(p) else None for p in addrprops]
        return Address(self.server, *args)

    @sender.setter
    def sender(self, addr):
        pr_addrtype, pr_dispname, pr_email, pr_entryid = self._addr_props(addr)
        self.mapiobj.SetProps([
            SPropValue(PR_SENDER_ADDRTYPE_W, _unicode(pr_addrtype)), # XXX pr_addrtype should be unicode already
            SPropValue(PR_SENDER_NAME_W, pr_dispname),
            SPropValue(PR_SENDER_EMAIL_ADDRESS_W, pr_email),
            SPropValue(PR_SENDER_ENTRYID, pr_entryid),
        ])
        _utils._save(self.mapiobj)

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

        # XXX Hack around missing sender
        if not self.sender.email:
            self.sender = addr

        _utils._save(self.mapiobj)

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

    def tables(self):
        yield self.table(PR_MESSAGE_RECIPIENTS)
        yield self.table(PR_MESSAGE_ATTACHMENTS)

    def recipients(self, _type=None):
        """ Return recipient :class:`addresses <Address>` """

        for row in self.table(PR_MESSAGE_RECIPIENTS):
            row2 = dict([(x.proptag, x) for x in row])
            if not _type or (PR_RECIPIENT_TYPE in row2 and row2[PR_RECIPIENT_TYPE].value == _type):
                args = [row2[p].value if p in row2 else None for p in (PR_ADDRTYPE_W, PR_DISPLAY_NAME_W, PR_EMAIL_ADDRESS_W, PR_ENTRYID, PR_SEARCH_KEY)]
                yield Address(self.server, *args, props=row)

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
    def replyto(self):
        try:
            entries = self[PR_REPLY_RECIPIENT_ENTRIES]
        except NotFoundError:
            return

        # specs say addressbook eids, but they are one-offs?
        # TODO use ECParseOneOff
        count = _utils.unpack_long(entries, 0)
        pos = 8
        for i in range(count):
            size = _utils.unpack_long(entries, pos)
            content = entries[pos + 4 + 24:pos + 4 + 24 + size]

            start = sos = 0
            info = []
            while True:
                if content[sos:sos + 2] == b'\x00\x00':
                    info.append(content[start:sos].decode('utf-16-le'))
                    if len(info) == 3:
                        yield Address(self.server, info[1], info[0], info[2])
                        break
                    start = sos + 2
                sos += 2

            pos += 4 + size

    @property
    def meetingrequest(self):
        return MeetingRequest(self)

    def _addr_props(self, addr):
        if isinstance(addr, _user.User):
            pr_addrtype = 'ZARAFA'
            pr_dispname = addr.name
            pr_email = addr.email
            pr_entryid = _bdec(addr.userid)
        elif isinstance(addr, Address):
            pr_addrtype = addr.addrtype
            pr_dispname = addr.name
            pr_email = addr.email
            pr_entryid = addr.entryid
        else:
            addr = _unicode(addr)
            pr_addrtype = 'SMTP'
            pr_dispname, pr_email = email_utils.parseaddr(addr)
            pr_dispname = pr_dispname or addr
            pr_entryid = self.server.ab.CreateOneOff(pr_dispname, u'SMTP', _unicode(pr_email), MAPI_UNICODE)
        return pr_addrtype, pr_dispname, pr_email, pr_entryid

    def _recipients(self, reciptype, addrs):
        """Sets the recipient table according to the type and given addresses.

        Since the recipienttable contains to, cc and bcc addresses, and item.to
        = [] should only set the to recipients (not append), the to recipients
        are first removed and then added again. This approach works but seems
        suboptimal, since it queries the MAPI_TO items, removes them and adds
        the new entries while leaving the other recipients intact.

        :param reciptype: MAPI_TO, MAPI_CC or MAPI_BCC
        :param addrs: list of addresses to set.
        """

        restriction = Restriction(SPropertyRestriction(RELOP_EQ,
                                                       PR_RECIPIENT_TYPE,
                                                       SPropValue(PR_RECIPIENT_TYPE, reciptype)))
        table = self.table(PR_MESSAGE_RECIPIENTS, columns=[PR_ROWID], restriction=restriction)
        rows = [[SPropValue(PR_ROWID, row[PR_ROWID])] for row in table.dict_rows()]
        if rows:
            self.mapiobj.ModifyRecipients(MODRECIP_REMOVE, rows)

        if _is_str(addrs):
            addrs = [x.strip() for x in _unicode(addrs).split(';')]
        elif isinstance(addrs, _user.User):
            addrs = [addrs]
        names = []
        for addr in addrs:
            pr_addrtype, pr_dispname, pr_email, pr_entryid = self._addr_props(addr)
            names.append([
                SPropValue(PR_RECIPIENT_TYPE, reciptype),
                SPropValue(PR_DISPLAY_NAME_W, pr_dispname),
                SPropValue(PR_ADDRTYPE_W, _unicode(pr_addrtype)),
                SPropValue(PR_EMAIL_ADDRESS_W, _unicode(pr_email)),
                SPropValue(PR_ENTRYID, pr_entryid),
            ])
        self.mapiobj.ModifyRecipients(MODRECIP_ADD, names)
        _utils._save(self.mapiobj) # XXX needed?

    @to.setter
    def to(self, addrs):
        self._recipients(MAPI_TO, addrs)

    @cc.setter
    def cc(self, addrs):
        self._recipients(MAPI_CC, addrs)

    @bcc.setter
    def bcc(self, addrs):
        self._recipients(MAPI_BCC, addrs)

    def delete(self, objects):
        """Delete properties or attachments from item.

        :param objects: The object(s) to delete
        """
        objects = _utils.arg_objects(objects, (Attachment, _prop.Property, Occurrence), 'Item.delete')
        # XXX embedded items?

        attach_ids = [item.number for item in objects if isinstance(item, Attachment)]
        proptags = [item.proptag for item in objects if isinstance(item, _prop.Property)]
        occs = [item for item in objects if isinstance(item, Occurrence)]
        for occ in occs:
            self.recurrence._delete_exception(occ.start, self, _copytags(self.mapiobj))
        if proptags:
            self.mapiobj.DeleteProps(proptags)
        for attach_id in attach_ids:
            self._arch_item.DeleteAttach(attach_id, 0, None, 0)

        # XXX: refresh the mapiobj since PR_ATTACH_NUM is updated when opening
        # a message? PR_HASATTACH is also updated by the server.
        _utils._save(self.mapiobj)

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

    def _dump(self, attachments=True, archiver=True, skip_broken=False, _main_item=None):
        _main_item = _main_item or self

        # props
        props = []
        tag_data = {}
        bestbody = _prop.bestbody(self.mapiobj)
        for prop in self.props():
            if (bestbody != PR_NULL and prop.proptag in (PR_BODY_W, PR_HTML, PR_RTF_COMPRESSED) and prop.proptag != bestbody):
                continue
            if prop.named: # named prop: prop.id_ system dependent...
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
                method = row.get(PR_ATTACH_METHOD, ATTACH_BY_VALUE)
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
                    data = item._dump(_main_item=_main_item) # recursion
                    atts.append(([[a, b, None] for a, b in row.items()], data))
                elif method == ATTACH_BY_VALUE and attachments:
                    try:
                        data = _utils.stream(att, PR_ATTACH_DATA_BIN)
                    except MAPIErrorNotFound:
                        service = self.server.service
                        log = (service or self.server).log
                        if log:
                            log.warn("no data found for attachment of item with entryid %s" % _main_item.entryid)
                        data = ''
                    atts.append(([[a, b, None] for a, b in row.items()], data))
            except Exception as e: # XXX generalize so usable in more places
                service = self.server.service
                log = (service or self.server).log
                if log:
                    log.error('could not serialize attachment for item with entryid %s' % _main_item.entryid)
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
        item = Item(mapiobj=self._arch_item) # TODO make configurable?
        item.server = self.server
        pickle.dump(item._dump(attachments=attachments, archiver=archiver), f, protocol=2)

    def dumps(self, attachments=True, archiver=True, skip_broken=False):
        item = Item(mapiobj=self._arch_item) # TODO make configurable?
        item.server = self.server
        return pickle.dumps(item._dump(attachments=attachments, archiver=archiver, skip_broken=skip_broken), protocol=2)

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
        recipients = [[SPropValue(proptag_, value) for (proptag_, value, nameid) in row] for row in d[b'recipients']]
        self.mapiobj.ModifyRecipients(0, recipients)

        # attachments
        for props, data in d[b'attachments']:
            if attachments or isinstance(data, dict):
                props = [SPropValue(proptag_, value) for (proptag_, value, nameid) in props]
                (id_, attach) = self.mapiobj.CreateAttach(None, 0)
                attach.SetProps(props)
                if isinstance(data, dict): # embedded message
                    msg = attach.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY)
                    item = Item(mapiobj=msg)
                    item._load(data, attachments) # recursion
                else:
                    stream = attach.OpenProperty(PR_ATTACH_DATA_BIN, IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_MODIFY | MAPI_CREATE)
                    stream.Write(data)
                    stream.Commit(0)
                _utils._save(attach)

        _utils._save(self.mapiobj) # XXX needed?

    def load(self, f, attachments=True):
        self._load(_pickle_load(f), attachments)

    def loads(self, s, attachments=True):
        self._load(_pickle_loads(s), attachments)

    @property
    def embedded(self): # XXX deprecate?
        return next(self.items())

    def create_item(self, message_flags=None, hidden=False, **kwargs):
        """ Create embedded :class:`item <Item>` """

        (id_, attach) = self.mapiobj.CreateAttach(None, 0)

        attach.SetProps([
            SPropValue(PR_ATTACH_METHOD, ATTACH_EMBEDDED_MSG),
            SPropValue(PR_ATTACHMENT_FLAGS, 2),
            SPropValue(PR_ATTACHMENT_HIDDEN, hidden),
            SPropValue(PR_ATTACHMENT_LINKID, 0),
            SPropValue(PR_ATTACH_FLAGS, 0),
        ])
        if kwargs.get('subject'):
            attach.SetProps([
                SPropValue(PR_DISPLAY_NAME_W, kwargs.get('subject')),
            ])

        msg = attach.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY)
        if message_flags is not None:
            msg.SetProps([SPropValue(PR_MESSAGE_FLAGS, message_flags)])

        item = Item(mapiobj=msg)
        item.server = self.server
        item._attobj = attach

        for key, val in kwargs.items():
            setattr(item, key, val)

        _utils._save(msg)
        _utils._save(attach)
        _utils._save(self.mapiobj) # XXX needed?

        return item

    def items(self, recurse=True):
        """ Return all embedded :class:`items <Item>` """

        for row in self.table(PR_MESSAGE_ATTACHMENTS).dict_rows(): # XXX should we use GetAttachmentTable?
            num = row[PR_ATTACH_NUM]
            method = row.get(PR_ATTACH_METHOD, ATTACH_BY_VALUE)
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
            return self.folder.primary_store.item(entryid=_benc(entryid))
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
            self.server,
            self.mapiobj,
            mapiobj.GetAttachmentTable(0),
            columns=[PR_ATTACH_NUM],
        )
        for row in at.rows():
            mapiobj.DeleteAttach(row[0].value, 0, None, 0)

        # copy contents into it
        exclude_props = [PROP_REF_STORE_ENTRYID, PROP_REF_ITEM_ENTRYID, PROP_REF_PREV_ENTRYID, PROP_FLAGS]
        self.mapiobj.CopyTo(None, exclude_props, 0, None, IID_IMessage, mapiobj, 0)

        _utils._save(mapiobj)

        # update backref
        if new:
            entryid = HrGetOneProp(mapiobj, PR_ENTRYID).Value
            self.mapiobj.SetProps([SPropValue(PROP_REF_ITEM_ENTRYID, entryid)])
            _utils._save(self.mapiobj)

    def copy(self, folder, _delete=False):
        """ Copy item to folder; return copied item

        :param folder: target folder
        """

        mapiobj = folder.mapiobj.CreateMessage(None, 0)
        self.mapiobj.CopyTo([], [], 0, None, IID_IMessage, mapiobj, 0)
        _utils._save(mapiobj)
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

    @property
    def is_meetingrequest(self):
        """ Is the item a meeting request """
        return self.message_class.startswith('IPM.Schedule.Meeting.')

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
