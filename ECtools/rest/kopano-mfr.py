from multiprocessing import Process
import optparse
import os
import os.path
import signal
import sys
import time

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

def run_app(socket_path, n, options):
    app = kopano_rest.RestAPI(options)
    unix_socket = 'unix:' + os.path.join(socket_path, 'rest%d.sock' % n)
    bjoern.run(app, unix_socket)

def run_notify(socket_path, options):
    app = kopano_rest.NotifyAPI(options)
    unix_socket = 'unix:' + os.path.join(socket_path, 'notify.sock')
    bjoern.run(app, unix_socket)

def main():
    options, args = opt_args()
    socket_path = options.socket_path or SOCKET_PATH
    nworkers = options.workers if options.workers is not None else WORKERS

    workers = []
    for n in range(nworkers):
        process = Process(target=run_app, args=(socket_path, n, options))
        workers.append(process)

    notify_process = Process(target=run_notify, args=(socket_path, options))
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
