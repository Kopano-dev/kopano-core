from .resource import Resource

class ProfilePhotoResource(Resource):
    fields = {
        '@odata.mediaContentType': lambda photo: photo.mimetype,

    }

# TODO restore on_get etc here?

#    def on_patch(self, *args, **kwargs):
#        self.on_put(*args, **kwargs)
#
#    def on_put(self, req, resp, userid=None, folderid=None, itemid=None, method=None):
#        server, store = _server_store(req, userid, self.options)
#        folder = utils._folder(store, folderid or 'contacts')
#        contact = folder.item(itemid)
#
#        contact.set_photo('noname', req.stream.read(), req.get_header('Content-Type'))

