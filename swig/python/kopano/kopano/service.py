# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)
"""

import collections
import errno
import grp
import pwd
import logging
import logging.handlers
import multiprocessing
from multiprocessing import Process
import os
import os.path
import signal
import socket
import ssl
import sys
import traceback


try:
    import daemon
    import daemon.pidfile as pidlockfile
except ImportError: # pragma: no cover
    pass

from . import config as _config
from . import log as _log
from . import server as _server
from . import errors as _errors
from . import parser as _parser

def _daemon_helper(func, service, log):
    try:
        if not service or isinstance(service, Service):
            if isinstance(service, Service): # TODO
                service.log_queue = multiprocessing.Queue()
                service.log_queue.cancel_join_thread()
                service.ql = _log.QueueListener(service.log_queue, *service.log.handlers)
                service.ql.start()
            func()
        else:
            func(service)
    finally:
        if log and service:
            log.info('stopping %s', service.name)
        if isinstance(service, Service) and service.ql: # XXX move queue stuff into Service
            service.ql.stop()
            service.log_queue.close()
            service.log_queue.join_thread()

def _daemonize(func, options=None, log=None, config=None,
        service=None):
    uid = gid = None
    pidfile = None
    if config:
        pidfile = config.get('pid_file')
        if config.get('run_as_user'):
            uid = pwd.getpwnam(config.get('run_as_user')).pw_uid
        if config.get('run_as_group'):
            gid = grp.getgrnam(config.get('run_as_group')).gr_gid
    if not pidfile and service:
        pidfile = "/var/run/kopano/%s.pid" % service.name
    if pidfile:
        pidfile = pidlockfile.TimeoutPIDLockFile(pidfile, 10)
        oldpid = pidfile.read_pid()
        if oldpid is None:
            # there was no pidfile, remove the lock if it's there
            pidfile.break_lock()
        elif oldpid:
            try:
                open('/proc/%u/cmdline' % oldpid).read().split('\0')
            except IOError as error:
                if error.errno != errno.ENOENT:
                    raise
                # errno.ENOENT indicates that no process with pid=oldpid
                # exists, which is ok
                pidfile.break_lock()
    if uid is not None and gid is not None:
        for h in log.handlers:
            if isinstance(h, logging.handlers.WatchedFileHandler):
                os.chown(h.baseFilename, uid, gid)
    with daemon.DaemonContext(
            pidfile=pidfile,
            uid=uid,
            gid=gid,
            working_directory='/',
            files_preserve=[h.stream for h in log.handlers if \
                isinstance(h, logging.handlers.WatchedFileHandler)] \
                    if log else None,
            prevent_core=False,
            detach_process=False,
            stdout=sys.stdout,
            stderr=sys.stderr,
    ):
        _daemon_helper(func, service, log)

class Service:
    """
Encapsulates everything to create a simple Kopano service, such as:

- Locating and parsing a configuration file
- Performing logging, as specifified in the configuration file
- Handling common command-line options (-c, -F)
- Daemonization (if no -F specified)

:param name: name of the service; if for example 'search', the configuration
    file should be called ``/etc/kopano/search.cfg`` or passed with -c
:param config: :class:`Configuration <Config>` to use
:param options: OptionParser instance to get settings from (see :func:`parser`)

"""

    def __init__(self, name, config=None, options=None, args=None,
            logname=None, **kwargs):
        self.name = name
        self.__dict__.update(kwargs)
        if not options:
            options, args = _parser.parser('CSKQUPufmvVFw').parse_args()
        self.options, self.args = options, args
        self.name = name
        self.logname = logname
        self.ql = None
        config2 = _config.CONFIG.copy()
        if config:
            config2.update(config)
        if getattr(options, 'config_file', None):
            # TODO useful during testing.
            # could be generalized with optparse callback?
            options.config_file = os.path.abspath(options.config_file)
        #: Service :class:`configuration <Config>` instance.
        self.config = _config.Config(config2, service=name, options=options)
        self.config.data['server_socket'] = \
            os.getenv("KOPANO_SOCKET") or self.config.data['server_socket']
        if getattr(options, 'worker_processes', None):
            self.config.data['worker_processes'] = options.worker_processes
        # check that this works here or daemon may die silently
        # TODO check run_as_user..?
        #: Service logger.
        self.log = _log.logger(self.logname or self.name,
            options=self.options, config=self.config)
        for msg in self.config.info:
            self.log.info(msg)
        for msg in self.config.warnings:
            self.log.warning(msg)
        if self.config.errors:
            for msg in self.config.errors:
                self.log.error(msg)
            sys.exit(1)
        self.stats = collections.defaultdict(int, {'errors': 0})
        self._server = None

    def main(self):
        raise _errors.Error('Service.main not implemented') # pragma: no cover

    @property
    def server(self):
        """Service :class:`server <Server>` instance."""
        if self._server is None:
            self._server = _server.Server(options=self.options,
                config=self.config.data, log=self.log, service=self,
                _skip_check=True)
        return self._server

    def start(self):
        """Start service."""
        for sig in (signal.SIGTERM, signal.SIGINT):
            signal.signal(sig, lambda *args: sys.exit(-sig))
        # TODO long term, reload config?
        signal.signal(signal.SIGHUP, signal.SIG_IGN)

        self.log.info('starting %s', self.logname or self.name)
        try:
            # do not run-as-service (e.g. backup)
            if getattr(self.options, 'service', True):
                _daemonize(self.main, options=self.options, log=self.log,
                    config=self.config, service=self)
            else:
                _daemon_helper(self.main, self, self.log)
        except Exception as e:
            self.log.error(traceback.format_exc())
            sys.exit(1)

class Worker(Process):
    """Service worker class.

    Used by services to spawn multiple worker instances.
    """
    def __init__(self, service, name, **kwargs):
        Process.__init__(self)
        self.daemon = True
        self.name = name
        self.service = service
        self._server = None
        self.__dict__.update(kwargs)
        self.log = logging.getLogger(name=self.name)
        if not self.log.handlers:
            loglevel = _log._loglevel(service.options, service.config)
            formatter = logging.Formatter(
                '%(asctime)s - %(name)s - %(levelname)s - %(message)s')
            qh = _log.QueueHandler(service.log_queue)
            qh.setFormatter(formatter)
            qh.setLevel(loglevel)
            self.log.addHandler(qh)
            self.log.setLevel(loglevel)

    @property
    def server(self):
        """Worker :class:`server <Server>` instance."""
        if self._server is None:
            self._server = _server.Server(options=self.service.options,
                config=self.service.config, log=self.service.log,
                service=self.service, _skip_check=True)
        return self._server

    def main(self):
        raise _errors.Error('Worker.main not implemented') # pragma: no cover

    def run(self):
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGHUP, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, lambda *args: sys.exit(0))
        with _log.log_exc(self.log):
            self.main()

class _ZSocket:
    def __init__(self, addr, ssl_key, ssl_cert):
        self.ssl_key = ssl_key
        self.ssl_cert = ssl_cert
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(addr)
        self.s.listen(socket.SOMAXCONN)

    def accept(self):
        newsocket, fromaddr = self.s.accept()
        connstream = ssl.wrap_socket(newsocket, server_side=True,
            keyfile=self.ssl_key, certfile=self.ssl_cert)
        return connstream, fromaddr

def server_socket(addr, ssl_key=None, ssl_cert=None, log=None):
    if addr.startswith('file://'):
        addr2 = addr.replace('file://', '')
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        if os.path.exists(addr2):
            os.remove(addr2)
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
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
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
