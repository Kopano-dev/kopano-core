# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import csv
import sys

try:
    from StringIO import StringIO
except ImportError: # pragma: no cover
    from io import StringIO

from MAPI import (
    TBL_ALL_COLUMNS, TABLE_SORT_ASCEND, TABLE_SORT_DESCEND, TBL_BATCH
)
from MAPI.Defs import PpropFindProp
from MAPI.Struct import MAPIErrorNotFound, SSort, SSortOrderSet

from .defs import REV_TAG
from .compat import fake_unicode as _unicode, repr as _repr

if sys.hexversion >= 0x03000000:
    from . import property_ as _prop
else: # pragma: no cover
    import property_ as _prop

class Table(object):
    """Table class"""

    def __init__(self, server, mapiobj, mapitable, proptag=None, restriction=None, order=None, columns=None):
        self.server = server
        self.mapiobj = mapiobj
        self.mapitable = mapitable
        self.proptag = proptag
        if columns:
            mapitable.SetColumns(columns, 0)
        else:
            cols = mapitable.QueryColumns(TBL_ALL_COLUMNS) # some columns are hidden by default XXX result (if at all) depends on table implementation
            cols = cols or mapitable.QueryColumns(0) # fall-back
            mapitable.SetColumns(cols, 0)
        if restriction:
            self.mapitable.Restrict(restriction.mapiobj, TBL_BATCH)

    @property
    def header(self):
        return [_unicode(REV_TAG.get(c, hex(c))) for c in self.mapitable.QueryColumns(0)]

    def rows(self, batch_size=100, page_start=None, page_limit=None):
        offset = 0
        if page_limit is not None:
            batch_size = page_limit
        if page_start is not None:
            self.mapitable.SeekRow(0, page_start)
        try:
            while True:
                result = self.mapitable.QueryRows(batch_size, offset)
                if len(result) == 0:
                    break

                for row in result:
                    yield [_prop.Property(self.mapiobj, c) for c in row]

                if page_limit is not None:
                    break
                offset += batch_size

        except MAPIErrorNotFound:
            pass

    @property
    def count(self):
        return self.mapitable.GetRowCount(0)

    def dict_rows(self, batch_size=100):
        for row in self.rows(batch_size):
            yield dict((p.proptag, p.mapiobj.Value) for p in row)

    # XXX: apply batch_size as shown above
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

    def __unicode__(self):
        tablename = None
        if self.proptag:
            tablename = REV_TAG.get(self.proptag)
        return u'Table(%s)' % tablename

    def __repr__(self):
        return _repr(self)
