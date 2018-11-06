#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# experimental mounting of users/folders/item in filesystem

# usage ./z-fuse.py path (path should exist)

# Copyright (c) 2013 Murat Knecht
# License: MIT

import errno
import stat
import re
import os
import time
import traceback

import fuse
import kopano

fuse.fuse_python_api = (0, 2)

LOG = '/tmp/z-fuse.log'

class ZFilesystem(fuse.Fuse):
    def __init__(self, *args, **kw):
        fuse.Fuse.__init__(self, *args, **kw)

        self.server = kopano.Server()

        self.path_object = {} # XXX caching everything, so don't use this on a large account..

    def getattr(self, path, fh=None):
        print >>open(LOG, 'a'), 'getattr', path, fh
        try:
            splitpath = path.split(os.sep)
            last = splitpath[-1]
            parentpath = os.sep.join(splitpath[:-1])
            st = fuse.Stat()
            st.st_nlink = 1
            if last == '' or last.startswith('User(') or last.startswith('Folder(') or last.startswith('Item('):
                st.st_mode = stat.S_IFDIR | 0o755
            elif last == 'eml':
                st.st_mode = stat.S_IFREG | 0o644
                item = self.path_object[parentpath]
                st.st_size = len(item.eml()) # XXX cache data
            else:
                return -errno.ENOENT
            return st

        except Exception as e:
            print >>open(LOG, 'a'), traceback.format_exc(e)

    def readdir(self, path, fh):
        try:
            print >>open(LOG, 'a'), 'readdir', path, fh

            yield fuse.Direntry('.')
            yield fuse.Direntry('..')
            basename = os.path.basename(path)

            splitpath = path.split(os.sep)
            last = splitpath[-1]

            if path == '/':
                for user in self.server.users():
                    dirname = 'User(%s)' % str(user.name)
                    self.path_object[path+'/'+dirname] = user
                    yield fuse.Direntry(dirname)

            elif last.startswith('User('):
                username = last[5:-1]
                for folder in self.server.user(username).folders():
                    dirname = 'Folder(%s)' % folder.entryid
                    self.path_object[path+'/'+dirname] = folder
                    yield fuse.Direntry(dirname)

            elif last.startswith('Folder('):
                folder = self.path_object[path]
                for item in folder:
                    dirname = 'Item('+item.entryid+')'
                    self.path_object[path+'/'+dirname] = item
                    yield fuse.Direntry(dirname)

            elif last.startswith('Item('):
                item = self.path_object[path]
                yield fuse.Direntry('eml')

            # XXX attachments!

        except Exception as e:
            print >>open(LOG, 'a'), traceback.format_exc(e)
 
    def read(self, path, size, offset):
        try:
            print >>open(LOG, 'a'), 'read', path, size, offset
            splitpath = path.split(os.sep)
            last = splitpath[-1]
            parentpath = os.sep.join(splitpath[:-1])
     
            if last == 'eml':
                item = self.path_object[parentpath]
                eml = item.eml()
                return eml[offset:offset+size]
        except Exception as e:
            print >>open(LOG, 'a'), traceback.format_exc(e)

def main():
    server = ZFilesystem(dash_s_do='setsingle')
    server.parse()
    server.main()

if __name__ == '__main__':
    main()
