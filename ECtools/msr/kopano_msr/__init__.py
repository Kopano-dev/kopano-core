#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only
import codecs
import collections
from contextlib import closing
import fcntl
import multiprocessing
import optparse
import os.path
import pickle
from queue import Empty
import sys

import bsddb3 as bsddb
import setproctitle

from .version import __version__

import kopano
from kopano import Config
from kopano import log_exc

"""
kopano-msr - live store migration tool

parallelized over stores, with max 1 worker per store.

migration settings and sync states are persistently stored in 'state_path'.

migration management is done from command-line (using socket for communication).

basic commands (see --help for all options):

kopano-msr --list-users -> list users currently being migrated

kopano-msr -u user1 --add --server node14 -> start migrating user

kopano-msr -u user1 ->  check migration status, compare store contents

kopano-msr -u user1 --remove -> finalize migration (metadata, special folders..)

"""

CONFIG = {
    'state_path': Config.path(default='/var/lib/kopano/msr/', check=True),
    'server_bind_name': Config.string(default='file:///var/run/kopano/msr.sock'),
    'ssl_private_key_file': Config.path(default=None, check=False), # XXX don't check when default=None?
    'ssl_certificate_file': Config.path(default=None, check=False),
}

setproctitle.setproctitle('kopano-msr main')

def init_globals(): # late init to survive daemonization
    global LOCK, STORE_STORE, USER_INFO, STORE_FOLDER_QUEUED, STORE_FOLDERS_TODO, USER_SINK

    LOCK = multiprocessing.Lock()

    _mgr = multiprocessing.Manager()

    # TODO persistency
    STORE_STORE = _mgr.dict() # store mapping
    USER_INFO = _mgr.dict() # statistics

    # ensure max one worker per store (performance, correctness)
    # storeid -> foldereid (specific folder sync) or None (for hierarchy sync)
    STORE_FOLDER_QUEUED = _mgr.dict()

    # store incoming updates until processed
    # storeid -> foldereids (or None for hierarchy sync)
    STORE_FOLDERS_TODO = _mgr.dict()

    USER_SINK = {} # per-user notification sink

def db_get(db_path, key, decode=True):
    """ get value from db file """
    key = key.encode('ascii')
    with closing(bsddb.hashopen(db_path, 'c')) as db:
        value = db.get(key)
        if value is not None and decode:
            value = value.decode('ascii')
        return value

def db_put(db_path, key, value):
    """ store key, value in db file """
    key = key.encode('ascii')
    with open(db_path+'.lock', 'w') as lockfile:
        fcntl.flock(lockfile.fileno(), fcntl.LOCK_EX)
        with closing(bsddb.hashopen(db_path, 'c')) as db:
            db[key] = value

def update_user_info(state_path, user, key, op, value):
    data = USER_INFO.get(user.name, {})
    if op == 'add':
        if key not in data:
            data[key] = 0
        data[key] += value
    elif op == 'store':
        data[key] = value
    USER_INFO[user.name] = data

    data = pickle.dumps(data)
    db_put(state_path, 'info', data)

def _queue_or_store(state_path, user, store_entryid, folder_entryid, iqueue):
    with LOCK:
        update_user_info(state_path, user, 'queue_empty', 'store', 'no')

        if store_entryid in STORE_FOLDER_QUEUED:
            todo = STORE_FOLDERS_TODO.get(store_entryid, set())
            todo.add(folder_entryid)
            STORE_FOLDERS_TODO[store_entryid] = todo
        else:
            STORE_FOLDER_QUEUED[store_entryid] = folder_entryid
            iqueue.put((store_entryid, folder_entryid))

