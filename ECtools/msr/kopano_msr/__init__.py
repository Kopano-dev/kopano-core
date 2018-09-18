#!/usr/bin/python
# SPDX-License-Identifier: AGPL-3.0-only
import codecs
import collections
from contextlib import closing
import fcntl
import multiprocessing
import os.path
from queue import Empty
import sys

import bsddb3 as bsddb
import setproctitle

from .version import __version__
import kopano
from kopano import Config
from kopano import log_exc

CONFIG = {
    'state_path': Config.string(default='/var/lib/kopano/msr/'),
    'server_bind_name': Config.string(default='file:///var/run/kopano/msr.sock'),
    'ssl_private_key_file': Config.path(default=None, check=False), # XXX don't check when default=None?
    'ssl_certificate_file': Config.path(default=None, check=False),
}

LOCK = multiprocessing.Lock()

setproctitle.setproctitle('kopano-msr manager')
_mgr = multiprocessing.Manager()
USER_USER = _mgr.dict() # user mapping
STORE_STORE = _mgr.dict() # store mapping
STORE_FOLDER_QUEUED = _mgr.dict() # ensure max one worker per store (performance, correctness)
STORE_FOLDERS_TODO = _mgr.dict() # store incoming updates until processed
USER_SINK = {} # per-user notification sink

def db_get(db_path, key):
    """ get value from db file """
    key = key.encode('ascii')
    with closing(bsddb.hashopen(db_path, 'c')) as db:
        value = db.get(key)
        if value is not None:
            return db.get(key).decode('ascii')

def db_put(db_path, key, value):
    """ store key, value in db file """
    key = key.encode('ascii')
    with open(db_path+'.lock', 'w') as lockfile:
        fcntl.flock(lockfile.fileno(), fcntl.LOCK_EX)
        with closing(bsddb.hashopen(db_path, 'c')) as db:
            db[key] = value

class ControlWorker(kopano.Worker):
    """ control process """

    def main(self):
        config, server, options = self.service.config, self.service.server, self.service.options
        setproctitle.setproctitle('kopano-msr control')

        def response(conn, msg):
            self.log.info('Response: %s', msg)
            conn.sendall((msg+'\r\n').encode())

        s = kopano.server_socket(config['server_bind_name'], ssl_key=config['ssl_private_key_file'], ssl_cert=config['ssl_certificate_file'], log=self.log)

        while True:
            with log_exc(self.log):
                try:
                    conn = None
                    conn, _ = s.accept()
                    fields_terms = []
                    for data in conn.makefile():
                        self.log.info('CMD: %s' % data.strip())
                        data = data.split()

                        if data[0] == 'ADD':
                            user, target_user = data[1:]
                            self.subscribe.put((user, target_user, True))
                            response(conn, 'OK:')
                            break

                        elif data[0] == 'REMOVE':
                            user = data[1]
                            self.subscribe.put((user, None, False))
                            response(conn, 'OK:')
                            break

                        elif data[0] == 'LIST':
                            response(conn, 'OK: ' + ' '.join(user+':'+target for user, target in USER_USER.items()))
                            break

                        else:
                            response(conn, 'ERROR')
                            break

                except Exception:
                    response(conn, 'ERROR')
                    raise
                finally:
                    if conn:
                        conn.close()

class HierarchyImporter:
    def __init__(self, state_path, store2, store_entryid, iqueue, log):
        self.state_path = state_path
        self.store2 = store2
        self.store_entryid = store_entryid
        self.iqueue = iqueue
        self.log = log

    def update(self, f):
        entryid2 = db_get(self.state_path, 'folder_map_'+f.sourcekey)

        # update (including move)
        if entryid2:
            self.log.info('updated folder: %s (%s)', f.sourcekey, f.name)

            folder2 = self.store2.folder(entryid=entryid2)
            folder2.name = f.name
            folder2.container_class = f.container_class

            parent_sk = f.parent.sourcekey
            parent2_eid = db_get(self.state_path, 'folder_map_'+parent_sk)
            if parent2_eid not in (None, folder2.parent.entryid):
                self.log.info('move to %s', parent2_eid)
                parent2 = self.store2.folder(entryid=parent2_eid)
                folder2.parent.move(folder2, parent2)

        # new
        else:
            path = f.path
            if path is not None: # above subtree
                self.log.info('new folder: %s (%s)', f.sourcekey, f.name)

                folder2 = self.store2.folder(path, create=True)
                db_put(self.state_path, 'folder_map_'+f.sourcekey, folder2.entryid)

                _queue_or_store(self.store_entryid, f.entryid, self.iqueue)

    def delete(self, f, flags):
        self.log.info('deleted folder: %s', f.sourcekey)

        entryid2 = db_get(self.state_path, 'folder_map_'+f.sourcekey)
        folder2 = self.store2.folder(entryid=entryid2)
        self.store2.delete(folder2)

