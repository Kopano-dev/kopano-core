#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only
from .version import __version__

import codecs
from copy import deepcopy
import hashlib
import hmac
import json
import os.path
import signal
import sys
sys.path.insert(0, os.path.dirname(__file__)) # XXX for __import__ to work
import time
import threading

import kopano
from kopano import log_exc, Config

from flask import Flask, request, abort

CONFIG = {
    'data_path': Config.string(default='/var/lib/kopano/presence/'),
    'data_save_interval': Config.integer(default=5),
    'plugins': Config.string(multiple=True, default=['spreed']),
    'run_as_user': Config.string(default="kopano"),
    'run_as_group': Config.string(default="kopano"),
    'server_bind': Config.string(default="127.0.0.1"),
    'server_port': Config.integer(default="1234"),
    'server_auth_user': Config.string(default="presence"),
    'server_auth_password': Config.string(default="presence"),
    'server_secret_key': Config.string(),
    'server_token_expire': Config.integer(default="5"),
    'xmpp_jid': Config.string(default=None),
    'xmpp_password': Config.string(default=None),
    'xmpp_user_id_strip_domain': Config.boolean(default=None),
    'spreed_auto_unavailable': Config.integer(default="2"),
}

STATUSES = ['available', 'busy', 'away', 'unavailable'] # XXX check these?

class Service(kopano.Service):
    def main(self):
        """ setup internal data, load plugins, setup signal handling, route GET/SET requests via Flask """

        self.data = {}
        self.lock = threading.Lock()

        self.plugins = {}
        for plugin in self.config['plugins']:
            self.plugins[plugin] = __import__('plugin_%s' % plugin).Plugin(self)

        for sig in (signal.SIGINT, signal.SIGTERM):
            signal.signal(sig, self.signal_handler)

        app = Flask('kopano_presence')
        app.add_url_rule('/', 'get', self.get, methods=['GET'])
        app.add_url_rule('/', 'put', self.put, methods=['PUT'])
        app.add_url_rule('/', 'post', self.post, methods=['POST'])
        app.run(host=self.config['server_bind'], port=self.config['server_port']) #, debug=True)

    def signal_handler(self, sig, frame):
        """ gracefully disconnect plugins on ctrl-c/kill signal """

        for plugin in self.plugins.values():
            plugin.disconnect()
        sys.exit(0)

    def check_auth(self):
        """ check shared-secret based authentication token """

        secret_key = codecs.encode(self.config['server_secret_key'], 'utf-8')
        t, userid, sha256 = codecs.encode(request.json['AuthenticationToken'], 'utf-8').split(b':')
        if (sha256 != codecs.encode(hmac.new(secret_key, b'%s:%s' % (t, userid), hashlib.sha256).digest(), 'base64').strip().upper()) or \
           ((int(time.time()) - int(t)) > (self.config['server_token_expire'] * 60)):
            self.log.warning('unauthorized access; please check shared key settings in presence.cfg and client configuration.')
            abort(401)

    def get(self):
        """ return status for one or more users """

        self.check_auth()
        with log_exc(self.log):
            data = []
            for userstatus in request.json['UserStatus']:
                user_id = userstatus['user_id']
                userdata = self.data_get(user_id) or {}
                userdata['user_id'] = user_id
                data.append(userdata)
            return json.dumps({"Type": "UserStatus", "UserStatus": data}, indent=4)

    def put(self):
        """ update status for one or more users """

        self.check_auth()
        with log_exc(self.log):
            for userstatus in request.json['UserStatus']:
                user_id = userstatus['user_id']
                for plugin, plugin_data in userstatus.items():
                    if plugin != 'user_id': # XXX
                        plugin_data['user_id'] = user_id
                        plugin_data['last_update'] = int(time.time())
                        self.data_set(user_id, plugin, plugin_data.get('status'), plugin_data.get('message'))
                        self.plugins[plugin].update(user_id, plugin_data)
            return ''

    def post(self):
        """ update status for one or more users and return status for one or more users """

        self.check_auth()
        with log_exc(self.log):
            data = []
            for userstatus in request.json['UserStatus']:
                user_id = userstatus['user_id']
                for plugin, plugin_data in userstatus.items():
                    if plugin != 'user_id': # XXX
                        plugin_data['user_id'] = user_id
                        plugin_data['last_update'] = int(time.time())
                        self.data_set(user_id, plugin, plugin_data.get('status'), plugin_data.get('message'))
                        self.plugins[plugin].update(user_id, plugin_data)
                userdata = self.data_get(user_id) or {}
                userdata['user_id'] = user_id
                data.append(userdata)
            return json.dumps({"Type": "UserStatus", "UserStatus": data}, indent=4)

    def data_get(self, username):
        """ atomic get of user data """

        with self.lock:
            return deepcopy(self.data.get(username))

    def data_set(self, username, plugin, status, message):
        """ atomic update of user data """

        self.log.debug('%s: %s %s', plugin, username, status)
        with self.lock:
            userplugin = self.data.setdefault(username, {}).setdefault(plugin, {})
            userplugin['status'] = status
            userplugin['message'] = message

def main():
    parser = kopano.parser('CKQSF')
    options, args = parser.parse_args()
    Service('presence', config=CONFIG, options=options).start()

if __name__ == '__main__':
    main()
