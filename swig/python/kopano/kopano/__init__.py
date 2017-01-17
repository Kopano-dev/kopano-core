"""
High-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)

Some goals:

- To be fully object-oriented, pythonic, layer above MAPI
- To be usable for many common system administration tasks
- To provide full access to the underlying MAPI layer if needed
- To return all text as unicode strings
- To return/accept binary identifiers in readable (hex-encoded) form
- To raise well-described exceptions if something goes wrong

Main classes:

:class:`Server`

:class:`Store`

:class:`User`

:class:`Company`

:class:`Store`

:class:`Folder`

:class:`Item`

:class:`Body`

:class:`Attachment`

:class:`Address`

:class:`Outofoffice`

:class:`Quota`

:class:`Permission`

:class:`Config`

:class:`Service`


"""

import collections
import contextlib
try:
    import daemon
    import daemon.pidlockfile
except ImportError:
    pass

import errno
import grp
import logging.handlers
from multiprocessing import Process, Queue
try:
    from Queue import Empty
except ImportError:
    from queue import Empty
import os.path
import pwd
import socket
import sys
import threading
import traceback
import signal
import ssl

from .compat import decode as _decode
from .errors import (
    Error, ConfigError, NotFoundError, LogonError, NotSupportedError
)
from .config import Config, CONFIG
from .server import Server
from .address import Address
from .attachment import Attachment
from .autoaccept import AutoAccept
from .body import Body
from .company import Company
from .folder import Folder
from .group import Group
from .item import Item
from .outofoffice import Outofoffice
from .prop import Property
from .permission import Permission
from .quota import Quota
from .recurrence import Recurrence, Occurrence
from .rule import Rule
from .store import Store
from .table import Table
from .user import User
from .parser import parser


def _daemon_helper(func, service, log):
    try:
        if not service or isinstance(service, Service):
            if isinstance(service, Service): # XXX
                service.log_queue = Queue()
                service.ql = QueueListener(service.log_queue, *service.log.handlers)
                service.ql.start()
            func()
        else:
            func(service)
    finally:
        if isinstance(service, Service) and service.ql: # XXX move queue stuff into Service
            service.ql.stop()
        if log and service:
            log.info('stopping %s', service.name)

def _daemonize(func, options=None, foreground=False, log=None, config=None, service=None):
    uid = gid = None
    working_directory = '/'
    pidfile = None
    if config:
        working_directory = config.get('running_path')
        pidfile = config.get('pid_file')
        if config.get('run_as_user'):
            uid = pwd.getpwnam(config.get('run_as_user')).pw_uid
        if config.get('run_as_group'):
            gid = grp.getgrnam(config.get('run_as_group')).gr_gid
    if not pidfile and service:
        pidfile = "/var/run/kopano/%s.pid" % service.name
    if pidfile:
        pidfile = daemon.pidlockfile.TimeoutPIDLockFile(pidfile, 10)
        oldpid = pidfile.read_pid()
        if oldpid is None:
            # there was no pidfile, remove the lock if it's there
            pidfile.break_lock()
        elif oldpid:
            try:
                cmdline = open('/proc/%u/cmdline' % oldpid).read().split('\0')
            except IOError as error:
                if error.errno != errno.ENOENT:
                    raise
                # errno.ENOENT indicates that no process with pid=oldpid exists, which is ok
                pidfile.break_lock()
    if uid is not None and gid is not None:
        for h in log.handlers:
            if isinstance(h, logging.handlers.WatchedFileHandler):
                os.chown(h.baseFilename, uid, gid)
    if options and options.foreground:
        foreground = options.foreground
        working_directory = os.getcwd()
    with daemon.DaemonContext(
            pidfile=pidfile,
            uid=uid,
            gid=gid,
            working_directory=working_directory,
            files_preserve=[h.stream for h in log.handlers if isinstance(h, logging.handlers.WatchedFileHandler)] if log else None,
            prevent_core=False,
            detach_process=not foreground,
            stdout=sys.stdout,
            stderr=sys.stderr,
        ):
        _daemon_helper(func, service, log)

def _loglevel(options, config):
    if options and getattr(options, 'loglevel', None):
        log_level = options.loglevel
    elif config:
        log_level = config.get('log_level')
    else:
        log_level = 'debug'
    return { # XXX NONE?
        '0': logging.NOTSET,
        '1': logging.CRITICAL,
        '2': logging.ERROR,
        '3': logging.WARNING,
        '4': logging.INFO,
        '5': logging.INFO,
        '6': logging.DEBUG,
        'DEBUG': logging.DEBUG,
        'INFO': logging.INFO,
        'WARNING': logging.WARNING,
        'ERROR': logging.ERROR,
        'CRITICAL': logging.CRITICAL,
    }[log_level.upper()]

