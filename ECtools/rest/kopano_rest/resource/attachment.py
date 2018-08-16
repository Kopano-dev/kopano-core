import base64

from ..utils import (
    _server_store, _folder, _item
)
from .resource import (
    Resource, _date
)

import falcon

class AttachmentResource(Resource):
    fields = {
        'id': lambda attachment: attachment.entryid,
        'lastModifiedDateTime': lambda attachment: _date(attachment.last_modified),
        'size': lambda attachment: attachment.size,
    }

    # TODO to ItemAttachmentResource
    expansions = {
        'microsoft.graph.itemAttachment/item': lambda attachment: (attachment.item, EmbeddedMessageResource),
    }

    def on_get(self, req, resp, userid=None, folderid=None, itemid=None, eventid=None, attachmentid=None, method=None):
        server, store = _server_store(req, userid, self.options)

        if folderid:
            folder = _folder(store, folderid)
        elif eventid:
            folder = store.calendar
        elif itemid:
            folder = store.inbox # TODO messages from all folders?

        if eventid:
            item = folder.event(eventid) # TODO like _item
        elif itemid:
            item = _item(folder, itemid)

        data = item.attachment(attachmentid)

        if method == '$value': # TODO graph doesn't do this?
            resp.content_type = data.mimetype
            resp.data = data.data
        else:
            if data.embedded:
                all_fields = ItemAttachmentResource.fields # TODO to sub resource
            else:
                all_fields = FileAttachmentResource.fields
            self.respond(req, resp, data, all_fields=all_fields)

    def on_delete(self, req, resp, userid=None, folderid=None, itemid=None, eventid=None, attachmentid=None, method=None):
        server, store = _server_store(req, userid, self.options)

        if folderid: # TODO same code above
            folder = _folder(store, folderid)
        elif eventid:
            folder = store.calendar
        elif itemid:
            folder = store.inbox # TODO messages from all folders?

        if eventid:
            item = folder.event(eventid) # TODO like _item
        elif itemid:
            item = _item(folder, itemid)

        attachment = item.attachment(attachmentid)
        item.delete(attachment)

        self.respond_204(resp)

class FileAttachmentResource(AttachmentResource):
    fields = AttachmentResource.fields.copy()
    fields.update({
        '@odata.type': lambda attachment: '#microsoft.graph.fileAttachment',
        'name': lambda attachment: attachment.name,
        'contentBytes': lambda attachment: base64.urlsafe_b64encode(attachment.data).decode('ascii'),
        'isInline': lambda attachment: attachment.inline,
        'contentType': lambda attachment: attachment.mimetype,
        'contentId': lambda attachment: attachment.content_id,
        'contentLocation': lambda attachment: attachment.content_location,
    })

class ItemAttachmentResource(AttachmentResource):
    fields = AttachmentResource.fields.copy()
    fields.update({
        '@odata.type': lambda attachment: '#microsoft.graph.itemAttachment',
    })
