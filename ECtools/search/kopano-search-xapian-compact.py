#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only
from __future__ import print_function
import fcntl
import glob
import grp
import os
import pwd
import shutil
import subprocess
import sys
import traceback

import kopano

"""
uses 'xapian-compact' to compact given database(s), specified using store GUID(s),
or all databases found in the 'index_path' directory specified in search.cfg.

"""

# TODO: purge removed users (stores)

def _default(name):
    if name == '':
        return 'root'
    elif name is None:
        return 'kopano'
    else:
        return name

def main():
    parser = kopano.parser() # select common cmd-line options
    options, args = parser.parse_args()

    config = kopano.Config(None, service='search', options=options)
    server = kopano.server(options, config, parse_args=True)

    # get index_path, run_as_user, run_as_group from  search.cfg
    search_config = server.config
    if not search_config:
        print('ERROR: search config is not available', file=sys.stderr)
        sys.exit(1)

    index_path = _default(search_config.get('index_path'))
    search_user = _default(search_config.get('run_as_user'))
    uid = pwd.getpwnam(search_user).pw_uid
    search_group = _default(search_config.get('run_as_group'))
    gid = grp.getgrnam(search_group).gr_gid

    if (uid, gid) != (os.getuid(), os.getgid()):
        print('ERROR: script should run under user/group as specified in search.cfg', file=sys.stderr)
        sys.exit(1)

    cleaned_args = [arg for arg in sys.argv[:1] if not arg.startswith('-')]
    # find database(s) corresponding to given store GUID(s)
    dbpaths = []
    if len(cleaned_args) > 1:
        for arg in cleaned_args[1:]:
            dbpath = os.path.join(index_path, server.guid+'-'+arg)
            dbpaths.append(dbpath)
    else:  # otherwise, select all databases for compaction
        dbpaths = []
        for path in glob.glob(os.path.join(index_path, server.guid+'-*')):
            if not path.endswith('.lock'):
                dbpaths.append(path)

    # loop and compact
    count = len(dbpaths)
    errors = 0
    for dbpath in dbpaths:
        try:
            if os.path.isdir(dbpath):
                if len(dbpath.split('-')) > 1:
                    store = dbpath.split('-')[1]
                    try:
                        server.store(store)
                    except kopano.NotFoundError as e:
                        print('deleting:', dbpath)
                        shutil.rmtree(dbpath)
                        os.remove('%s.lock' % dbpath)
                        continue

                print('compact:', dbpath)

                with open('%s.lock' % dbpath, 'w') as lockfile:  # do not index at the same time
                    fcntl.flock(lockfile.fileno(), fcntl.LOCK_EX)
                    shutil.rmtree(dbpath + '.compact', ignore_errors=True)
                    subprocess.call(['xapian-compact', dbpath, dbpath + '.compact'])
                    # quickly swap in compacted database
                    shutil.move(dbpath, dbpath + '.old')
                    shutil.move(dbpath + '.compact', dbpath)
                    shutil.rmtree(dbpath + '.old')
            else:
                print('ERROR: no such database:', dbpath)
                errors += 1
            print('')
        except Exception as e:
            print('ERROR', file=sys.stderr)
            traceback.print_exc(e)
            errors += 1

    # summarize
    print('done compacting (%d processed, %d errors)' % (count, errors))

if __name__ == '__main__':
    main()