class FolderImporter:
    def __init__(self, state_path, folder2, log):
        self.state_path = state_path
        self.folder2 = folder2
        self.log = log

    def update(self, item, flags):
        self.log.info('new/updated item: %s', item.sourcekey)
        entryid2 = db_get(self.state_path, 'item_map_'+item.sourcekey)
        if entryid2:
            item2 = self.folder2.item(entryid2)
            self.folder2.delete(item2) # TODO remove from db
        item2 = item.copy(self.folder2)
        db_put(self.state_path, 'item_map_'+item.sourcekey, item2.entryid)

    def read(self, item, state):
        self.log.info('changed readstate for item: %s', item.sourcekey)
        entryid2 = db_get(self.state_path, 'item_map_'+item.sourcekey)
        self.folder2.item(entryid2).read = state

    def delete(self, item, flags):
        self.log.info('deleted item: %s', item.sourcekey)
        entryid2 = db_get(self.state_path, 'item_map_'+item.sourcekey)
        self.folder2.delete(self.folder2.item(entryid2))

def _queue_or_store(store_entryid, folder_entryid, iqueue):
    with LOCK:
        if store_entryid in STORE_FOLDER_QUEUED:
            todo = STORE_FOLDERS_TODO.get(store_entryid, set())
            todo.add(folder_entryid)
            STORE_FOLDERS_TODO[store_entryid] = todo
        else:
            STORE_FOLDER_QUEUED[store_entryid] = folder_entryid
            iqueue.put((store_entryid, folder_entryid))

class NotificationSink:
    def __init__(self, store, iqueue, log):
        self.store = store
        self.log = log
        self.iqueue = iqueue

    def update(self, notification):
        self.log.info('notif: %s %s', notification.object_type, notification.event_type)

        if notification.object_type == 'item':
            folder = notification.object.folder
            _queue_or_store(self.store.entryid, folder.entryid, self.iqueue)

        elif notification.object_type == 'folder':
            _queue_or_store(self.store.entryid, None, self.iqueue)

class SyncWorker(kopano.Worker):
    """ worker process """

    def main(self):
        config, server, options = self.service.config, self.service.server, self.service.options
        setproctitle.setproctitle('kopano-msr worker %d' % self.nr)

        while True:
            with log_exc(self.log):
                (store_entryid, folder_entryid) = self.iqueue.get()

                store = self.server.store(entryid=store_entryid)
                user = store.user
                store2 = self.server.store(entryid=STORE_STORE[store_entryid])
                state_path = os.path.join(config['state_path'], store_entryid)

                self.log.info('syncing for user %s', user.name)

                # sync folder
                if folder_entryid:
                    try:
                        folder = store.folder(entryid=folder_entryid)

                        entryid2 = db_get(state_path, 'folder_map_'+folder.sourcekey)
                        folder2 = store2.folder(entryid=entryid2)

                    except kopano.NotFoundError: # TODO further investigate
                        self.log.info('parent folder does not exist (anymore)')
                    else:
                        self.log.info('syncing folder %s (%s)', folder.sourcekey, folder.name)

                        # check previous state
                        state = db_get(state_path, 'folder_state_'+folder.sourcekey)
                        if state:
                            self.log.info('previous folder sync state: %s', state)

                        # sync and store new state
                        importer = FolderImporter(state_path, folder2, self.log)
                        newstate = folder.sync(importer, state)
                        db_put(state_path, 'folder_state_'+folder.sourcekey, newstate)

                # sync hierarchy
                else:
                    self.log.info('syncing hierarchy')

                    # check previous state
                    state = db_get(state_path, 'store_state_'+store_entryid)
                    if state:
                        self.log.info('found previous store sync state: %s', state)

                    # sync and store new state
                    importer = HierarchyImporter(state_path, store2, store_entryid, self.iqueue, self.log)
                    newstate = store.subtree.sync_hierarchy(importer, state)
                    db_put(state_path, 'store_state_'+store_entryid, newstate)

            self.oqueue.put((store_entryid, folder_entryid))

