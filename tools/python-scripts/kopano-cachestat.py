#!/usr/bin/python -u

import sys
import datetime
from MAPI import *
from MAPI.Defs import *
from MAPI.Util import *

def getStats(store):
    systemtable = store.OpenProperty(PR_EC_STATSTABLE_SYSTEM, IID_IMAPITable, 0, 0)
    systemtable.SetColumns([PR_DISPLAY_NAME, PR_EC_STATS_SYSTEM_VALUE, PR_EC_STATS_SYSTEM_DESCRIPTION], TBL_BATCH)

    stats = {}
    while True:
        rows = systemtable.QueryRows(-1, 0)

        if len(rows) == 0: break

        for row in rows:
            stats[row[0].Value] = {}
            stats[row[0].Value]['value'] = row[1].Value
            stats[row[0].Value]['description'] = row[2].Value

    return stats

def diff(new, old):
    d = {}

    if not old:
        for n in new.keys():
            try:
                d[n] = int(new[n]['value'])
            except ValueError: pass

    else:
        for n in new.keys():
            try:
                d[n] = int(new[n]['value']) - int(old[n]['value'])
            except ValueError: pass

    return d

session = OpenECSession('SYSTEM', '', 'default:')
store = GetDefaultStore(session)

stats = getStats(store)
cstat = {}
serverstarttime = ''

for n in stats.keys():
    if n == 'server_start_date':
        serverstarttime = stats[n]['value']

    if n.startswith('cache_'):
        if n.endswith('_hit'):
            name = n[6:-4]
            if not cstat.has_key(name): cstat[name] = {}
            cstat[name]['hits'] = int(stats[n]['value'])
        if n.endswith('_req'):
            name = n[6:-4]
            if not cstat.has_key(name): cstat[name] = {}
            cstat[name]['requests'] = int(stats[n]['value'])
        if n.endswith('_size'):
            name = n[6:-5]
            if not cstat.has_key(name): cstat[name] = {}
            cstat[name]['size'] = int(stats[n]['value'])
        if n.endswith('_maxsz'):
            name = n[6:-6]
            if not cstat.has_key(name): cstat[name] = {}
            cstat[name]['maxsz'] = int(stats[n]['value'])

print 'Kopano Cache Statistics'
print '  Server start time: %s' % ( serverstarttime )
print '  Current time     : %s' % ( datetime.datetime.now().strftime('%c') )
print ''
print '%10s %24s         %24s' % ('Cache', 'Hit ratio', 'Mem usage ratio')

for name in cstat:
    if ('requests' not in cstat[name]):
        continue

    if(cstat[name]['requests']):
        percentage = '%d%%' % (cstat[name]['hits'] * 100 / cstat[name]['requests'])
    else:
        percentage = 'N/A'

    if ('maxsz' in cstat[name]):
        persize = '%d%%' % (cstat[name]['size'] * 100 / cstat[name]['maxsz'])
    else:
        persize = 'N/A'

    print '%10s (%12d/%12d) (%3s)   (%12d/%12d) (%3s)' % (name, cstat[name]['hits'], cstat[name]['requests'], percentage, cstat[name]['size'], cstat[name]['maxsz'], persize)