class ControlWorker(kopano.Worker):
    """ control process """

    def user_details(self, server, username):
        lines = []

        # general info
        data = USER_INFO[username]
        lines.append('user:               %s' % username)
        lines.append('target server:      %s' % data['server'])
        lines.append('processed items:    %d' % data['items'])
        lines.append('initial sync done:  %s' % data['init_done'])
        lines.append('update queue empty: %s' % data['queue_empty'])
        lines.append('')

        # store contents comparison
        lines.append('store comparison:')

        storea = server.user(username).store
        storeb = server.store(entryid=STORE_STORE[storea.entryid])

        difference = False

        foldersa = set(f.path for f in storea.folders())
        foldersb = set(f.path for f in storeb.folders())

        for path in foldersb - foldersa:
            lines.append('%s: folder only exists in target store' % path)
            difference = True

        for path in foldersa - foldersb:
            lines.append('%s: folder only exists in source store' % path)
            difference = True

        for path in foldersa & foldersb:
            counta = storea.folder(path).count
            countb = storeb.folder(path).count
            if counta != countb:
                lines.append("%s: item count is %d compared to source %d" % (path, countb, counta))
                difference = True

        if not difference:
            lines.append('folder structure and item counts are identical')

        return lines

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
                    for data in conn.makefile():
                        self.log.info('CMD: %s', data.strip())
                        data = data.split()

                        if data[0] == 'ADD':
                            user, target_user, target_server = data[1:]
                            if user in USER_INFO:
                                self.log.error('user %s is already being relocated', user) # TODO return error
                                response(conn, 'ERROR')
                                break
                            self.subscribe.put((user, target_user, target_server, True, None))
                            response(conn, 'OK:')
                            break

                        elif data[0] == 'REMOVE':
                            user = data[1]
                            if user not in USER_INFO:
                                self.log.error('user %s is not being relocated', user) # TODO return error
                                response(conn, 'ERROR')
                                break
                            self.subscribe.put((user, None, None, False, None))
                            response(conn, 'OK:')
                            break

                        elif data[0] == 'LIST':
                            lines = []
                            for user, data in USER_INFO.items():
                                items = data['items']
                                init_done = data['init_done']
                                queue_empty = data['queue_empty']
                                servername = data['server']
                                line = 'user=%s server=%s items=%d init_done=%s queue_empty=%s' % (user, servername, items, init_done, queue_empty)
                                lines.append(line)
                            response(conn, 'OK:\n' + '\n'.join(lines))
                            break

                        elif data[0] == 'DETAILS':
                            response(conn, 'OK:\n' + '\n'.join(self.user_details(server, data[1])))
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
    def __init__(self, state_path, store2, store_entryid, iqueue, subtree_sk, user, log):
        self.state_path = state_path
        self.store2 = store2
        self.store_entryid = store_entryid
        self.iqueue = iqueue
        self.subtree_sk = subtree_sk
        self.user = user
        self.log = log

    def update(self, f):
        with log_exc(self.log): # TODO use decorator?
            entryid2 = db_get(self.state_path, 'folder_map_'+f.sourcekey)
            psk = f.parent.sourcekey or self.subtree_sk # TODO default pyko to subtree_sk?

            self.log.info('updated: %s', f)

            parent2_eid = db_get(self.state_path, 'folder_map_'+psk)
            parent2 = self.store2.folder(entryid=parent2_eid)

            self.log.info('parent: %s', parent2)

            if entryid2:
                self.log.info('exists')

                folder2 = self.store2.folder(entryid=entryid2)
                folder2.name = f.name
                folder2.container_class = f.container_class

                if folder2.parent.entryid != parent2_eid:
                    self.log.info('move folder')

                    folder2.parent.move(folder2, parent2)

            else:
                self.log.info('create')

                folder2 = parent2.folder(f.name, create=True)

                db_put(self.state_path, 'folder_map_'+f.sourcekey, folder2.entryid)

            _queue_or_store(self.state_path, self.user, self.store_entryid, f.entryid, self.iqueue)

    def delete(self, f, flags):
        with log_exc(self.log):
            self.log.info('deleted folder: %s', f.sourcekey)

            entryid2 = db_get(self.state_path, 'folder_map_'+f.sourcekey)
            if entryid2: # TODO why this check
                folder2 = self.store2.folder(entryid=entryid2)
                self.store2.delete(folder2)

            # TODO delete from mapping

