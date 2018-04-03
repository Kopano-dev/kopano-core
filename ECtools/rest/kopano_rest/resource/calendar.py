import falcon

from .resource import (
    DEFAULT_TOP, json, _start_end,
)
from .event import EventResource
from ..utils import (
    _server_store, _folder
)
from .folder import FolderResource

class CalendarResource(FolderResource):
    fields = FolderResource.fields.copy()
    fields.update({
        'displayName': lambda folder: folder.name,
    })

    def on_get(self, req, resp, userid=None, folderid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        path = req.path
        fields = None

        if method:
            path = '/'.join(path.split('/')[:-1])

        if path.split('/')[-1] == 'calendars':
            data = self.generator(req, store.calendars)
        else:
            folder = _folder(store, folderid or 'calendar')

            if method == 'calendarView':
                start, end = _start_end(req)
                data = (folder.occurrences(start, end), DEFAULT_TOP, 0, 0)
                fields = EventResource.fields

            elif method == 'events':
                data = self.generator(req, folder.items, folder.count)
                fields = EventResource.fields

            elif method:
                raise falcon.HTTPBadRequest(None, "Unsupported segment '%s'" % method)

            else:
                data = folder

        self.respond(req, resp, data, fields)

    def on_post(self, req, resp, userid=None, folderid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        folder = store.calendar # TODO

        if method == 'events':
            fields = json.loads(req.stream.read().decode('utf-8'))
            item = self.create_message(folder, fields, EventResource.set_fields)
            self.respond(req, resp, item, EventResource.fields)

