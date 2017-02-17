"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

try:
    import cPickle as pickle
except ImportError:
    import pickle

try:
    from functools import lru_cache
except ImportError:
    from .lru_cache import lru_cache

try:
    from StringIO import StringIO
except ImportError:
    # We only need it for Python 2
    pass

import codecs
import io
import sys

# Python 3
if sys.hexversion >= 0x03000000:
    def is_str(s):
        return isinstance(s, str)

    def pickle_load(f):
        return pickle.load(f, encoding='bytes')

    def pickle_loads(s):
        return pickle.loads(s, encoding='bytes')

    def hex(s):
        return codecs.encode(s, 'hex').upper()

    def unhex(s):
        return codecs.decode(s, 'hex')

    def is_int(i):
        return isinstance(i, int)

    def is_file(f):
        return isinstance(f, io.IOBase)

    def repr(o):
        return o.__unicode__()

    def fake_unicode(s):
        return str(s)

    def decode(s):
        return s

    def encode(s):
        return s.encode()

# Python 2
else:
    def is_str(s):
        return isinstance(s, (str, unicode))

    def pickle_load(f):
        return pickle.loads(f)

    def pickle_loads(s):
        return pickle.loads(s)

    def hex(s):
        return s.encode('hex').upper()

    def unhex(s):
        return s.decode('hex')

    def is_int(i):
        return isinstance(i, (int, long))

    def is_file(f):
        return isinstance(f, file) or isinstance(f, StringIO)

    def encode(s):
        # sys.stdout can be StringIO (nosetests)
        return s.encode(getattr(sys.stdout, 'encoding', 'utf8') or 'utf8')

    def decode(s):
        return s.decode(getattr(sys.stdin, 'encoding', 'utf8') or 'utf8')

    def repr(o):
        return encode(unicode(o))

    def fake_unicode(u):
        return unicode(u)