class FolderImporter:
    def __init__(self, state_path, folder2, user, log):
        self.state_path = state_path
        self.folder2 = folder2
        self.user = user
        self.log = log

    def update(self, item, flags):
        with log_exc(self.log):
            self.log.info('new/updated item: %s', item.sourcekey)
            entryid2 = db_get(self.state_path, 'item_map_'+item.sourcekey)
            if entryid2:
                item2 = self.folder2.item(entryid2)
                self.folder2.delete(item2) # TODO remove from db
            item2 = item.copy(self.folder2)
            db_put(self.state_path, 'item_map_'+item.sourcekey, item2.entryid)
            update_user_info(self.state_path, self.user, 'items', 'add', 1)

    def read(self, item, state):
        with log_exc(self.log):
            self.log.info('changed readstate for item: %s', item.sourcekey)
            entryid2 = db_get(self.state_path, 'item_map_'+item.sourcekey)
            self.folder2.item(entryid2).read = state

    def delete(self, item, flags):
        with log_exc(self.log):
            self.log.info('deleted item: %s', item.sourcekey)
            entryid2 = db_get(self.state_path, 'item_map_'+item.sourcekey)
            self.folder2.delete(self.folder2.item(entryid2))

class NotificationSink:
    def __init__(self, state_path, user, store, iqueue, log):
        self.state_path = state_path
        self.user = user
        self.store = store
        self.iqueue = iqueue
        self.log = log

    def update(self, notification):
        with log_exc(self.log):
            self.log.info('notif: %s %s', notification.object_type, notification.event_type)

            if notification.object_type == 'item':
                folder = notification.object.folder
                _queue_or_store(self.state_path, self.user, self.store.entryid, folder.entryid, self.iqueue)

            elif notification.object_type == 'folder':
                _queue_or_store(self.state_path, self.user, self.store.entryid, None, self.iqueue)

class SyncWorker(kopano.Worker):
    """ worker process """

    def main(self):
        config, server, options = self.service.config, self.service.server, self.service.options
        setproctitle.setproctitle('kopano-msr worker %d' % self.nr)

        while True:
            store_entryid, folder_entryid = self.iqueue.get()

            with log_exc(self.log):

                store = self.server.store(entryid=store_entryid)
                user = store.user
                store2 = self.server.store(entryid=STORE_STORE[store_entryid])
                state_path = os.path.join(config['state_path'], user.name)

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
                        importer = FolderImporter(state_path, folder2, user, self.log)
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
                    importer = HierarchyImporter(state_path, store2, store_entryid, self.iqueue, store.subtree.sourcekey, user, self.log)
                    newstate = store.subtree.sync_hierarchy(importer, state)
                    db_put(state_path, 'store_state_'+store_entryid, newstate)

            self.oqueue.put((store_entryid, folder_entryid))

