# SPDX-License-Identifier: AGPL-3.0-or-later
import falcon

from ..utils import _server_store
from .resource import (
    DEFAULT_TOP, Resource
)
from . import user

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
            data = (server.groups(), DEFAULT_TOP, 0, 0)

        if method == 'members':
            data = (group.users(), DEFAULT_TOP, 0, 0)
            self.respond(req, resp, data, user.UserResource.fields)

        elif method:
            raise falcon.HTTPBadRequest(None, "Unsupported segment '%s'" % method)

        else:
            self.respond(req, resp, data)
