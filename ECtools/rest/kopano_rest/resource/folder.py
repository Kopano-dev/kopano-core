import codecs

from ..config import TOP
from ..utils import (
    _server_store, _folder, db_put, db_get
)
from .resource import (
    Resource, urlparse
)

class DeletedFolder(object):
    pass

class FolderImporter:
    def __init__(self):
        self.updates = []
        self.deletes = []

    def update(self, folder):
        self.updates.append(folder)
        db_put(folder.sourcekey, folder.entryid) # TODO different db?

    def delete(self, folder, flags):
        d = DeletedFolder()
        d.entryid = db_get(folder.sourcekey)
        d.container_class = 'IPF.Note' # TODO
        self.deletes.append(d)

class FolderResource(Resource):
    fields = {
        'id': lambda folder: folder.entryid,
    }

    def on_delete(self, req, resp, userid=None, folderid=None):
        server, store = _server_store(req, userid, self.options)
        folder = _folder(store, folderid)
        store.delete(folder)

    def delta(self, req, resp, store): # TODO contactfolders, calendars.. use restriction?
        args = urlparse.parse_qs(req.query_string)
        token = args['$deltatoken'][0] if '$deltatoken' in args else None
        importer = FolderImporter()
        newstate = store.subtree.hierarchy_sync(importer, token)
        if not token: # TODO initial sync broken??
            for f in store.subtree.folders():
                importer.update(f)
        changes = [(o, self) for o in importer.updates] + \
            [(o, self.deleted_resource) for o in importer.deletes]
        changes = [c for c in changes if c[0].container_class in (None, 'IPF.Note')]
        data = (changes, TOP, 0, len(changes))
        deltalink = b"%s?$deltatoken=%s" % (req.path.encode('utf-8'), codecs.encode(newstate, 'ascii'))
        self.respond(req, resp, data, self.fields, deltalink=deltalink)
