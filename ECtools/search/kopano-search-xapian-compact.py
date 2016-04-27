#!/usr/bin/env python
import fcntl
import os
import shutil
import subprocess
import sys
import traceback

import kopano

def cmd(c):
    print c
    assert os.system(c) == 0

def main():
    print 'kopano-search-xapian-compact started'
    server = kopano.Server()
    index_path = kopano.Config(None, 'search').get('index_path')
    if len(sys.argv) > 1:
        stores = [server.store(arg) for arg in sys.argv[1:]]
    else:
        stores = server.stores()
    for store in stores:
        try:
            dbpath = os.path.join(index_path, server.guid+'-'+store.guid)
            print 'compact:', dbpath
            if os.path.isdir(dbpath):
                with open('%s.lock' % dbpath, 'w') as lockfile: # do not index at the same time
                    fcntl.flock(lockfile.fileno(), fcntl.LOCK_EX)
                    shutil.rmtree(dbpath+'.compact', ignore_errors=True)
                    subprocess.call(['xapian-compact', dbpath, dbpath+'.compact'])
                    # quickly swap in compacted database
                    shutil.move(dbpath, dbpath+'.old')
                    shutil.move(dbpath+'.compact', dbpath)
                    shutil.rmtree(dbpath+'.old')
            else:
                print 'WARNING: does not (yet) exist (new user?)'
            print
        except Exception, e:
            print 'ERROR'
            traceback.print_exc(e)
    print 'kopano-search-xapian-compact done'

if __name__ == '__main__':
    main()
