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

from MAPI import MAPI_MESSAGE # TODO
import kopano
kopano.set_bin_encoding('base64')

from . import utils
from .config import PREFIX

SUBSCRIPTIONS = {}

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
# TODO handshake with webhook

class Processor(Thread):
    def __init__(self, api):
        Thread.__init__(self)
        self.api = api
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

                verify = not self.api.options or not self.api.options.insecure
                try:
                    requests.post(subscription['notificationUrl'], json.dumps(data), timeout=10, verify=verify)
                except Exception:
                    traceback.print_exc()

class Sink:
    def __init__(self, api, store, subscription):
        self.api = api
        self.store = store
        self.subscription = subscription

    def update(self, notification):
        global QUEUE
        try:
            QUEUE
        except NameError:
            QUEUE = Queue()
            Processor(self.api).start()

        QUEUE.put((self.store, notification, self.subscription))

#def _get_folder(store, resource):
#    resource = resource.split('/')
#    if (len(resource) == 4 and \
#        resource[0] == 'me' and resource[1] == 'mailFolders' and resource[3] == 'messages'):
#        folderid = resource[2]
#    return utils._folder(store, folderid)

class SubscriptionResource:
    def __init__(self, api):
        self.api = api

    def on_post(self, req, resp):
        user = _user(req)
        store = user.store
        fields = json.loads(req.stream.read().decode('utf-8'))
#        folder = _get_folder(store, fields['resource'])

        # TODO folder-level, hierarchy.. ?

        id_ = str(uuid.uuid4())
        subscription = fields
        subscription['id'] = id_

        sink = Sink(self.api, store, subscription)
        store.subscribe(sink)

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
        #folder = _get_folder(store, subscription['resource'])

        store.unsubscribe(sink)
        del SUBSCRIPTIONS[subscriptionid]

class NotifyAPI(falcon.API):
    def __init__(self, options=None):
        super().__init__(media_type=None)
        self.options = options

        subscriptions = SubscriptionResource(self)

        self.add_route(PREFIX+'/subscriptions', subscriptions)
        self.add_route(PREFIX+'/subscriptions/{subscriptionid}', subscriptions)
