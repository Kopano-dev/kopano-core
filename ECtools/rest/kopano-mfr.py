import logging
from logging.handlers import QueueHandler, QueueListener
import multiprocessing
import optparse
import os
import os.path
import signal
import sys
import time

import falcon

try:
    from prometheus_client import multiprocess
    from prometheus_client import generate_latest, CollectorRegistry, CONTENT_TYPE_LATEST, Summary
    PROMETHEUS = True
except ImportError:
    PROMETHEUS = False

try:
    import setproctitle
    SETPROCTITLE = True
except ImportError:
    SETPROCTITLE = False

import bjoern
import kopano_rest

"""
Master Fleet Runner

Instantiates the specified number of Bjoern WSGI server processes,
each taking orders on their own unix socket and passing requests to
the respective WSGI app (rest, notify or metrics).

"""

# TODO use kopano.Service

SOCKET_PATH = '/var/run/kopano'
WORKERS = 8
METRICS_LISTEN = 'localhost:6060'

# metrics
if PROMETHEUS:
    REQUEST_TIME = Summary('kopano_mfr_request_processing_seconds', 'Time spent processing request', ['method', 'endpoint'])

def opt_args():
    parser = optparse.OptionParser()

    parser.add_option("", "--socket-path", dest="socket_path",
                      help="parent directory for unix sockets", metavar="PATH")
    parser.add_option("-w", "--workers", dest="workers", type='int',
                      help="number of workers (unix sockets)", metavar="N")
    parser.add_option("", "--insecure", dest='insecure', action='store_true', default=False,
                      help="allow insecure connections")
    parser.add_option("", "--enable-auth-basic", dest='auth_basic', action='store_true', default=False,
                      help="enable basic authentication")
    parser.add_option("", "--enable-auth-passthrough", dest='auth_passthrough', action='store_true', default=False,
                      help="enable passthrough authentication (use with caution)")
    parser.add_option("", "--disable-auth-bearer", dest='auth_bearer', action='store_false', default=True,
                      help="disable bearer authentication")
    parser.add_option("", "--with-metrics", dest='with_metrics', action='store_true', default=False,
                      help="enable metrics process")
    parser.add_option("", "--metrics-listen", dest='metrics_listen', metavar='ADDRESS:PORT',
                      default=METRICS_LISTEN, help="metrics process address")

    options, args = parser.parse_args()
    if args:
        parser.print_usage()
        sys.exit(-1)
    return options, args

def error_handler(ex, req, resp, params):
    # Ignore falcon.HTTPError and re-raise
    if isinstance(ex, falcon.HTTPError):
        raise

    # Do things with other errors here
    logging.exception(ex)

# falcon metrics middleware

class FalconMetrics(object):
    def process_request(self, req, resp):
        req.context['start_time'] = time.time()

    def process_resource(self, req, resp, resource, params):
        req.context['label'] = \
            req.uri_template.replace('{method}', params.get('method', ''))

    def process_response(self, req, resp, resource):
        t = time.time() - req.context['start_time']
        label = req.context.get('label')
        if label:
            if 'deltaid' in req.context:
                label = label.replace(req.context['deltaid'], 'delta')
            REQUEST_TIME.labels(req.method, label).observe(t)

# Expose metrics.
def metrics_app(environ, start_response):
    registry = CollectorRegistry()
    multiprocess.MultiProcessCollector(registry)
    data = generate_latest(registry)
    status = '200 OK'
    response_headers = [
        ('Content-type', CONTENT_TYPE_LATEST),
        ('Content-Length', str(len(data)))
    ]
    start_response(status, response_headers)
    return iter([data])

# TODO merge run_*
def run_app(socket_path, n, options):
    if SETPROCTITLE:
        setproctitle.setproctitle('kopano-mfr rest %d' % n)
    if options.with_metrics:
        middleware=[FalconMetrics()]
    else:
        middleware=None
    app = kopano_rest.RestAPI(options=options, middleware=middleware)
    app.add_error_handler(Exception, error_handler)
    unix_socket = 'unix:' + os.path.join(socket_path, 'rest%d.sock' % n)
    logging.info('starting rest worker: %s', unix_socket)
    try:
        bjoern.run(app, unix_socket)
    except KeyboardInterrupt:
        pass

def run_notify(socket_path, options):
    if SETPROCTITLE:
        setproctitle.setproctitle('kopano-mfr notify')
    if options.with_metrics:
        middleware=[FalconMetrics()]
    else:
        middleware=None
    app = kopano_rest.NotifyAPI(options=options, middleware=middleware)
    app.add_error_handler(Exception, error_handler)
    unix_socket = 'unix:' + os.path.join(socket_path, 'notify.sock')
    logging.info('starting notify worker: %s', unix_socket)
    try:
        bjoern.run(app, unix_socket)
    except KeyboardInterrupt:
        pass

def run_metrics(socket_path, options):
    if SETPROCTITLE:
        setproctitle.setproctitle('kopano-mfr metrics')
    address = options.metrics_listen
    logging.info('starting metrics worker: %s', address)
    address = address.split(':')
    try:
        bjoern.run(metrics_app, address[0], int(address[1]))
    except KeyboardInterrupt:
        pass

def logger_init():
    q = multiprocessing.Queue()
    # this is the handler for all log records
    handler = logging.StreamHandler()
    handler.setFormatter(logging.Formatter("%(levelname)s: %(asctime)s - %(process)s - %(message)s"))

    # ql gets records from the queue and sends them to the handler
    ql = QueueListener(q, handler)
    ql.start()

    logger = logging.getLogger()
    logger.setLevel(logging.DEBUG)
    # add the handler to the logger so records from this process are handled
    logger.addHandler(handler)

    return ql, q

def main():
    options, args = opt_args()
    socket_path = options.socket_path or SOCKET_PATH
    nworkers = options.workers if options.workers is not None else WORKERS

    q_listener, q = logger_init()
    logging.info('starting kopano-mfr')

    workers = []
    for n in range(nworkers):
        process = multiprocessing.Process(target=run_app, args=(socket_path, n, options))
        workers.append(process)

    notify_process = multiprocessing.Process(target=run_notify, args=(socket_path, options))
    workers.append(notify_process)

    if options.with_metrics:
        if PROMETHEUS:
            metrics_process = multiprocessing.Process(target=run_metrics, args=(socket_path, options))
            workers.append(metrics_process)
        else:
            logging.error('please install prometheus client python bindings')
            sys.exit(-1)

    for worker in workers:
        worker.daemon = True
        worker.start()

    try:
        for worker in workers:
            worker.join()
    except KeyboardInterrupt:
        for worker in workers:
            worker.terminate()
    finally:
        q_listener.stop()

        sockets = []
        for n in range(nworkers):
            sockets.append('rest%d.sock' % n)
        sockets.append('notify.sock')
        for socket in sockets:
            try:
                unix_socket = os.path.join(socket_path, socket)
                os.unlink(unix_socket)
            except OSError:
                pass

        if options.with_metrics:
            for worker in workers:
                multiprocess.mark_process_dead(worker.pid)

if __name__ == '__main__':
    main()
