# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import csv

from io import StringIO

from MAPI import (
    TBL_ALL_COLUMNS, TABLE_SORT_ASCEND, TABLE_SORT_DESCEND, TBL_BATCH
)
from MAPI.Defs import PpropFindProp
from MAPI.Struct import MAPIErrorNotFound, SSort, SSortOrderSet

from .defs import REV_TAG

from . import property_ as _prop

class Table(object):
    """Table class

    Low-level abstraction for MAPI tables.
    """

    def __init__(self, server, mapiobj, mapitable, proptag=None,
            restriction=None, order=None, columns=None):
        self.server = server
        self.mapiobj = mapiobj
        self.mapitable = mapitable
        self.proptag = proptag
        if columns:
            mapitable.SetColumns(columns, 0)
        else:
            # some columns are hidden by default TODO result (if at all)
            # depends on table implementation
            cols = mapitable.QueryColumns(TBL_ALL_COLUMNS)
            cols = cols or mapitable.QueryColumns(0) # fall-back
            mapitable.SetColumns(cols, 0)
        if restriction:
            self.mapitable.Restrict(restriction.mapiobj, TBL_BATCH)

    @property
    def header(self):
        """Return all table column names."""
        return [str(REV_TAG.get(c, hex(c))) \
            for c in self.mapitable.QueryColumns(0)]

    def rows(self, batch_size=100, page_start=None, page_limit=None):
        """Return all table rows."""
        offset = 0
        if page_limit is not None:
            batch_size = page_limit
        if page_start is not None:
            self.mapitable.SeekRow(0, page_start)
        try:
            while True:
                result = self.mapitable.QueryRows(batch_size, offset)
                if not result:
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
        """Return table row count."""
        return self.mapitable.GetRowCount(0)

    def dict_rows(self, batch_size=100):
        """Return all table rows as dictionaries."""
        for row in self.rows(batch_size):
            yield dict((p.proptag, p.mapiobj.Value) for p in row)

    # TODO: apply batch_size as shown above
    def dict_(self, key, value):
        d = {}
        for row in self.mapitable.QueryRows(2147483647, 0):
            d[PpropFindProp(row, key).Value] = PpropFindProp(row, value).Value
        return d

    def index(self, key):
        """Return key->row dictionary keyed on given column (proptag)."""
        d = {}
        for row in self.mapitable.QueryRows(2147483647, 0):
            d[PpropFindProp(row, key).Value] = \
                dict((c.ulPropTag, c.Value) for c in row)
        return d

    def data(self, header=False):
        """Return list per row with textual representation for each element."""
        data = [[p.strval for p in row] for row in self.rows()]
        if header:
            data = [self.header] + data
        return data

    def text(self, borders=False):
        """Return textual table representation."""
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
        """Return CSV data for table. All arguments are passed to
        csv.writer."""
        csvfile = StringIO()
        writer = csv.writer(csvfile, *args, **kwargs)
        writer.writerows(self.data(header=True))
        return csvfile.getvalue()

    def sort(self, tags):
        """Sort table.

        :param tags: Tag(s) on which to sort.
        """
        if not isinstance(tags, tuple):
            tags = (tags,)

        ascend_descend = [SSort(abs(tag),
            TABLE_SORT_DESCEND if tag < 0 else TABLE_SORT_ASCEND)
                for tag in tags]

        self.mapitable.SortTable(SSortOrderSet(ascend_descend, 0, 0), 0)

    def __iter__(self):
        return self.rows()

    def __unicode__(self):
        tablename = None
        if self.proptag:
            tablename = REV_TAG.get(self.proptag)
        return 'Table(%s)' % tablename

    def __repr__(self):
        return self.__unicode__()
