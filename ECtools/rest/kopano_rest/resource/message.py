import base64
import calendar
import codecs
import datetime
import dateutil

from ..config import TOP
from ..utils import (
    _server_store, _folder, db_get, db_put,
)
from .resource import (
    _date, urlparse, json
)
from .item import (
    ItemResource, get_body, set_body, get_email, get_attachments,
)

def set_torecipients(item, arg):
    addrs = []
    for a in arg:
        a = a['emailAddress']
        addrs.append('%s <%s>' % (a.get('name', a['address']), a['address']))
    item.to = ';'.join(addrs)

class DeletedItem(object):
    pass

class ItemImporter:
    def __init__(self):
        self.updates = []
        self.deletes = []

    def update(self, item, flags):
        self.updates.append(item)
        db_put(item.sourcekey, item.entryid)

    def delete(self, item, flags):
        d = DeletedItem()
        d.entryid = db_get(item.sourcekey)
        self.deletes.append(d)

class MessageResource(ItemResource):
    fields = ItemResource.fields.copy()
    fields.update({
        # TODO pyko shortcut for event messages
        # TODO eventMessage resource?
        '@odata.type': lambda item: '#microsoft.graph.eventMessage' if item.message_class.startswith('IPM.Schedule.Meeting.') else None,
        'subject': lambda item: item.subject,
        'body': lambda req, item: get_body(req, item),
        'from': lambda item: get_email(item.from_),
        'sender': lambda item: get_email(item.sender),
        'toRecipients': lambda item: [get_email(to) for to in item.to],
        'ccRecipients': lambda item: [get_email(cc) for cc in item.cc],
        'bccRecipients': lambda item: [get_email(bcc) for bcc in item.bcc],
        'sentDateTime': lambda item: _date(item.sent) if item.sent else None,
        'receivedDateTime': lambda item: _date(item.received) if item.received else None,
        'hasAttachments': lambda item: item.has_attachments,
        'internetMessageId': lambda item: item.messageid,
        'importance': lambda item: item.urgency,
        'parentFolderId': lambda item: item.folder.entryid,
        'conversationId': lambda item: item.conversationid,
        'isRead': lambda item: item.read,
        'isReadReceiptRequested': lambda item: item.read_receipt,
        'isDeliveryReceiptRequested': lambda item: item.read_receipt,
        'replyTo': lambda item: [get_email(to) for to in item.replyto],
        'bodyPreview': lambda item: item.body_preview,
    })

    set_fields = {
        'subject': lambda item, arg: setattr(item, 'subject', arg),
        'body': set_body,
        'toRecipients': set_torecipients,
        'isRead': lambda item, arg: setattr(item, 'read', arg),
    }

    def delta(self, req, resp, folder):
        args = urlparse.parse_qs(req.query_string)
        token = args['$deltatoken'][0] if '$deltatoken' in args else None
        filter_ = args['$filter'][0] if '$filter' in args else None
        begin = None
        if filter_ and filter_.startswith('receivedDateTime ge '):
            begin = dateutil.parser.parse(filter_[20:])
            seconds = calendar.timegm(begin.timetuple())
            begin = datetime.datetime.fromtimestamp(seconds)
        importer = ItemImporter()
        newstate = folder.sync(importer, token, begin=begin)
        changes = [(o, MessageResource) for o in importer.updates] + \
            [(o, DeletedMessageResource) for o in importer.deletes]
        data = (changes, TOP, 0, len(changes))
        # TODO include filter in token?
        deltalink = b"%s?$deltatoken=%s" % (req.path.encode('utf-8'), codecs.encode(newstate, 'ascii'))
        self.respond(req, resp, data, MessageResource.fields, deltalink=deltalink)

    def on_get(self, req, resp, userid=None, folderid=None, itemid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        folder = _folder(store, folderid or 'inbox') # TODO all folders?

        if itemid == 'delta': # TODO move to MailFolder resource somehow?
            self.delta(req, resp, folder)
            return
        else:
            item = folder.item(itemid)

        if method == 'attachments':
            attachments = list(get_attachments(item))
            data = (attachments, TOP, 0, len(attachments))
            self.respond(req, resp, data)
            return

        elif method in ('copy', 'move'):
            body = json.loads(req.stream.read().decode('utf-8'))
            folder = store.folder(entryid=body['destinationId'].encode('ascii')) # TODO ascii?

            if method == 'copy':
                item = item.copy(folder)
            else:
                item = item.move(folder)

        self.respond(req, resp, item)

    def on_post(self, req, resp, userid=None, folderid=None, itemid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        folder = _folder(store, folderid or 'inbox') # TODO all folders?
        item = folder.item(itemid)

        if method == 'createReply':
            self.respond(req, resp, item.reply())

        elif method == 'createReplyAll':
            self.respond(req, resp, item.reply(all=True))

        elif method == 'attachments':
            fields = json.loads(req.stream.read().decode('utf-8'))
            if fields['@odata.type'] == '#microsoft.graph.fileAttachment':
                item.create_attachment(fields['name'], base64.urlsafe_b64decode(fields['contentBytes']))

    def on_patch(self, req, resp, userid=None, folderid=None, itemid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        folder = _folder(store, folderid or 'inbox') # TODO all folders?
        item = folder.item(itemid)

        fields = json.loads(req.stream.read().decode('utf-8'))

        for field, value in fields.items():
            if field in self.set_fields:
                self.set_fields[field](item, value)

        self.respond(req, resp, item, MessageResource.fields)

    def on_delete(self, req, resp, userid=None, folderid=None, itemid=None):
        server, store = _server_store(req, userid, self.options)
        item = store.item(itemid)
        store.delete(item)

class DeletedMessageResource(ItemResource):
    fields = {
        '@odata.type': lambda item: '#microsoft.graph.message', # TODO
        'id': lambda item: item.entryid,
        '@removed': lambda item: {'reason': 'deleted'} # TODO soft deletes
    }

class EmbeddedMessageResource(MessageResource):
    fields = MessageResource.fields.copy()
    fields.update({
        'id': lambda item: '',
    })
    del fields['parentFolderId'] # TODO more?
