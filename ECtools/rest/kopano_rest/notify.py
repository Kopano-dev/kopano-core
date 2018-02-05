import falcon
import json
import kopano

PREFIX = '/api/gc/v0'
SERVER = kopano.Server(notifications=True)

class SubscriptionResource:
    def on_post(self, req, resp):
        fields = json.loads(req.stream.read().decode('utf-8'))
        print('POST', fields)

subscriptions = SubscriptionResource()
app = falcon.API()
app.add_route(PREFIX+'/subscriptions', subscriptions)
