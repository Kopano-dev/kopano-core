import multiprocessing
import sys
from wsgiref.simple_server import make_server

from .api_v1.rest import RestAPI
from .api_v1.notify import NotifyAPI

REST_PORT = int(sys.argv[1]) if len(sys.argv) > 2 else 8000
NOTIFY_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8001

def run_rest():
    make_server('localhost', REST_PORT, RestAPI()).serve_forever()

def run_notify():
    make_server('localhost', NOTIFY_PORT, NotifyAPI()).serve_forever()

multiprocessing.Process(target=run_rest).start()
multiprocessing.Process(target=run_notify).start()
