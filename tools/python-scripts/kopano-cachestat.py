#!/usr/bin/python -u

from datetime import datetime

import kopano
from MAPI.Tags import PR_EC_STATSTABLE_SYSTEM, PR_DISPLAY_NAME_W, PR_EC_STATS_SYSTEM_VALUE, PR_EC_STATS_SYSTEM_DESCRIPTION

"""

provides an easy-to read overview of the kopano-server cache statistics,
as compared to the more low-level "kopano-stats --system"

"""

server = kopano.Server(auth_user='SYSTEM', auth_pass='')
# XXX: When python-kopano supports sorting, simplify the loop by sorting on display_name in descending order.
table = server.table(PR_EC_STATSTABLE_SYSTEM, columns=[PR_DISPLAY_NAME_W, PR_EC_STATS_SYSTEM_VALUE, PR_EC_STATS_SYSTEM_DESCRIPTION])

cstat = {}
for row in table.rows():
    name, value, _ = row
    name = name.value
    value = value.value

    if name == 'server_start_date':
        serverstarttime = value

    # Skip non cache items
    if not name.startswith('cache_'):
        continue

    if name.endswith('_hit'):
        name = name[6:-4]
        if not name in cstat: cstat[name] = {}
        cstat[name]['hits'] = int(value)
    if name.endswith('_req'):
        name = name[6:-4]
        if not name in cstat: cstat[name] = {}
        cstat[name]['requests'] = int(value)
    if name.endswith('_size'):
        name = name[6:-5]
        if not name in cstat: cstat[name] = {}
        cstat[name]['size'] = int(value)
    if name.endswith('_maxsz'):
        name = name[6:-6]
        if not name in cstat: cstat[name] = {}
        cstat[name]['maxsz'] = int(value)

print('Kopano Cache Statistics')
print('  Server start time: %s' % ( serverstarttime ))
print('  Current time     : %s' % ( datetime.now().strftime('%c') ))
print('')
print('%10s %24s         %24s' % ('Cache', 'Hit ratio', 'Mem usage ratio'))

for name in cstat:
    if(cstat[name]['requests']):
        percentage = '%d%%' % (cstat[name]['hits'] * 100 / cstat[name]['requests'])
    else:
        percentage = 'N/A'

    if ('maxsz' in cstat[name]):
        persize = '%d%%' % (cstat[name]['size'] * 100 / cstat[name]['maxsz'])
    else:
        persize = 'N/A'

    print('%10s (%12d/%12d) (%3s)   (%12d/%12d) (%3s)' % (name, cstat[name]['hits'], cstat[name]['requests'], percentage, cstat[name]['size'], cstat[name]['maxsz'], persize))
