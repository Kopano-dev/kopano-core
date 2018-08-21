# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)
"""

import contextlib
import logging.handlers
try:
    from Queue import Empty
except ImportError:
    from queue import Empty
import sys
import threading
import traceback

FMT = '%(asctime)s - %(name)s - %(levelname)s - %(message)s'

def _kopano_logger():
    log = logging.getLogger('kopano')
    log.setLevel(logging.WARNING)
    fh = logging.StreamHandler(sys.stderr)
    log.addHandler(fh)
    formatter = logging.Formatter(FMT)
    fh.setFormatter(formatter)
    return log

LOG = _kopano_logger()

def _loglevel(options, config):
    if options and getattr(options, 'loglevel', None):
        log_level = options.loglevel
    elif config:
        log_level = config.get('log_level')
    else:
        log_level = 'warning'
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
    formatter = logging.Formatter(FMT)
    log_method = 'file'
    log_file = '/var/log/kopano/%s.log' % service
    if config:
        log_method = config.get('log_method') or log_method
        log_file = config.get('log_file') or log_file
    log_level = _loglevel(options, config)
    if name:
        log_file = log_file.replace(service, name) # XXX
    fh = None
    if log_method == 'file':
        if log_file == '-':
            fh = logging.StreamHandler(sys.stderr)
        else:
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
        if stdout or (options and options.foreground and \
                      (log_method, log_file) != ('file', '-')):
            logger.addHandler(ch)
    logger.setLevel(log_level)
    return logger

# it logs errors, that's all you need to know :-)
@contextlib.contextmanager
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
    def __init__(self, queue, *handlers):
        self.queue = queue
        self.handlers = handlers
        self._stop = threading.Event()
        self._thread = None

    def start(self):
        self._thread = t = threading.Thread(target=self._monitor)
        t.setDaemon(True)
        t.start()

    def handle(self, record):
        for handler in self.handlers:
            handler.handle(record)

    def _monitor(self):
        q = self.queue
        has_task_done = hasattr(q, 'task_done')

        while True:
            try:
                record = self.queue.get(True, 1) # block, timeout 1 sec
                self.handle(record)
                if has_task_done:
                    q.task_done()

            except (Empty, EOFError):
                if self._stop.isSet():
                    break

    def stop(self):
        self._stop.set()
        self._thread.join()
        self._thread = None
