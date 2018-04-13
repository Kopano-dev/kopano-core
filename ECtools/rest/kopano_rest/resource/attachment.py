import base64

from ..utils import (
    _server_store, _folder, _item
)
from .resource import (
    Resource, _date
)

class AttachmentResource(Resource):
    fields = {
        'id': lambda attachment: attachment.entryid,
        'name': lambda attachment: attachment.name,
        'lastModifiedDateTime': lambda attachment: _date(attachment.last_modified),
        'size': lambda attachment: attachment.size,
        'isInline': lambda attachment: False, # TODO
        'contentType': lambda attachment: attachment.mimetype,
    }

    # TODO to ItemAttachmentResource
    expansions = {
        'microsoft.graph.itemAttachment/item': lambda attachment: (attachment.item, EmbeddedMessageResource),
    }

    def on_get(self, req, resp, userid=None, folderid=None, itemid=None, attachmentid=None, method=None):
        server, store = _server_store(req, userid, self.options)

        if folderid:
            folder = _folder(store, folderid)
        elif itemid:
            folder = store.calendar
        else:
            folder = store.inbox # TODO messages from all folders?

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

    def on_delete(self, req, resp, userid=None, folderid=None, itemid=None, attachmentid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        item = _item(store, itemid)
        attachment = item.attachment(attachmentid)
        item.delete(attachment)

class FileAttachmentResource(AttachmentResource):
    fields = AttachmentResource.fields.copy()
    fields.update({
        '@odata.type': lambda attachment: '#microsoft.graph.fileAttachment',
        'contentBytes': lambda attachment: base64.urlsafe_b64encode(attachment.data).decode('ascii'),
    })

class ItemAttachmentResource(AttachmentResource):
    fields = AttachmentResource.fields.copy()
    fields.update({
        '@odata.type': lambda attachment: '#microsoft.graph.itemAttachment',
    })
