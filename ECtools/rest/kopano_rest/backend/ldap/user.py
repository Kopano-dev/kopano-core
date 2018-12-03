# SPDX-License-Identifier: AGPL-3.0-or-later

import json

from .resource import Resource

class UserResource(Resource):
    fields = {
        'id': lambda user: user.userid,
        'displayName': lambda user: user.fullname,
        'jobTitle': lambda user: user.job_title,
        'givenName': lambda user: user.first_name,
        'mail': lambda user: user.email,
        'mobilePhone': lambda user: user.mobile_phone,
        'officeLocation': lambda user: user.office_location,
        'surname': lambda user: user.last_name,
        'userPrincipalName': lambda user: user.name,
    }

    def on_get(self, req, resp, userid=None, method=None):
        data = {
            '@odata.context': '/api/gc/v1/users',
            '@odata.nextLink': '/api/gc/v1/users?$skip=10',
            'value': [
                {
                    'displayName': 'Franz von Schitznel',
                    'mail': 'franz@schnitzel.com',
                },
                {
                    'displayName': 'Helga Wurfel',
                    'mail': 'helga@wurfel.com',
                },

            ]
        }

        resp.content_type = "application/json"
        resp.body = json.dumps(data, indent=2) # TODO stream
