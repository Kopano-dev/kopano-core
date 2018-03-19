from ..utils import _server_store
from ..config import TOP
from .base import Resource

class GroupResource(Resource):
    fields = {
        'id': lambda group: group.groupid,
        'displayName': lambda group: group.name,
        'mail': lambda group: group.email,
    }

    def on_get(self, req, resp, userid=None, groupid=None, method=None):
        server, store = _server_store(req, userid, self.options)

        if groupid:
            for group in server.groups(): # TODO server.group(groupid/entryid=..)
                if group.groupid == groupid:
                    data = group
        else:
            data = (server.groups(), TOP, 0, 0)

        if method == 'members':
            data = (group.users(), TOP, 0, 0)
            self.respond(req, resp, data, UserResource.fields)
        else:
            self.respond(req, resp, data)
