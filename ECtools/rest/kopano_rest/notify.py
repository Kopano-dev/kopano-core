import base64
import codecs
import falcon
import json
import uuid
try:
    from queue import Queue
except ImportError:
    from Queue import Queue
from threading import Thread

from MAPI import (
    MAPIAdviseSink, fnevObjectModified, fnevObjectCreated, fnevObjectMoved,
    fnevObjectDeleted,
)
from MAPI.Tags import IID_IMAPIAdviseSink

import kopano

from . import utils

PREFIX = '/api/gc/v0'

def _user(req):
    auth_header = req.get_header('Authorization')
    userid = req.get_header('X-Kopano-UserEntryID')
    if auth_header:
        user, passwd = codecs.decode(codecs.encode(auth_header[6:], 'ascii'), 'base64').split(b':')
        return SERVER.user(codecs.decode(user, 'utf8'))
    elif userid:
        return SERVER.user(userid=userid)

# TODO don't block on sending updates
# TODO restarting app/server

class Processor(Thread):
    def __init__(self):
        Thread.__init__(self)
        self.daemon = True

    def run(self):
        while True:
            store, notification = QUEUE.get()
            print('call webhook!', store, notification)

class AdviseSink(MAPIAdviseSink): # TODO hide behind pyko
    def __init__(self, store):
        MAPIAdviseSink.__init__(self, [IID_IMAPIAdviseSink])
        self.store = store

    def OnNotify(self, notifications):
        for n in notifications:
            QUEUE.put((self.store, n))
        return 0

class SubscriptionResource:
    def on_post(self, req, resp):
        user = _user(req)
        store = user.store
        fields = json.loads(req.stream.read().decode('utf-8'))
        folder = utils._folder(store, 'inbox')

        sink = AdviseSink(store)
        flags = fnevObjectModified | fnevObjectCreated | fnevObjectMoved | fnevObjectDeleted
        conn = store.mapiobj.Advise(base64.urlsafe_b64decode(folder.entryid), flags, sink)

        id_ = str(uuid.uuid4())
        subscription = {
            'id': id_,
        }
        SUBSCRIPTIONS[id_] = (subscription, conn)

        resp.content_type = "application/json"
        resp.body = json.dumps(subscription, indent=2, separators=(',', ': '))

    def on_delete(self, req, resp, subscriptionid=None):
        user = _user(req)
        store = user.store
        fields = json.loads(req.stream.read().decode('utf-8'))

        subscription, conn = SUBSCRIPTIONS[fields['id']]

        store.mapiobj.Unadvise(conn)

SERVER = kopano.Server(notifications=True)
SUBSCRIPTIONS = {}
QUEUE = Queue()

subscriptions = SubscriptionResource()
app = falcon.API()
app.add_route(PREFIX+'/subscriptions', subscriptions)
app.add_route(PREFIX+'/subscriptions/{subscriptionid}', subscriptions)

Processor().start()
