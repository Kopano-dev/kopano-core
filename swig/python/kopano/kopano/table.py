"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import csv

try:
    from StringIO import StringIO
except ImportError:
    from io import StringIO

from .defs import *
from .compat import fake_unicode as _unicode
from .prop import Property

from MAPI.Util import *

class Table(object):
    """Table class"""

    def __init__(self, server, mapitable, proptag, restriction=None, order=None, columns=None):
        self.server = server
        self.mapitable = mapitable
        self.proptag = proptag
        if columns:
            mapitable.SetColumns(columns, 0)
        else:
            cols = mapitable.QueryColumns(TBL_ALL_COLUMNS) # some columns are hidden by default XXX result (if at all) depends on table implementation 
            cols = cols or mapitable.QueryColumns(0) # fall-back 
            mapitable.SetColumns(cols, 0)

    @property
    def header(self):
        return [_unicode(REV_TAG.get(c, hex(c))) for c in self.mapitable.QueryColumns(0)]

    def rows(self):
        try:
            for row in self.mapitable.QueryRows(-1, 0):
                yield [Property(self.server.mapistore, c) for c in row]
        except MAPIErrorNotFound:
            pass

    def dict_rows(self):
        for row in self.mapitable.QueryRows(-1, 0):
            yield dict((c.ulPropTag, c.Value) for c in row)

    def dict_(self, key, value):
        d = {}
        for row in self.mapitable.QueryRows(-1, 0):
            d[PpropFindProp(row, key).Value] = PpropFindProp(row, value).Value
        return d

    def index(self, key):
        d = {}
        for row in self.mapitable.QueryRows(-1, 0):
            d[PpropFindProp(row, key).Value] = dict((c.ulPropTag, c.Value) for c in row)
        return d

    def data(self, header=False):
        data = [[p.strval for p in row] for row in self.rows()]
        if header:
            data = [self.header] + data
        return data

    def text(self, borders=False):
        result = []
        data = self.data(header=True)
        colsizes = [max(len(d[i]) for d in data) for i in range(len(data[0]))]
        for d in data:
            line = []
            for size, c in zip(colsizes, d):
                line.append(c.ljust(size))
            result.append(' '.join(line))
        return '\n'.join(result)

    def csv(self, *args, **kwargs):
        csvfile = StringIO()
        writer = csv.writer(csvfile, *args, **kwargs)
        writer.writerows(self.data(header=True))
        return csvfile.getvalue()

    def sort(self, tags):
        if not isinstance(tags, tuple):
            tags = (tags,)
        self.mapitable.SortTable(SSortOrderSet([SSort(abs(tag), TABLE_SORT_DESCEND if tag < 0 else TABLE_SORT_ASCEND) for tag in tags], 0, 0), 0)

    def __iter__(self):
        return self.rows()

    def __repr__(self):
        return u'Table(%s)' % REV_TAG.get(self.proptag)
