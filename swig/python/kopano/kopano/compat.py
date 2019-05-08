# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)
"""

try:
    import cPickle as pickle
except ImportError:
    import pickle

import base64
import codecs
import io

_BIN_ENCODING = 'hex'
_MISSING_NONE = False

# TODO inline some of these again, now that we're python3-only
def is_str(s):
    return isinstance(s, str)

def pickle_load(f):
    return pickle.load(f, encoding='bytes')

def pickle_loads(s):
    return pickle.loads(s, encoding='bytes')

def hex(s):
    return codecs.encode(s, 'hex').upper().decode('ascii')

def benc(s):
    if _BIN_ENCODING == 'hex':
        return codecs.encode(s, _BIN_ENCODING).strip().upper().decode('ascii')
    elif _BIN_ENCODING == 'base64':
        return base64.urlsafe_b64encode(s).strip().decode('ascii')
    else:
        return codecs.encode(s, _BIN_ENCODING).strip().decode('ascii')

def bdec(s):
    if _BIN_ENCODING == 'base64':
        return base64.urlsafe_b64decode(s)
    else:
        return codecs.decode(s, _BIN_ENCODING)

def is_file(f):
    return isinstance(f, io.IOBase)

def repr(o):
    return o.__unicode__()

def fake_unicode(s): # TODO inline.. remove str() also in most cases?
    return str(s)

def decode(s):
    return s

def encode(s):
    return s.encode()

def set_bin_encoding(encoding):
    """Override encoding to use for binary identifiers (hex or base64)."""
    global _BIN_ENCODING
    _BIN_ENCODING = encoding

def set_missing_none():
    """When properties are missing, return *None* instead of default."""
    global _MISSING_NONE
    _MISSING_NONE = True

def default(value):
    return None if _MISSING_NONE else value
