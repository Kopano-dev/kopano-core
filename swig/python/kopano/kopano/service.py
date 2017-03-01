"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import collections
import logging
from multiprocessing import Process
import os.path
import signal
import socket
import ssl
import sys

from .compat import decode as _decode

if sys.hexversion >= 0x03000000:
    from . import config as _config
    from . import log as _log
    from . import utils as _utils
    from . import server as _server
    from . import errors as _errors
    from . import parser as _parser
else:
    import config as _config
    import log as _log
    import utils as _utils
    import server as _server
    import errors as _errors
    import parser as _parser

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
            options, args = _parser.parser('cskpUPufmvVFw').parse_args()
            args = [_decode(arg) for arg in args]
        self.options, self.args = options, args
        self.name = name
        self.logname = logname
        self.ql = None
        config2 = _config.CONFIG.copy()
        if config:
            config2.update(config)
        if getattr(options, 'config_file', None):
            options.config_file = os.path.abspath(options.config_file) # XXX useful during testing. could be generalized with optparse callback?
        if not getattr(options, 'service', True):
            options.foreground = True
        self.config = _config.Config(config2, service=name, options=options)
        self.config.data['server_socket'] = os.getenv("KOPANO_SOCKET") or self.config.data['server_socket']
        if getattr(options, 'worker_processes', None):
            self.config.data['worker_processes'] = options.worker_processes
        self.log = _log.logger(self.logname or self.name, options=self.options, config=self.config) # check that this works here or daemon may die silently XXX check run_as_user..?
        for msg in self.config.warnings:
            self.log.warn(msg)
        if self.config.errors:
            for msg in self.config.errors:
                self.log.error(msg)
            sys.exit(1)
        self.stats = collections.defaultdict(int, {'errors': 0})
        self._server = None

    def main(self):
        raise _errors.Error('Service.main not implemented')

    @property
    def server(self):
        if self._server is None:
            self._server = _server.Server(options=self.options, config=self.config.data, log=self.log, service=self)
        return self._server

    def start(self):
        for sig in (signal.SIGTERM, signal.SIGINT):
            signal.signal(sig, lambda *args: sys.exit(-sig))
        signal.signal(signal.SIGHUP, signal.SIG_IGN) # XXX long term, reload config?

        self.log.info('starting %s', self.logname or self.name)
        with _log.log_exc(self.log):
            if getattr(self.options, 'service', True): # do not run-as-service (eg backup)
                _utils._daemonize(self.main, options=self.options, log=self.log, config=self.config, service=self)
            else:
                _utils._daemon_helper(self.main, self, self.log)

class Worker(Process):
    def __init__(self, service, name, **kwargs):
        Process.__init__(self)
        self.daemon = True
        self.name = name
        self.service = service
        self.__dict__.update(kwargs)
        self.log = logging.getLogger(name=self.name)
        if not self.log.handlers:
            loglevel = _log._loglevel(service.options, service.config)
            formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
            qh = _log.QueueHandler(service.log_queue)
            qh.setFormatter(formatter)
            qh.setLevel(loglevel)
            self.log.addHandler(qh)
            self.log.setLevel(loglevel)

    def main(self):
        raise _errors.Error('Worker.main not implemented')

    def run(self):
        self.service._server = None # do not re-use "forked" session
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGHUP, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, lambda *args: sys.exit(0))
        with _log.log_exc(self.log):
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
