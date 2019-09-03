#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only
from .version import __version__

from http.server import HTTPServer, BaseHTTPRequestHandler
import codecs
from copy import deepcopy
from functools import partial
import hashlib
import hmac
import json
import os.path
import signal
import sys
sys.path.insert(0, os.path.dirname(__file__)) # XXX for __import__ to work
import time
import threading
import traceback

import kopano
from kopano import log_exc, Config

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

class RequestHandler(BaseHTTPRequestHandler):
    def __init__(self, service, *args, **kwargs):
        self.service = service
        self.config = service.config
        self.log = service.log
        super().__init__(*args, **kwargs)

    def get_json(self):
        content_len = int(self.headers.get('Content-Length'))
        body = self.rfile.read(content_len)
        return json.loads(body)

    def respond(self, status_code=200, data=None):
        self.send_response(status_code)
        if data:
            self.send_header('Content-type', 'application/json')
        self.end_headers()
        if data:
            self.wfile.write(codecs.encode(json.dumps(data, indent=4), 'utf-8'))

    def do_GET(self):
        """Return status for one or more users."""
        body = self.get_json()
        if not self.check_auth(body):
            return
        try:
            data = []
            for userstatus in body['UserStatus']:
                user_id = userstatus['user_id']
                userdata = self.service.data_get(user_id) or {}
                userdata['user_id'] = user_id
                data.append(userdata)
            self.respond(200, {
                'Type': 'UserStatus',
                'UserStatus': data
            })
        except Exception:
            self.log.error(traceback.format_exc())
            self.respond(status_code=500)

    def do_PUT(self):
        """Update status for one or more users."""
        body = self.get_json()
        if not self.check_auth(body):
            return
        try:
            for userstatus in self.get_json('UserStatus'):
                user_id = userstatus['user_id']
                for plugin, plugin_data in userstatus.items():
                    if plugin != 'user_id': # XXX
                        plugin_data['user_id'] = user_id
                        plugin_data['last_update'] = int(time.time())
                        self.data_set(user_id, plugin, plugin_data.get('status'), plugin_data.get('message'))
                        self.plugins[plugin].update(user_id, plugin_data)
            self.respond(200)
        except Exception:
            self.log.error(traceback.format_exc())
            self.respond(500)

    def do_POST(self):
        """Update status for one or more users and return status for one or more users."""
        body = self.get_json()
        if not self.check_auth(body):
            return
        try:
            data = []
            for userstatus in body['UserStatus']:
                user_id = userstatus['user_id']
                for plugin, plugin_data in userstatus.items():
                    if plugin != 'user_id': # XXX
                        plugin_data['user_id'] = user_id
                        plugin_data['last_update'] = int(time.time())
                        self.data_set(user_id, plugin, plugin_data.get('status'), plugin_data.get('message'))
                        self.plugins[plugin].update(user_id, plugin_data)
                userdata = self.service.data_get(user_id) or {}
                userdata['user_id'] = user_id
                data.append(userdata)
            self.respond(200, {
                'Type': 'UserStatus',
                'UserStatus': data
            })
        except Exception:
            self.log.error(traceback.format_exc())
            self.respond(500)

    def check_auth(self, body):
        """Check shared-secret based authentication token."""
        secret_key = codecs.encode(self.config['server_secret_key'], 'utf-8')
        t, userid, sha256 = codecs.encode(body['AuthenticationToken'], 'utf-8').split(b':')
        if (sha256 != codecs.encode(hmac.new(secret_key, b'%s:%s' % (t, userid), hashlib.sha256).digest(), 'base64').strip().upper()) or \
           ((int(time.time()) - int(t)) > (self.config['server_token_expire'] * 60)):
            self.log.warning('unauthorized access; please check shared key settings in presence.cfg and client configuration.')
            self.respond(401)
            return False
        return True

class Service(kopano.Service):
    def main(self):
        """Setup internal data, load plugins, setup signal handling, start HTTP server."""
        self.data = {}
        self.lock = threading.Lock()

        self.plugins = {}
        for plugin in self.config['plugins']:
            self.plugins[plugin] = __import__('plugin_%s' % plugin).Plugin(self)

        for sig in (signal.SIGINT, signal.SIGTERM):
            signal.signal(sig, self.signal_handler)

        addr = (self.config['server_bind'], self.config['server_port'])
        server = HTTPServer(addr, partial(RequestHandler, self))
        server.serve_forever()

    def signal_handler(self, sig, frame):
        """Gracefully disconnect plugins on ctrl-c/kill signal."""
        for plugin in self.plugins.values():
            plugin.disconnect()
        sys.exit(0)

    def data_get(self, username):
        """Atomic get of user data."""
        with self.lock:
            return deepcopy(self.data.get(username))

    def data_set(self, username, plugin, status, message):
        """Atomic update of user data."""
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
