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

    options, args = parser.parse_args()
    if args:
        parser.print_usage()
        sys.exit(-1)
    return options, args

def run_app(socket_path, n):
    app = kopano_rest.app
    unix_socket = 'unix:' + os.path.join(socket_path, 'rest%d.sock' % n)
    bjoern.run(app, unix_socket)

def run_notify(socket_path):
    app = kopano_rest.notify_app
    unix_socket = 'unix:' + os.path.join(socket_path, 'notify.sock')
    bjoern.run(app, unix_socket)

def main():
    options, args = opt_args()
    socket_path = options.socket_path or SOCKET_PATH
    nworkers = options.workers if options.workers is not None else WORKERS

    workers = []
    for n in range(nworkers):
        process = Process(target=run_app, args=(socket_path, n))
        workers.append(process)

    notify_process = Process(target=run_notify, args=(socket_path,))
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
