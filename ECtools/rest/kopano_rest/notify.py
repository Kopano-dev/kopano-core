import base64
import codecs
import falcon
import json
import traceback
import uuid
try:
    from queue import Queue
except ImportError:
    from Queue import Queue
import requests
from threading import Thread

import kopano

from . import utils
from .config import PREFIX

SUBSCRIPTIONS = {}
INSECURE = False

def _user(req):
    global SERVER
    try:
        SERVER
    except NameError:
        SERVER = kopano.Server(notifications=True, parse_args=False)

    auth_header = req.get_header('Authorization')
    userid = req.get_header('X-Kopano-UserEntryID')

    if auth_header and auth_header.startswith('Basic '):
        user, passwd = codecs.decode(codecs.encode(auth_header[6:], 'ascii'), 'base64').split(b':')
        return SERVER.user(codecs.decode(user, 'utf8'))
    elif userid:
        return SERVER.user(userid=userid)

# TODO don't block on sending updates
# TODO restarting app/server
# TODO expiration
# TODO handshake/call webhook

class Processor(Thread):
    def __init__(self):
        Thread.__init__(self)
        self.daemon = True

    def run(self):
        while True:
            store, notification, subscription = QUEUE.get()

            data = {
                'subscriptionId': subscription['id'],
                'clientState': subscription['clientState'],
                'changeType': 'created', # TODO from notification
                'resource': subscription['resource'],
                'resourceData': {
                    # TODO fill in
                }
            }
            try:
                requests.post(subscription['notificationUrl'], json.dumps(data), timeout=10, verify=not INSECURE)
            except Exception:
                traceback.print_exc()

class Sink:
    def __init__(self, store, subscription):
        self.store = store
        self.subscription = subscription

    def update(self, notification):
        global QUEUE
        try:
            QUEUE
        except NameError:
            QUEUE = Queue()
            Processor().start()

        QUEUE.put((self.store, notification, self.subscription))

def _get_folder(store, resource):
    resource = resource.split('/')
    if (len(resource) == 4 and \
        resource[0] == 'me' and resource[1] == 'mailFolders' and resource[3] == 'messages'):
        folderid = resource[2]
    return utils._folder(store, folderid)

class SubscriptionResource:
    def on_post(self, req, resp):
        user = _user(req)
        store = user.store
        fields = json.loads(req.stream.read().decode('utf-8'))
        folder = _get_folder(store, fields['resource'])

        # TODO store-level, hierarchy.. ?

        id_ = str(uuid.uuid4())
        subscription = fields
        subscription['id'] = id_

        sink = Sink(store, subscription)
        folder.subscribe(sink)

        SUBSCRIPTIONS[id_] = (subscription, sink)

        resp.content_type = "application/json"
        resp.body = json.dumps(subscription, indent=2, separators=(',', ': '))

    def on_get(self, req, resp, subscriptionid):
        subscription, sink = SUBSCRIPTIONS[subscriptionid]

        resp.content_type = "application/json"
        resp.body = json.dumps(subscription, indent=2, separators=(',', ': '))

    def on_delete(self, req, resp, subscriptionid):
        user = _user(req)
        store = user.store

        subscription, sink = SUBSCRIPTIONS[subscriptionid]
        folder = _get_folder(store, subscription['resource'])

        folder.unsubscribe(sink)
        del SUBSCRIPTIONS[subscriptionid]

app = falcon.API()

subscriptions = SubscriptionResource()
app.add_route(PREFIX+'/subscriptions', subscriptions)
app.add_route(PREFIX+'/subscriptions/{subscriptionid}', subscriptions)
