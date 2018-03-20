from .resource import json
from ..utils import (
    _server_store, _folder
)
from .contact import ContactResource
from .folder import FolderResource

class ContactFolderResource(FolderResource):
    fields = FolderResource.fields.copy()
    fields.update({
        'displayName': lambda folder: folder.name,
        'parentFolderId': lambda folder: folder.parent.entryid,
    })

    def on_get(self, req, resp, userid=None, folderid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        folder = _folder(store, folderid)

        if method == 'contacts':
            data = self.folder_gen(req, folder)
            fields = ContactResource.fields
        else:
            data = folder
            fields = self.fields

        self.respond(req, resp, data, fields)

    def on_post(self, req, resp, userid=None, folderid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        folder = _folder(store, folderid)

        if method == 'contacts':
            fields = json.loads(req.stream.read().decode('utf-8'))
            item = self.create_message(folder, fields, ContactResource.set_fields)

            self.respond(req, resp, item, ContactResource.fields)