def logger(service, options=None, stdout=False, config=None, name=''):
    logger = logging.getLogger(name or service)
    if logger.handlers:
        return logger
    formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    log_method = 'file'
    log_file = '/var/log/kopano/%s.log' % service
    if config:
        log_method = config.get('log_method') or log_method
        log_file = config.get('log_file') or log_file
    log_level = _loglevel(options, config)
    if name:
        log_file = log_file.replace(service, name) # XXX
    fh = None
    if log_method == 'file' and log_file != '-':
        fh = logging.handlers.WatchedFileHandler(log_file)
    elif log_method == 'syslog':
        fh = logging.handlers.SysLogHandler(address='/dev/log')
    if fh:
        fh.setLevel(log_level)
        fh.setFormatter(formatter)
        logger.addHandler(fh)
    if not getattr(options, 'service', True):
        ch = logging.StreamHandler(sys.stdout)
        ch.setLevel(log_level)
        ch.setFormatter(formatter)
        if stdout or (options and options.foreground):
            logger.addHandler(ch)
    logger.setLevel(log_level)
    return logger

@contextlib.contextmanager # it logs errors, that's all you need to know :-)
def log_exc(log, stats=None):
    """
Context-manager to log any exception in sub-block to given logger instance

:param log: logger instance

Example usage::

    with log_exc(log):
        .. # any exception will be logged when exiting sub-block

"""
    try: yield
    except Exception:
        log.error(traceback.format_exc())
        if stats:
            stats['errors'] += 1


# log-to-queue handler copied from Vinay Sajip
class QueueHandler(logging.Handler):
    def __init__(self, queue):
        logging.Handler.__init__(self)
        self.queue = queue

    def enqueue(self, record):
        self.queue.put_nowait(record)

    def prepare(self, record):
        self.format(record)
        record.msg, record.args, record.exc_info = record.message, None, None
        return record

    def emit(self, record):
        try:
            self.enqueue(self.prepare(record))
        except (KeyboardInterrupt, SystemExit):
            raise
        except:
            self.handleError(record)

# log-to-queue listener copied from Vinay Sajip
class QueueListener(object):
    _sentinel = None

    def __init__(self, queue, *handlers):
        self.queue = queue
        self.handlers = handlers
        self._stop = threading.Event()
        self._thread = None

    def dequeue(self, block):
        return self.queue.get(block)

    def start(self):
        self._thread = t = threading.Thread(target=self._monitor)
        t.setDaemon(True)
        t.start()

    def prepare(self, record):
        return record

    def handle(self, record):
        record = self.prepare(record)
        for handler in self.handlers:
            handler.handle(record)

    def _monitor(self):
        q = self.queue
        has_task_done = hasattr(q, 'task_done')
        while not self._stop.isSet():
            try:
                record = self.dequeue(True)
                if record is self._sentinel:
                    break
                self.handle(record)
                if has_task_done:
                    q.task_done()
            except (Empty, EOFError):
                pass
        # There might still be records in the queue.
        while True:
            try:
                record = self.dequeue(False)
                if record is self._sentinel:
                    break
                self.handle(record)
                if has_task_done:
                    q.task_done()
            except (Empty, EOFError):
                break

    def stop(self):
        self._stop.set()
        self.queue.put_nowait(self._sentinel)
        self._thread.join()
        self._thread = None

