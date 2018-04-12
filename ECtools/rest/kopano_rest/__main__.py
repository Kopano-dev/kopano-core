import multiprocessing
import sys
from wsgiref.simple_server import make_server

from .api_v0.rest import RestAPIv0
from .api_v0.notify import NotifyAPIv0

REST_PORT = int(sys.argv[1]) if len(sys.argv) > 2 else 8000
NOTIFY_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8001

def run_rest():
    make_server('localhost', REST_PORT, RestAPIv0()).serve_forever()

def run_notify():
    make_server('localhost', NOTIFY_PORT, NotifyAPIv0()).serve_forever()

multiprocessing.Process(target=run_rest).start()
multiprocessing.Process(target=run_notify).start()
