import falcon

from .resource import json
from ..utils import (
    _server_store, _folder
)
from .contact import ContactResource
from .folder import FolderResource

class DeletedContactFolderResource(FolderResource):
    fields = {
        '@odata.type': lambda folder: '#microsoft.graph.contactFolder', # TODO
        'id': lambda folder: folder.entryid,
        '@removed': lambda folder: {'reason': 'deleted'} # TODO soft deletes
    }

class ContactFolderResource(FolderResource):
    fields = FolderResource.fields.copy()
    fields.update({
        'displayName': lambda folder: folder.name,
        'parentFolderId': lambda folder: folder.parent.entryid,
    })

    deleted_resource = DeletedContactFolderResource
    container_classes = ('IPF.Contact',)

    def on_get(self, req, resp, userid=None, folderid=None, method=None):
        server, store = _server_store(req, userid, self.options)

        if folderid == 'delta':
            req.context['deltaid'] = '{folderid}'
            self.delta(req, resp, store)
            return

        folder = _folder(store, folderid)

        if not method:
            self.respond(req, resp, folder, self.fields)

        elif method == 'contacts':
            data = self.folder_gen(req, folder)
            fields = ContactResource.fields
            self.respond(req, resp, data, fields)

        else:
            raise falcon.HTTPBadRequest(None, "Unsupported segment '%s'" % method)

    def on_post(self, req, resp, userid=None, folderid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        folder = _folder(store, folderid)

        if method == 'contacts':
            fields = json.loads(req.stream.read().decode('utf-8'))
            item = self.create_message(folder, fields, ContactResource.set_fields)

            self.respond(req, resp, item, ContactResource.fields)
