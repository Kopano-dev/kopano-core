import base64
import codecs
import falcon
import json
import jwt
import traceback
import uuid
try:
    from queue import Queue
except ImportError:
    from Queue import Queue
import requests
from threading import Thread

from MAPI import MAPI_MESSAGE # TODO
import kopano
kopano.set_bin_encoding('base64')

from .. import utils
from .config import PREFIX

SUBSCRIPTIONS = {}

def _user(req, options):
    global SERVER
    try:
        SERVER
    except NameError:
        SERVER = kopano.Server(notifications=True, parse_args=False)

    auth = utils._auth(req, options)

    if auth['method'] == 'bearer':
        return SERVER.user(auth['user'])
    elif auth['method'] == 'basic':
        return SERVER.user(codecs.decode(auth['user'], 'utf8'))
    elif auth['method'] == 'passthrough':
        return SERVER.user(userid=auth['userid'])

# TODO don't block on sending updates
# TODO restarting app/server
# TODO expiration
# TODO handshake with webhook

class Processor(Thread):
    def __init__(self, options):
        Thread.__init__(self)
        self.options = options
        self.daemon = True

    def _notification(self, subscription, changetype, obj):
        return {
            'subscriptionId': subscription['id'],
            'clientState': subscription['clientState'],
            'changeType': changetype,
            'resource': subscription['resource'],
            'resourceData': {
                '@data.type': '#Microsoft.Graph.Message',
                'id': obj.entryid,
            }
        }

    def run(self):
        while True:
            store, notification, subscription = QUEUE.get()

            if notification.mapiobj.ulObjType == MAPI_MESSAGE:

                if notification.event_type == 'update':
                    changetype = 'updated'
                elif notification.event_type in ('create', 'copy', 'move'):
                    changetype = 'created'
                elif notification.event_type == 'delete':
                    changetype = 'deleted'

                data = self._notification(subscription, changetype, notification.object)

                if notification.event_type == 'move':
                    old_data = self._notification(subscription, 'deleted', notification.old_object)
                    data = {'value': [old_data, data]}

                verify = not self.options or not self.options.insecure
                try:
                    requests.post(subscription['notificationUrl'], json.dumps(data), timeout=10, verify=verify)
                except Exception:
                    traceback.print_exc()

class Sink:
    def __init__(self, api, store, subscription):
        self.options = options
        self.store = store
        self.subscription = subscription

    def update(self, notification):
        global QUEUE
        try:
            QUEUE
        except NameError:
            QUEUE = Queue()
            Processor(self.options).start()

        QUEUE.put((self.store, notification, self.subscription))

#def _get_folder(store, resource):
#    resource = resource.split('/')
#    if (len(resource) == 4 and \
#        resource[0] == 'me' and resource[1] == 'mailFolders' and resource[3] == 'messages'):
#        folderid = resource[2]
#    return utils._folder(store, folderid)

class SubscriptionResource:
    def __init__(self, options):
        self.options = options

    def on_post(self, req, resp):
        user = _user(req, self.options)
        store = user.store
        fields = json.loads(req.stream.read().decode('utf-8'))
#        folder = _get_folder(store, fields['resource'])

        # TODO folder-level, hierarchy.. ?

        id_ = str(uuid.uuid4())
        subscription = fields
        subscription['id'] = id_

        sink = Sink(self.options, store, subscription)
        store.subscribe(sink)

        SUBSCRIPTIONS[id_] = (subscription, sink)

        resp.content_type = "application/json"
        resp.body = json.dumps(subscription, indent=2, separators=(',', ': '))

    def on_get(self, req, resp, subscriptionid):
        subscription, sink = SUBSCRIPTIONS[subscriptionid]

        resp.content_type = "application/json"
        resp.body = json.dumps(subscription, indent=2, separators=(',', ': '))

    def on_delete(self, req, resp, subscriptionid):
        user = _user(req, self.options)
        store = user.store

        subscription, sink = SUBSCRIPTIONS[subscriptionid]
        #folder = _get_folder(store, subscription['resource'])

        store.unsubscribe(sink)
        del SUBSCRIPTIONS[subscriptionid]

class NotifyAPIv0(falcon.API):
    def __init__(self, options=None):
        super().__init__(media_type=None)
        self.options = options

        subscriptions = SubscriptionResource(options)

        self.add_route(PREFIX+'/subscriptions', subscriptions)
        self.add_route(PREFIX+'/subscriptions/{subscriptionid}', subscriptions)
