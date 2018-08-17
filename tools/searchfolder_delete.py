# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import print_function

import datetime
import kopano

for u in kopano.users():
    print('user', u.name)
    try:
        findroot = u.root.folder('FINDER_ROOT')
    except:
        print('ERROR getting findroot, skipping user')
        continue
    print(findroot.name, findroot.subfolder_count)
    for sf in findroot.folders():
        print(sf.name, sf.entryid, sf.hierarchyid)
        print('created at ' + sf.created)
        if sf.created < datetime.datetime.now()-datetime.timedelta(days=30):
            print('DELETING!')
            findroot.delete(sf)
