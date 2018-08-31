# SPDX-License-Identifier: AGPL-3.0-or-later
import falcon

from .resource import json
from ..utils import (
    _server_store, _folder
)
from .message import MessageResource
from .folder import FolderResource

class DeletedMailFolderResource(FolderResource):
    fields = {
        '@odata.type': lambda folder: '#microsoft.graph.mailFolder', # TODO
        'id': lambda folder: folder.entryid,
        '@removed': lambda folder: {'reason': 'deleted'} # TODO soft deletes
    }

class MailFolderResource(FolderResource):
    fields = FolderResource.fields.copy()
    fields.update({
        'parentFolderId': lambda folder: folder.parent.entryid,
        'displayName': lambda folder: folder.name,
        'unreadItemCount': lambda folder: folder.unread,
        'totalItemCount': lambda folder: folder.count,
        'childFolderCount': lambda folder: folder.subfolder_count,
    })

    relations = {
        'childFolders': lambda folder: (folder.folders, MailFolderResource),
        'messages': lambda folder: (folder.items, MessageResource) # TODO event msgs
    }

    deleted_resource = DeletedMailFolderResource
    container_classes = (None, 'IPF.Note')

    def on_get(self, req, resp, userid=None, folderid=None, method=None):
        server, store = _server_store(req, userid, self.options)

        if folderid == 'delta':
            req.context['deltaid'] = '{folderid}'
            self.delta(req, resp, store)
            return
        else:
            data = _folder(store, folderid)

        if not method:
            self.respond(req, resp, data)

        elif method == 'childFolders':
            data = self.generator(req, data.folders, data.subfolder_count_recursive)
            self.respond(req, resp, data)

        elif method == 'messages':
            data = self.folder_gen(req, data)
            self.respond(req, resp, data, MessageResource.fields)

        else:
            raise falcon.HTTPBadRequest(None, "Unsupported segment '%s'" % method)

    def on_post(self, req, resp, userid=None, folderid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        folder = _folder(store, folderid)
        fields = json.loads(req.stream.read().decode('utf-8'))

        if method == 'messages':
            item = self.create_message(folder, fields, MessageResource.set_fields)
            self.respond(req, resp, item, MessageResource.fields)

        elif method == 'childFolders':
            child = folder.create_folder(fields['displayName']) # TODO exception on conflict
            self.respond(req, resp, child, MailFolderResource.fields)

        elif method in ('copy', 'move'):
            to_folder = store.folder(entryid=fields['destinationId'].encode('ascii')) # TODO ascii?
            if method == 'copy':
                folder.parent.copy(folder, to_folder)
            else:
                folder.parent.move(folder, to_folder)
