# SPDX-License-Identifier: AGPL-3.0-or-later

import json

from .resource import Resource

class UserResource(Resource):

    def on_get(self, req, resp, userid=None, folderid=None, itemid=None, method=None):
        data = {
            '@odata.context': '/api/gc/v1/me/messages',
            '@odata.nextLink': '/api/gc/v1/me/messages?$skip=10',
            'value': [
                 {
                     'subject': 'hello imap'
                 },
            ],
        }

        resp.content_type = 'application/json'
        resp.body = json.dumps(data, indent=2) # TODO stream