class Service(kopano.Service):
    """ main process """

    def main(self):
        init_globals()

        setproctitle.setproctitle('kopano-msr service')

        self.iqueue = multiprocessing.Queue() # folders in the working queue
        self.oqueue = multiprocessing.Queue() # processed folders, used to update STORE_FOLDER_QUEUED
        self.subscribe = multiprocessing.Queue() # subscription update queue

        self.state_path = self.config['state_path']

        # initialize and start workers
        workers = [SyncWorker(self, 'msr%d'%i, nr=i, iqueue=self.iqueue, oqueue=self.oqueue)
                       for i in range(self.config['worker_processes'])]
        for worker in workers:
            worker.start()

        control_worker = ControlWorker(self, 'control', subscribe=self.subscribe)
        control_worker.start()

        # resume relocations
        for username in os.listdir(self.state_path):
            if not username.endswith('.lock'):
                with log_exc(self.log):
                    state_path = os.path.join(self.state_path, username)
                    info = pickle.loads(db_get(state_path, 'info', decode=False))

                    self.subscribe.put((username, info['target'], info['server'], True, info['store']))

        # continue using notifications
        self.notify_sync()

    def subscribe_user(self, server, username, target_user, target_server, storeb_eid):
        self.log.info('subscribing: %s -> %s', username, target_server)

        usera = server.user(username)
        storea = usera.store

        # TODO report errors back directly to command-line process
        if storeb_eid:
            storeb = server.store(entryid=storeb_eid)
        elif target_server != '_':
            try:
                server2 = kopano.server(
                    server_socket=target_server,
                    sslkey_file=server.sslkey_file,
                    sslkey_pass=server.sslkey_pass
                )
            except Exception as e:
                self.log.error("could not connect to server: %s (%s)" % (target_server, e), file=sys.stderr)
            try:
                userb = server2.user(username) # TODO assuming the user is there?
                storeb = server2.create_store(userb, _msr=True)
                storeb.subtree.empty() # remove default english special folders
            except kopano.DuplicateError:
                self.log.error('user already has hooked store on server %s, try to unhook first' % target_server)
                return
        else:
            storeb = server.user(target_user).store

        state_path = os.path.join(self.state_path, usera.name)

        sink = NotificationSink(state_path, usera, usera.store, self.iqueue, self.log)

        storea.subscribe(sink, object_types=['item', 'folder'])

        STORE_STORE[storea.entryid] = storeb.entryid
        USER_SINK[username] = sink
        update_user_info(state_path, usera, 'items', 'store', 0)
        update_user_info(state_path, usera, 'init_done', 'store', 'no')
        update_user_info(state_path, usera, 'queue_empty', 'store', 'no')
        update_user_info(state_path, usera, 'target', 'store', target_user)
        update_user_info(state_path, usera, 'server', 'store', target_server)
        update_user_info(state_path, usera, 'store', 'store', storeb.entryid)

        db_put(state_path, 'folder_map_'+storea.subtree.sourcekey, storeb.subtree.entryid)

        _queue_or_store(state_path, usera, storea.entryid, None, self.iqueue)

    def unsubscribe_user(self, server, username):
        self.log.info('unsubscribing: %s', username)

        storea = server.user(username).store
        storeb = server.store(entryid=STORE_STORE[storea.entryid])

        # unsubscribe user from notification
        sink = USER_SINK[username]
        storea.unsubscribe(sink)

        # unregister user everywhere
        del USER_SINK[username]
        del STORE_STORE[storea.entryid]
        del USER_INFO[username]

        # set special folders
        for attr in (
            'calendar',
            'contacts',
            'wastebasket',
            'drafts',
            'inbox',
            'journal',
            'junk',
            'notes',
            'outbox',
            'sentmail',
            'tasks',
        ):
            with log_exc(self.log):
                setattr(storeb, attr, storeb.folder(getattr(storea, attr).path))

        # transfer metadata
        with log_exc(self.log): # TODO testing: store deleted in tearDown
            storeb.settings_loads(storea.settings_dumps())

            for foldera in storea.folders():
                if foldera.path:
                    folderb = storeb.get_folder(foldera.path)
                    if folderb:
                        folderb.settings_loads(foldera.settings_dumps())

        # remove state dir
        if self.state_path:
            os.system('rm -rf %s/%s' % (self.state_path, username))
            os.system('rm -rf %s/%s.lock' % (self.state_path, username))

    def notify_sync(self):
        server = kopano.server(notifications=True, options=self.options) # TODO ugh

        while True:
            # check command-line add-user requests
            with log_exc(self.log):
                try:
                    user, target_user, target_server, subscribe, store_eid = self.subscribe.get(timeout=0.01)
                    if subscribe:
                        self.subscribe_user(server, user, target_user, target_server, store_eid)
                    else:
                        self.unsubscribe_user(server, user)

                except Empty:
                    pass

            # check worker output queue: more folders for same store?
            try:
                store_entryid, _ = self.oqueue.get(timeout=0.01)
                with LOCK:
                    folders_todo = STORE_FOLDERS_TODO.get(store_entryid)
                    if folders_todo:
                        folder_entryid = folders_todo.pop()
                        STORE_FOLDER_QUEUED[store_entryid] = folder_entryid
                        self.iqueue.put((store_entryid, folder_entryid))
                        STORE_FOLDERS_TODO[store_entryid] = folders_todo
                    else:
                        del STORE_FOLDER_QUEUED[store_entryid]

                        store = server.store(entryid=store_entryid)
                        user = store.user # TODO shall we use only storeid..
                        state_path = os.path.join(self.state_path, user.name)
                        update_user_info(state_path, user, 'init_done', 'store', 'yes')
                        update_user_info(state_path, user, 'queue_empty', 'store', 'yes')

            except Empty:
                pass

    def do_cmd(self, cmd):
        try:
            with closing(kopano.client_socket(self.config['server_bind_name'], ssl_cert=self.config['ssl_certificate_file'])) as s:
                s.sendall(cmd.encode())
                m = s.makefile()
                line = m.readline()
                if not line.startswith('OK'):
                    print('kopano-msr returned an error. please check log.', file=sys.stderr)
                    sys.exit(1)
                for line in m:
                    print(line.rstrip())
        except OSError as e:
            print('could not connect to msr socket (%s)' % e)
            sys.exit(1)

    def cmd_add(self, options):
        username = options.users[0]
        for name in (username, options.target):
            if name and not self.server.get_user(name):
                print("no such user: %s" % name, file=sys.stderr)
                sys.exit(1)
        if options.server:
            try:
                ts = kopano.server(
                    server_socket=options.server,
                    sslkey_file=self.server.sslkey_file,
                    sslkey_pass=self.server.sslkey_pass
                )
            except Exception as e:
                print("could not connect to server: %s (%s)" % (options.server, e), file=sys.stderr)
                sys.exit(1)
        self.do_cmd('ADD %s %s %s\r\n' % (username, options.target or '_', options.server or '_'))

    def cmd_remove(self, options):
        self.do_cmd('REMOVE %s\r\n' % options.users[0])

    def cmd_list(self):
        self.do_cmd('LIST\r\n')

    def cmd_details(self, options):
        username = options.users[0]
        if not self.server.get_user(username):
            print("no such user: %s" % username, file=sys.stderr)
            sys.exit(1)
        self.do_cmd('DETAILS %s\r\n' % options.users[0])