class Service:
    """
Encapsulates everything to create a simple service, such as:

- Locating and parsing a configuration file
- Performing logging, as specifified in the configuration file
- Handling common command-line options (-c, -F)
- Daemonization (if no -F specified)

:param name: name of the service; if for example 'search', the configuration file should be called ``/etc/kopano/search.cfg`` or passed with -c
:param config: :class:`Configuration <Config>` to use
:param options: OptionParser instance to get settings from (see :func:`parser`)

"""

    def __init__(self, name, config=None, options=None, args=None, logname=None, **kwargs):
        self.name = name
        self.__dict__.update(kwargs)
        if not options:
            options, args = parser('cskpUPufmvVFw').parse_args()
            args = [_decode(arg) for arg in args]
        self.options, self.args = options, args
        self.name = name
        self.logname = logname
        self.ql = None
        config2 = CONFIG.copy()
        if config:
            config2.update(config)
        if getattr(options, 'config_file', None):
            options.config_file = os.path.abspath(options.config_file) # XXX useful during testing. could be generalized with optparse callback?
        if not getattr(options, 'service', True):
            options.foreground = True
        self.config = Config(config2, service=name, options=options)
        self.config.data['server_socket'] = os.getenv("KOPANO_SOCKET") or self.config.data['server_socket']
        if getattr(options, 'worker_processes', None):
            self.config.data['worker_processes'] = options.worker_processes
        self.log = logger(self.logname or self.name, options=self.options, config=self.config) # check that this works here or daemon may die silently XXX check run_as_user..?
        for msg in self.config.warnings:
            self.log.warn(msg)
        if self.config.errors:
            for msg in self.config.errors:
                self.log.error(msg)
            sys.exit(1)
        self.stats = collections.defaultdict(int, {'errors': 0})
        self._server = None

    def main(self):
        raise Error('Service.main not implemented')

    @property
    def server(self):
        if self._server is None:
            self._server = Server(options=self.options, config=self.config.data, log=self.log, service=self)
        return self._server

    def start(self):
        for sig in (signal.SIGTERM, signal.SIGINT):
            signal.signal(sig, lambda *args: sys.exit(-sig))
        signal.signal(signal.SIGHUP, signal.SIG_IGN) # XXX long term, reload config?

        self.log.info('starting %s', self.logname or self.name)
        with log_exc(self.log):
            if getattr(self.options, 'service', True): # do not run-as-service (eg backup)
                _daemonize(self.main, options=self.options, log=self.log, config=self.config, service=self)
            else:
                _daemon_helper(self.main, self, self.log)

class Worker(Process):
    def __init__(self, service, name, **kwargs):
        Process.__init__(self)
        self.daemon = True
        self.name = name
        self.service = service
        self.__dict__.update(kwargs)
        self.log = logging.getLogger(name=self.name)
        if not self.log.handlers:
            loglevel = _loglevel(service.options, service.config)
            formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
            qh = QueueHandler(service.log_queue)
            qh.setFormatter(formatter)
            qh.setLevel(loglevel)
            self.log.addHandler(qh)
            self.log.setLevel(loglevel)

    def main(self):
        raise Error('Worker.main not implemented')

    def run(self):
        self.service._server = None # do not re-use "forked" session
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGHUP, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, lambda *args: sys.exit(0))
        with log_exc(self.log):
            self.main()

class _ZSocket: # XXX megh, double wrapper
    def __init__(self, addr, ssl_key, ssl_cert):
        self.ssl_key = ssl_key
        self.ssl_cert = ssl_cert
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.bind(addr)
        self.s.listen(socket.SOMAXCONN)

    def accept(self):
        newsocket, fromaddr = self.s.accept()
        connstream = ssl.wrap_socket(newsocket, server_side=True, keyfile=self.ssl_key, certfile=self.ssl_cert)
        return connstream, fromaddr


def server_socket(addr, ssl_key=None, ssl_cert=None, log=None): # XXX https, merge code with client_socket
    if addr.startswith('file://'):
        addr2 = addr.replace('file://', '')
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        os.system('rm -f %s' % addr2)
        s.bind(addr2)
        s.listen(socket.SOMAXCONN)
    elif addr.startswith('https://'):
        addr2 = addr.replace('https://', '').split(':')
        addr2 = (addr2[0], int(addr2[1]))
        s = _ZSocket(addr2, ssl_key=ssl_key, ssl_cert=ssl_cert)
    else:
        addr2 = addr.replace('http://', '').split(':')
        addr2 = (addr2[0], int(addr2[1]))
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.bind(addr2)
        s.listen(socket.SOMAXCONN)
    if log:
        log.info('listening on socket %s', addr)
    return s

def client_socket(addr, ssl_cert=None, log=None):
    if addr.startswith('file://'):
        addr2 = addr.replace('file://', '')
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    elif addr.startswith('https://'):
        addr2 = addr.replace('https://', '').split(':')
        addr2 = (addr2[0], int(addr2[1]))
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s = ssl.wrap_socket(s, ca_certs=ssl_cert, cert_reqs=ssl.CERT_REQUIRED)
    else:
        addr2 = addr.replace('http://', '').split(':')
        addr2 = (addr2[0], int(addr2[1]))
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(addr2)
    return s

# interactive shortcuts
def user(name):
    return Server().user(name)

def users(*args, **kwargs):
    return Server().users(*args, **kwargs)

def store(guid):
    return Server().store(guid)

def stores(*args, **kwargs):
    return Server().stores(*args, **kwargs)

def company(name):
    return Server().company(name)

def companies(*args, **kwargs):
    return Server().companies(*args, **kwargs)
