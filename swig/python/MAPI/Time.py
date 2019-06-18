# SPDX-License-Identifier: AGPL-3.0-only
import datetime
import sys
import time

# PT_SYSTIME properties store the number of 100-nanosecond units since 1601,1,1 ..
# unixtime is represented by the number of seconds since 1970,1,1

# number of 100-nanosecond units between 1601,1,1 and 1970,1,1
NANOSECS_BETWEEN_EPOCH = 116444736000000000

def _convert(s):
    if sys.hexversion >= 0x03000000 and isinstance(s, bytes):
        return s.decode('ascii')
    else:
        return s

# class representing a PT_SYSTIME value. the 'unixtime' property can be used to convert to/from unixtime.

class FileTime(object):
    def __init__(self, filetime):
        self.filetime = filetime

    def datetime(self):
        return datetime.datetime.utcfromtimestamp(self.unixtime)

    def __getattr__(self, attr):
        if attr == 'unixtime':
            return (self.filetime - NANOSECS_BETWEEN_EPOCH) / 10000000.0;
        else:
            raise AttributeError

    def __setstate__(self, d):
        # XXX pickle with python2, unpickle with python3 (encoding='bytes')
        for k, v in d.items():
            setattr(self, _convert(k), v)

    def __setattr__(self, attr, val):
        if attr == 'unixtime':
            self.filetime = val * 10000000 + NANOSECS_BETWEEN_EPOCH
        else:
            object.__setattr__(self, attr, val)

    def __repr__(self):
        try:
            return self.datetime().isoformat()
        except ValueError:
            return '%d' % (self.filetime)

    def __lt__(self, other):
        return self.filetime < other.filetime
    def __le__(self, other):
        return self.filetime <= other.filetime
    def __gt__(self, other):
        return self.filetime > other.filetime
    def __ge__(self, other):
        return self.filetime >= other.filetime
    def __eq__(self, other):
        return isinstance(other, FileTime) and self.filetime == other.filetime
    def __ne__(self, other):
        return not isinstance(other, FileTime) or self.filetime != other.filetime

# convert unixtime to PT_SYSTIME.. (bad name, as it sounds like the result is a unixtime)

def unixtime(secs):
    t = FileTime(0)
    t.unixtime = secs
    return t