def main():
    # select common options
    parser = kopano.parser('SUPKQCFulw')

    # custom options
    parser.add_option('--add', dest='add', action='store_true', help='Add user')
    parser.add_option('--target', dest='target', action='store', help=optparse.SUPPRESS_HELP) # TODO remove (still used in testset/msr)
    parser.add_option('--server', dest='server', action='store', help='Specify target server')
    parser.add_option('--remove', dest='remove', action='store_true', help='Remove user')
    parser.add_option('--list-users', dest='list_users', action='store_true', help='List users')

    # parse and check command-line options
    options, args = parser.parse_args()

    service = Service('msr', options=options, config=CONFIG)

    if options.users:
        if options.list_users:
            print('too many options provided')
            sys.exit(1)
        if len(options.users) != 1:
            print('more than one user specified', file=sys.stderr)
            sys.exit(1)
        if options.add:
            if not (options.target or options.server):
                print('no server specified', file=sys.stderr)
                sys.exit(1)
            service.cmd_add(options)
        elif options.remove:
            service.cmd_remove(options)
        else:
            service.cmd_details(options)

    elif options.list_users:
        if options.add or options.remove or options.target or options.server:
            print('too many options provided')
            sys.exit(1)
        service.cmd_list()

    else:
        if options.add or options.remove or options.target or options.server:
            print('no user specified')
            sys.exit(1)
        service.start()

if __name__ == '__main__':
    main() # pragma: no cover