class Service(kopano.Service):
    """ main process """

    def main(self):
        setproctitle.setproctitle('kopano-msr main')

        self.iqueue = multiprocessing.Queue() # folders in the working queue
        self.oqueue = multiprocessing.Queue() # processed folders, used to update STORE_FOLDER_QUEUED
        self.subscribe = multiprocessing.Queue() # subscription update queue

        # initialize and start workers
        workers = [SyncWorker(self, 'msr%d'%i, nr=i, iqueue=self.iqueue, oqueue=self.oqueue)
                       for i in range(self.config['worker_processes'])]
        for worker in workers:
            worker.start()

        control_worker = ControlWorker(self, 'control', subscribe=self.subscribe)
        control_worker.start()

        # continue using notifications
        self.notify_sync()

    def subscribe_user(self, server, user, target_user):
        self.log.info('subscribing: %s -> %s', user, target_user)

        usera = server.user(user)
        userb = server.user(target_user)
        sink = NotificationSink(usera.store, self.iqueue, self.log)

        usera.store.subscribe(sink, object_types=['item', 'folder'])

        STORE_STORE[usera.store.entryid] = userb.store.entryid
        USER_USER[usera.name] = userb.name
        USER_SINK[usera.name] = sink

        _queue_or_store(usera.store.entryid, None, self.iqueue)
        for folder in usera.folders():
            _queue_or_store(usera.store.entryid, folder.entryid, self.iqueue)

    def unsubscribe_user(self, server, user):
        self.log.info('unsubscribing: %s', user)

        usera = server.user(user)
        sink = USER_SINK[usera.name]

        usera.store.unsubscribe(sink)

        del USER_USER[usera.name]
        del USER_SINK[usera.name]
        del STORE_STORE[usera.store.entryid]

    def notify_sync(self):
        server = kopano.Server(notifications=True, options=self.options, parse_args=False) # TODO ugh

        while True:
            # check command-line add-user requests
            try:
                user, target_user, subscribe = self.subscribe.get(timeout=0.01)
                if subscribe:
                    self.subscribe_user(server, user, target_user)
                else:
                    self.unsubscribe_user(server, user)

            except Empty:
                pass

            # check worker output queue: more folders for same store?
            try:
                store_entryid, folder_entryid = self.oqueue.get(timeout=0.01)
                with LOCK:
                    folders_todo = STORE_FOLDERS_TODO.get(store_entryid)
                    if folders_todo:
                        self.iqueue.put((store_entryid, folders_todo.pop()))
                        STORE_FOLDERS_TODO[store_entryid] = folders_todo
                    else:
                        del STORE_FOLDER_QUEUED[store_entryid]
            except Empty:
                pass

    def cmd_add(self, options): # TODO merge common
        if len(options.users) != 1:
            print("please specify single user to add", file=sys.stderr)
            sys.exit(1)

        username = options.users[0]
        targetname = options.target

        for name in (username, targetname):
            if not self.server.get_user(name):
                print("no such user: %s" % name, file=sys.stderr)
                sys.exit(1)

        with closing(kopano.client_socket(self.config['server_bind_name'], ssl_cert=self.config['ssl_certificate_file'])) as s:
            s.sendall(('ADD %s %s\r\n' % (username, targetname)).encode())
            m = s.makefile()
            data = m.readline()
            if not data.startswith('OK'):
                print('kopano-msr returned an error. please check log.', file=sys.stderr)
                sys.exit(1)

    def cmd_remove(self, options):
        if len(options.users) != 1:
            print("please specify single user to remove", file=sys.stderr)
            sys.exit(1)

        username = options.users[0]

        with closing(kopano.client_socket(self.config['server_bind_name'], ssl_cert=self.config['ssl_certificate_file'])) as s:
            s.sendall(('REMOVE %s\r\n' % username).encode())
            m = s.makefile()
            data = m.readline()
            if not data.startswith('OK'):
                print('kopano-msr returned an error. please check log.', file=sys.stderr)
                sys.exit(1)

    def cmd_list(self):
        with closing(kopano.client_socket(self.config['server_bind_name'], ssl_cert=self.config['ssl_certificate_file'])) as s:
            s.sendall(('LIST\r\n').encode())
            m = s.makefile()
            data = m.readline()
            if not data.startswith('OK:'):
                print('kopano-msr returned an error. please check log.', file=sys.stderr)
                sys.exit(1)
            for word in data.split()[1:]:
                print(word.replace(':', ' '))

def main():
    # select common options
    parser = kopano.parser('uclFw')

    # custom options
    parser.add_option('--add', dest='add', action='store_true', help='Add user')
    parser.add_option('--target', dest='target', action='store', help='Specify target user')
    parser.add_option('--remove', dest='remove', action='store_true', help='Remove user')
    parser.add_option('--list-users', dest='list_users', action='store_true', help='List users')

    # parse and check command-line options
    options, args = parser.parse_args()

    service = Service('msr', options=options, config=CONFIG)

    if options.add:
        service.cmd_add(options)
    elif options.remove:
        service.cmd_remove(options)
    elif options.list_users:
        service.cmd_list()
    else:
        # start MSR
        service.start()

if __name__ == '__main__':
    main() # pragma: no cover
