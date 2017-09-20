import datetime
import kopano

for u in kopano.users():
    print('user {}'.format(u.name))
    try:
        findroot = u.root.folder('FINDER_ROOT')
    except:
        print('ERROR getting findroot, skipping user')
        continue
    print('{} {}'.format(findroot.name, findroot.subfolder_count))
    for sf in findroot.folders():
        print('{} {} {}'.format(sf.name, sf.entryid, sf.hierarchyid))
        print( 'created at ' + sf.created)
        if sf.created < datetime.datetime.now()-datetime.timedelta(days=30):
            print('DELETING!')
            findroot.delete(sf)
