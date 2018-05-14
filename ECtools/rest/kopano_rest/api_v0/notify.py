import base64
import codecs
import json
import logging
import traceback
import uuid
try:
    from queue import Queue
except ImportError:
    from Queue import Queue
import requests
from threading import Thread

import falcon

try:
    from prometheus_client import Counter, Gauge
    PROMETHEUS = True
except ImportError:
    PROMETHEUS = False

from MAPI import MAPI_MESSAGE # TODO avoid MAPI
import kopano
kopano.set_bin_encoding('base64')

from .. import utils
from .config import PREFIX

logging.getLogger("requests").setLevel(logging.WARNING)

# TODO don't block on sending updates
# TODO async subscription validation
# TODO restarting app/server?
# TODO expiration?
# TODO avoid globals (threading)
# TODO list subscription scalability

SUBSCRIPTIONS = {}

if PROMETHEUS:
    SUBSCR_COUNT = Counter('kopano_mfr_total_subscriptions', 'Total number of subscriptions')
    SUBSCR_ACTIVE = Gauge('kopano_mfr_active_subscriptions', 'Number of active subscriptions', multiprocess_mode='livesum')
    POST_COUNT = Counter('kopano_mfr_total_webhook_posts', 'Total number of webhook posts')

def _server(auth_user, auth_pass, oidc=False):
    # return global connection, using credentials from first user to
    # authenticate, and use it for all notifications
    global SERVER
    try: # TODO thread lock?
        SERVER
    except NameError:
        SERVER = kopano.Server(auth_user=auth_user, auth_pass=auth_pass,
            notifications=True, parse_args=False, oidc=oidc)
    return SERVER

def _user(req, options):
    auth = utils._auth(req, options)

    if auth['method'] == 'bearer':
        username = auth['user']
        server = _server(auth['userid'], auth['token'], oidc=True)
    elif auth['method'] == 'basic':
        username = codecs.decode(auth['user'], 'utf8')
        server = _server(username, auth['password'])
    elif auth['method'] == 'passthrough':
        username = utils._username(auth['userid'])
        server = _server(username, '')
    return server.user(username)


class Processor(Thread):
    def __init__(self, options):
        Thread.__init__(self)
        self.options = options
        self.daemon = True

    def _notification(self, subscription, event_type, obj):
        return {
            'subscriptionId': subscription['id'],
            'clientState': subscription['clientState'],
            'changeType': event_type,
            'resource': subscription['resource'],
            'resourceData': {
                '@data.type': '#Microsoft.Graph.Message',
                'id': obj.entryid,
            }
        }

    def run(self):
        while True:
            store, notification, subscription = QUEUE.get()

            data = self._notification(subscription, notification.event_type, notification.object)

            verify = not self.options or not self.options.insecure
            try:
                if self.options and self.options.with_metrics:
                    POST_COUNT.inc()
                logging.debug('Subscription notification: %s' % subscription['notificationUrl'])
                print('posting', json.dumps(data))
                requests.post(subscription['notificationUrl'], json=data, timeout=10, verify=verify)
            except Exception:
                traceback.print_exc()

class Sink:
    def __init__(self, options, store, subscription):
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

def _subscription_object(store, resource):
    resource = resource.split('/')

    if (len(resource) == 4 and \
        resource[0] == 'me' and \
        ((resource[:2] == ['me', 'mailFolders'] and resource[-1] == 'messages') or \
         (resource[:2] == ['me', 'contactFolders'] and resource[-1] == 'contacts'))):
        return utils._folder(store, resource[2]), None

    elif (len(resource) == 2 and \
          resource[0] == 'me'):
        if resource[1] == 'messages':
            return store, 'mail'
        elif resource[1] == 'contacts':
            return store, 'contacts'

class SubscriptionResource:
    def __init__(self, options):
        self.options = options

    def on_post(self, req, resp):
        user = _user(req, self.options)
        store = user.store
        fields = json.loads(req.stream.read().decode('utf-8'))

        # validate webhook
        validationToken = str(uuid.uuid4())
        verify = not self.options or not self.options.insecure
        try: # TODO async
            logging.debug('Subscription validation: %s' % fields['notificationUrl'])
            r = requests.post(fields['notificationUrl']+'?validationToken='+validationToken, timeout=10, verify=verify)
            if r.text != validationToken:
                raise falcon.HTTPBadRequest(None, "Subscription validation request failed.")
        except Exception:
            raise falcon.HTTPBadRequest(None, "Subscription validation request failed.")

        # create subscription
        id_ = str(uuid.uuid4())
        subscription = fields
        subscription['id'] = id_

        target, folder_types = _subscription_object(store, fields['resource'])

        sink = Sink(self.options, store, subscription)
        object_types = ['item'] # TODO folders not supported by graph atm?
        event_types = [x.strip() for x in subscription['changeType'].split(',')]

        target.subscribe(sink, object_types=object_types,
                         event_types=event_types, folder_types=folder_types)

        SUBSCRIPTIONS[id_] = (subscription, sink, user.userid)

        resp.content_type = "application/json"
        resp.body = json.dumps(subscription, indent=2, separators=(',', ': '))

        if self.options and self.options.with_metrics:
            SUBSCR_COUNT.inc()
            SUBSCR_ACTIVE.set(len(SUBSCRIPTIONS))

    def on_get(self, req, resp, subscriptionid=None):
        user = _user(req, self.options)

        if subscriptionid:
            subscription, sink, userid = SUBSCRIPTIONS[subscriptionid]
            data = subscription
        else:
            userid = user.userid
            data = {
                '@odata.context': req.path,
                'value': [subscription for (subscription, _, uid) in SUBSCRIPTIONS.values() if uid == userid], # TODO doesn't scale
            }

        resp.content_type = "application/json"
        resp.body = json.dumps(data, indent=2, separators=(',', ': '))

    def on_delete(self, req, resp, subscriptionid):
        user = _user(req, self.options)
        store = user.store

        subscription, sink, userid = SUBSCRIPTIONS[subscriptionid]

        store.unsubscribe(sink)
        del SUBSCRIPTIONS[subscriptionid]

        if self.options and self.options.with_metrics:
            SUBSCR_ACTIVE.set(len(SUBSCRIPTIONS))

class NotifyAPIv0(falcon.API):
    def __init__(self, options=None, middleware=None):
        super().__init__(media_type=None, middleware=middleware)
        self.options = options

        subscriptions = SubscriptionResource(options)

        self.add_route(PREFIX+'/subscriptions', subscriptions)
        self.add_route(PREFIX+'/subscriptions/{subscriptionid}', subscriptions)
