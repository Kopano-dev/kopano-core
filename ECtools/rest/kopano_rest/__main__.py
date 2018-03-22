import gunicorn.app.base

from .api.rest import RestAPI
from .api.notify import NotifyAPI

class StandaloneApplication(gunicorn.app.base.BaseApplication):

    def __init__(self, app, options=None):
        self.options = options or {}
        self.application = app
        super(StandaloneApplication, self).__init__()

    def load_config(self):
        config = dict([(key, value) for key, value in self.options.items()
                       if key in self.cfg.settings and value is not None])
        for key, value in config.items():
            self.cfg.set(key.lower(), value)

    def load(self):
        return self.application

options = {
    'bind': '%s:%s' % ('127.0.0.1', '8000'),
    'workers': 1,
}
StandaloneApplication(RestAPI(), options).run()

#options = {
#    'bind': '%s:%s' % ('127.0.0.1', '8001'),
#    'workers': 1,
#}
#StandaloneApplication(NotifyAPI(), options).run()
