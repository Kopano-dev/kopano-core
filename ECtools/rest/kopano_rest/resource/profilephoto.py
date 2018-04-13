import falcon

from ..utils import (
    _server_store, _folder, _item
)
from .resource import Resource

from MAPI.Util import GetDefaultStore
import kopano

class ProfilePhotoResource(Resource):
    fields = {
        '@odata.mediaContentType': lambda photo: photo.mimetype,
        'width': lambda photo: photo.width,
        'height': lambda photo: photo.height,
        'id': lambda photo: ('%dX%d' % (photo.width, photo.height))
    }

    def on_get(self, req, resp, userid=None, folderid=None, itemid=None, photoid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        photo = None

        if userid:
            photo = server.user(userid=userid).photo
        elif itemid:
            folder = _folder(store, folderid or 'contacts')
            photo = _item(folder, itemid).photo
        else:
            userid = kopano.Store(server=server,
                mapiobj = GetDefaultStore(server.mapisession)).user.userid
            photo = server.user(userid=userid).photo

        if not photo:
            raise falcon.HTTPNotFound(description="The photo wasn't found")

        if photoid:
            size = tuple(map(int, photoid.lower().split('x')))
            photo = photo.scale(size)

        if method == '$value':
            resp.content_type = photo.mimetype
            resp.data = photo.data

        else:
            self.respond(req, resp, photo)

    def on_patch(self, *args, **kwargs):
        self.on_put(*args, **kwargs)

    def on_put(self, req, resp, userid=None, folderid=None, itemid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        folder = _folder(store, folderid or 'contacts')
        contact = _item(folder, itemid)
        contact.set_photo('noname', req.stream.read(), req.get_header('Content-Type'))
