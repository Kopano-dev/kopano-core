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
the kopano-rest WSGI app.

"""

# TODO use kopano.Service

SOCKET_PATH = '/var/run/kopano'
WORKERS = 8

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


def run_app(socket_path, n, options):
    if SETPROCTITLE:
        setproctitle.setproctitle('kopano-mfr rest %d' % n)
    app = kopano_rest.RestAPI(options)
    app.add_error_handler(Exception, error_handler)
    unix_socket = 'unix:' + os.path.join(socket_path, 'rest%d.sock' % n)
    logging.info('starting rest worker: %s', unix_socket)
    try:
        bjoern.run(app, unix_socket)
    except KeyboardInterrupt:
        pass

def run_notify(socket_path, options):
    # TODO merge with run_app?
    if SETPROCTITLE:
        setproctitle.setproctitle('kopano-mfr notify')
    app = kopano_rest.NotifyAPI(options)
    app.add_error_handler(Exception, error_handler)
    unix_socket = 'unix:' + os.path.join(socket_path, 'notify.sock')
    logging.info('starting notify worker: %s', unix_socket)
    try:
        bjoern.run(app, unix_socket)
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

if __name__ == '__main__':
    main()
