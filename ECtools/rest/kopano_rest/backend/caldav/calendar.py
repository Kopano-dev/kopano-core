# SPDX-License-Identifier: AGPL-3.0-or-later

import json

from .resource import Resource

class CalendarResource(Resource):
    def on_get(self, req, resp, userid=None, folderid=None, method=None):
        data = {
            '@data.context': '/api/gc/v1/me/calendar',
            'displayName': 'caldav calendar',
        }

        resp.content_type = 'application/json'
        resp.body = json.dumps(data, indent=2) # TODO stream
