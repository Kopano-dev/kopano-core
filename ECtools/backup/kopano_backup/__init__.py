#!/usr/bin/python
# SPDX-License-Identifier: AGPL-3.0-only
from .version import __version__

import binascii
import codecs
from contextlib import closing
import csv
import datetime
import fcntl
from multiprocessing import Queue
import os.path
import shutil
import sys
import time
from xml.etree import ElementTree
import zlib
try:
    import cPickle as pickle
except ImportError:
    import _pickle as pickle
if sys.hexversion >= 0x03000000:
    import bsddb3 as bsddb
else: # pragma: no cover
    import bsddb

from MAPI import (
    PT_UNICODE, PT_ERROR, MAPI_UNICODE, KEEP_OPEN_READWRITE, MAPI_MODIFY,
    ROW_ADD,
)

from MAPI.Defs import (
    PROP_TYPE, CHANGE_PROP_TYPE, HrGetOneProp,
)

from MAPI.Struct import (
    MAPIErrorNotFound, SPropValue, ROWENTRY,
)

from MAPI.Tags import (
    PR_EC_BACKUP_SOURCE_KEY, PR_EC_OUTOFOFFICE, PR_ENTRYID,
    PR_EC_OUTOFOFFICE_SUBJECT, PR_EC_OUTOFOFFICE_MSG, PR_EC_OUTOFOFFICE_FROM,
    PR_EC_OUTOFOFFICE_UNTIL, IID_IExchangeModifyTable, PR_CONTAINER_CLASS_W,
    PR_SOURCE_KEY, PR_ACL_TABLE, PR_STORE_RECORD_KEY, PR_RULES_DATA,
    PR_FREEBUSY_ENTRYIDS, PR_SCHDINFO_DELEGATE_ENTRYIDS, PT_TSTRING,
    PR_EC_WEBACCESS_SETTINGS_W, PR_EC_RECIPIENT_HISTORY_W,
    PR_EC_WEBACCESS_SETTINGS_JSON_W, PR_EC_RECIPIENT_HISTORY_JSON_W,
    PR_EC_WEBAPP_PERSISTENT_SETTINGS_JSON_W, PR_STORE_ENTRYID
)

WEBAPP_SETTINGS = (
    PR_EC_WEBACCESS_SETTINGS_W, PR_EC_RECIPIENT_HISTORY_W,
    PR_EC_WEBACCESS_SETTINGS_JSON_W, PR_EC_RECIPIENT_HISTORY_JSON_W,
    PR_EC_WEBAPP_PERSISTENT_SETTINGS_JSON_W,
)

import kopano
from kopano import log_exc, Config

"""
kopano-backup - a MAPI-level backup/restore tool built on python-kopano.

backup is done incrementally using ICS and can be parallellized over stores.

restore is not parallelized.

items are serialized and maintained in per-folder key-value stores.

metadata such as webapp settings, rules, acls and delegation permissions are also stored per-folder.

differential backups are possible via the --differential option.

basic commands (see --help for all options):

kopano-backup -u user1 -> backup (sync) data for user 'user1' in (new) directory 'user1'
kopano-backup -u user1 -f Inbox -> backup 'Inbox' folder for user 'user1' in (new) directory 'user1'

kopano-backup --restore user1 -> restore data from directory 'user1' to account called 'user1'
kopano-backup --restore -u user2 user1 -> same, but restore to account 'user2'

kopano-backup --stats user1 -> summarize contents of backup directory 'user1', in CSV format
kopano-backup --index user1 -> low-level overview of stored items, in CSV format

options can be combined when this makes sense, for example:

kopano-backup --index user1 -f Inbox/subfolder --recursive --period-begin 2014-01-01

"""

CONFIG = {
    'backup_servers': Config.string(multiple=True, default=None),
}

CACHE_SIZE = 64000000 # XXX make configurable

if sys.hexversion >= 0x03000000:
    def _decode(s):
        return s

    def _encode(s):
        return s

    def pickle_dumps(s):
        return pickle.dumps(s, protocol=2)

    def pickle_loads(s):
        return pickle.loads(s, encoding='bytes')

    def _unbase64(s):
        return codecs.decode(codecs.encode(s, 'ascii'), 'base64')

    def _base64(s):
        return codecs.decode(codecs.encode(s, 'base64').strip(), 'ascii')

else: # pragma: no cover
    def _decode(s):
        return s.decode(getattr(sys.stdin, 'encoding', 'utf8') or 'utf8')

    def _encode(s):
        return s.encode(getattr(sys.stdin, 'encoding', 'utf8') or 'utf8')

    def pickle_dumps(s):
        return pickle.dumps(s, protocol=2)

    def pickle_loads(s):
        return pickle.loads(s)

    def _unbase64(s):
        return s.decode('base64')

    def _base64(s):
        return s.encode('base64').strip()

def _hex(s):
    return codecs.encode(s, 'hex').upper()

def _unhex(s):
    return codecs.decode(s, 'hex')

def fatal(s):
    sys.stderr.write(s + "\n")
    sys.exit(1)

def dbopen(path):
    if sys.hexversion >= 0x03000000:
        return bsddb.hashopen(path, 'c')
    else: # pragma: no cover
        return bsddb.hashopen(_encode(path), 'c')

def _copy_folder_meta(from_dir, to_dir, keep_db=False):
    if not os.path.exists(to_dir):
        os.makedirs(to_dir)

    for filename in ('acl', 'folder', 'rules', 'items', 'index', 'state', 'path'): # 'path' last to indicate completion
        if not (keep_db and filename in ('index', 'items')):
            from_path = from_dir+'/'+filename
            to_path = to_dir+'/'+filename
            if os.path.exists(from_path):
                shutil.copy(from_path, to_dir) # overwrites

def _copy_store_meta(from_dir, to_dir):
    for filename in ('delegates', 'store', 'user'):
        from_path = from_dir+'/'+filename
        to_path = to_dir+'/'+filename
        if os.path.exists(from_path):
            shutil.copy(from_path, to_dir) # overwrites

def _mark_deleted( index, fpath, timestamp, log):
    log.debug("marking deleted folder '%s'", fpath)

    with closing(dbopen(index)) as db_index:
        idx = db_index.get(b'folder')
        d = pickle_loads(idx) if idx else {}
        if not d.get(b'backup_deleted'):
            d[b'backup_deleted'] = timestamp
            db_index[b'folder'] = pickle_dumps(d)

class BackupWorker(kopano.Worker):
    """ each worker takes stores from a queue, and backs them up to disk (or syncs them),
        according to the given command-line options """

    def main(self):
        config, server, options = self.service.config, self.service.server, self.service.options
        while True:
            stats = {'changes': 0, 'deletes': 0, 'errors': 0}
            self.service.stats = stats # XXX generalize
            with log_exc(self.log, stats):
                # get store from input queue
                (store_entryid, username, path) = self.iqueue.get()
                store = server.store(entryid=store_entryid)
                user = store.user

                # from- and to- paths for differential
                self.orig_path = path
                if options.differential:
                    path = 'differential/' + path

                # create main directory
                if not os.path.isdir(path):
                    os.makedirs(path)

                # differential: determine to- and from- backups
                if options.differential:
                    diff_ids = [int(x) for x in os.listdir(path)]
                    if diff_ids:
                        self.orig_path = path + '/' + str(max(diff_ids))
                        diff_id = max(diff_ids)+1
                    else:
                        diff_id = 1
                    path = path + '/' + str(diff_id)
                    self.log.info("performing differential backup from '%s' to '%s'", self.orig_path, path)
                    os.makedirs(path)

                # lock backup dir and sync hierarchy
                with open(path+'/lock', 'w') as lockfile:
                    fcntl.flock(lockfile.fileno(), fcntl.LOCK_EX)
                    t0 = time.time()
                    self.backup_hierarchy(path, stats, options, store, user, server, config)
                    changes = stats['changes'] + stats['deletes']
                    self.log.info('backing up %s took %.2f seconds (%d changes, ~%.2f/sec, %d errors)',
                        path, time.time()-t0, changes, changes/(time.time()-t0), stats['errors'])

            # return statistics in output queue
            self.oqueue.put(stats)

    def _store_props(self, store):
        props = list(store.props())

        for proptag in WEBAPP_SETTINGS:
            if not [prop for prop in props if prop.proptag == proptag]:
                prop = store.get_prop(proptag)
                if prop:
                    props.append(prop)

        return props

    def backup_hierarchy(self, path, stats, options, store, user, server, config):
        # backup user and store properties
        if not options.folders:
            open(path+'/store', 'wb').write(dump_props(self._store_props(store), stats, self.log))
            if user:
                open(path+'/user', 'wb').write(dump_props(user.props(), stats, self.log))
                if not options.skip_meta:
                    open(path+'/delegates', 'wb').write(dump_delegates(user, server, stats, self.log)) # XXX why 'if user'?
                    open(path+'/acl', 'wb').write(dump_acl(store, user, server, stats, self.log))

        # time of last backup
        open(path+'/timestamp', 'wb').write(pickle_dumps(self.service.timestamp))
        if not os.path.exists(path+'/folders'):
            os.makedirs(path+'/folders')

        # check command-line options and collect folders
        self.log.info('backing up: %s', path)
        sk_folder = {}
        folders = list(store.folders())
        subtree = store.subtree
        if options.recursive:
            folders = sum([[f]+list(f.folders()) for f in folders], [])
        for folder in folders:
            if (not store.public and \
                ((options.skip_junk and folder == store.junk) or \
                 (options.skip_deleted and folder == store.wastebasket))):
                continue
            sk_folder[folder.sourcekey] = folder
        sk_dir = sk_struct(self.orig_path, options)

        # differential
        if options.differential:
            for sk, folder in sk_folder.items():
                data_path = path+'/folders/'+folder.sourcekey
                orig_data_path = (self.orig_path+'/'+sk_dir[sk]) if sk in sk_dir else None
                self.backup_folder(data_path, orig_data_path, folder, subtree, config, options, stats, store, user, server)
            return

        # add new folders
        for new_sk in set(sk_folder) - set(sk_dir):
            data_path = path+'/folders/'+new_sk
            folder = sk_folder[new_sk]
            self.backup_folder(data_path, data_path, folder, subtree, config, options, stats, store, user, server)

        # update existing folders
        for both_sk in set(sk_folder) & set(sk_dir):
            data_path = path+'/'+sk_dir[both_sk]
            folder = sk_folder[both_sk]
            self.backup_folder(data_path, data_path, folder, subtree, config, options, stats, store, user, server)

        # timestamp deleted folders
        if not options.folders:
            for del_sk in set(sk_dir) - set(sk_folder):
                fpath = open(path+'/'+sk_dir[del_sk]+'/path', 'rb').read().decode('utf8')
                index = (path+'/'+sk_dir[del_sk]+'/index')
                _mark_deleted(index, fpath, self.service.timestamp, self.log)

    def backup_folder(self, data_path, orig_data_path, folder, subtree, config, options, stats, store, user, server):
        """ backup single folder """

        self.log.debug('backing up folder: "%s" "%s"', store.name, folder.path)

        if not os.path.isdir(data_path):
            os.makedirs(data_path)

        # backup folder properties, path, metadata
        open(data_path+'/path', 'wb').write(folder.path.encode('utf8'))
        open(data_path+'/folder', 'wb').write(dump_props(folder.props(), stats, self.log))
        if not options.skip_meta:
            open(data_path+'/acl', 'wb').write(dump_acl(folder, user, server, stats, self.log))
            open(data_path+'/rules', 'wb').write(dump_rules(folder, user, server, stats, self.log))
        if options.only_meta:
            return

        # sync over ICS, using stored 'state'
        importer = FolderImporter(folder, data_path, config, options, self.log, stats, self.service)
        state = None
        if orig_data_path:
            orig_statepath = '%s/state' % orig_data_path
            if os.path.exists(orig_statepath):
                state = open(orig_statepath, 'rb').read()
                self.log.debug('found previous folder sync state: %s', state)
        new_state = folder.sync(importer, state, log=self.log, stats=stats, begin=options.period_begin, end=options.period_end)
        if new_state != state or options.differential:
            importer.commit()
            statepath = '%s/state' % data_path
            open(statepath, 'wb').write(new_state.encode('ascii'))
            self.log.debug('saved folder sync state: %s', new_state)

class FolderImporter:
    """ tracks changes for a given folder """

    def __init__(self, *args):
        self.folder, self.folder_path, self.config, self.options, self.log, self.stats, self.service = args
        self.reset_cache()

    def reset_cache(self):
        self.item_updates = []
        self.index_updates = []
        self.cache_size = 0

    def update(self, item, flags):
        """ store updated item in 'items' database, and subject and date in 'index' database """

        with log_exc(self.log, self.stats):
            self.log.debug('folder %s: new/updated document with entryid %s, sourcekey %s', self.folder.sourcekey, item.entryid, item.sourcekey)

            data = zlib.compress(item.dumps(attachments=not self.options.skip_attachments, archiver=False, skip_broken=True))
            self.item_updates.append((item.sourcekey, data))

            orig_prop = item.get_prop(PR_EC_BACKUP_SOURCE_KEY)
            if orig_prop:
                orig_prop = _hex(orig_prop.value)
            idx = pickle_dumps({
                b'subject': item.subject,
                b'orig_sourcekey': orig_prop,
                b'last_modified': item.last_modified,
                b'backup_updated': self.service.timestamp,
            })
            self.index_updates.append((item.sourcekey, idx))

            self.stats['changes'] += 1

            self.cache_size += len(data) + len(idx)
            if self.cache_size > CACHE_SIZE:
                self.commit()

    def delete(self, item, flags): # XXX batch as well, 'updating' cache?
        """ deleted item from 'items' and 'index' databases """

        with log_exc(self.log, self.stats):
         with closing(dbopen(self.folder_path+'/items')) as db_items:
          with closing(dbopen(self.folder_path+'/index')) as db_index:

            self.log.debug('folder %s: deleted document with sourcekey %s', self.folder.sourcekey, item.sourcekey)

            # NOTE ICS may generate delete events for items that did not exist
            # before, for example for a new message which has already been
            # deleted in the meantime.
            if item.sourcekey.encode('ascii') in db_items:
                idx = pickle_loads(db_index[item.sourcekey.encode('ascii')])
                idx[b'backup_deleted'] = self.service.timestamp
                db_index[item.sourcekey.encode('ascii')] = pickle_dumps(idx)
            else:
                db_index[item.sourcekey.encode('ascii')] = pickle_dumps({
                    b'backup_deleted': self.service.timestamp
                })
            self.stats['deletes'] += 1

    def commit(self):
        """ commit data to storage """

        t0 = time.time()
        with closing(dbopen(self.folder_path+'/items')) as item_db:
         with closing(dbopen(self.folder_path+'/index')) as index_db:

            for sourcekey, data in self.item_updates:
                item_db[sourcekey.encode('ascii')] = data
            for sourcekey, idx in self.index_updates:
                index_db[sourcekey.encode('ascii')] = idx

        self.log.debug('commit took %.2f seconds (%d items)', time.time()-t0, len(self.item_updates))
        self.reset_cache()

class Service(kopano.Service):
    """ main backup process """

    def main(self):
        self.timestamp = datetime.datetime.now()

        if self.options.restore or (self.options.purge is not None) or self.options.merge:
            data_path = _decode(self.args[0].rstrip('/'))
            with open(data_path+'/lock', 'w') as lockfile:
                fcntl.flock(lockfile.fileno(), fcntl.LOCK_EX)
                if self.options.restore:
                    self.restore(data_path)
                elif (self.options.purge is not None):
                    self.purge(data_path)
                elif self.options.merge:
                    self.merge(data_path)
        else:
            self.backup()

    def backup(self):
        """ create backup workers, determine which stores to queue, start the workers, display statistics """

        self.iqueue, self.oqueue = Queue(), Queue()
        workers = [BackupWorker(self, 'backup%d'%i, nr=i, iqueue=self.iqueue, oqueue=self.oqueue)
                       for i in range(self.config['worker_processes'])]
        for worker in workers:
            worker.start()
        try:
            jobs = self.create_jobs()
        except kopano.Error as e:
            fatal(str(e))
        for job in jobs:
            self.iqueue.put(job)
        self.log.info('queued %d store(s) for parallel backup (%s processes)', len(jobs), len(workers))
        t0 = time.time()
        stats = [self.oqueue.get() for i in range(len(jobs))] # blocking
        changes = sum(s['changes'] + s['deletes'] for s in stats)
        errors = sum(s['errors'] for s in stats)
        self.log.info('queue processed in %.2f seconds (%d changes, ~%.2f/sec, %d errors)',
            (time.time()-t0), changes, changes/(time.time()-t0), errors)

    def restore(self, data_path):
        """ restore data from backup """

        # determine store to restore to
        self.log.info('starting restore of %s', data_path)
        username = os.path.split(data_path)[1]

        try:
            if self.options.users:
                store = self._store(self.options.users[0])
            elif self.options.stores:
                store = self.server.store(self.options.stores[0])
            else:
                store = self._store(username)
        except kopano.NotFoundError:
            store = None

        if not store:
            fatal('unable to open store (username: %s)' % username)

        user = store.user

        # determine stored and specified folders
        path_folder = folder_struct(data_path, self.options)
        paths = self.options.folders or sorted(path_folder.keys())
        if self.options.recursive:
            paths = [path2 for path2 in path_folder for path in paths if (path2+'//').startswith(path+'/')]
        for path in paths:
            if path not in path_folder:
                fatal('no such folder: %s' % path)

        # start restore
        self.log.info('restoring to store %s', store.entryid)
        t0 = time.time()
        stats = {'changes': 0, 'errors': 0}

        # restore specified (parts of) folders
        restored = []
        for path in paths:
            fpath = path_folder[path]
            restore_path = _decode(self.options.restore_root)+'/'+path if self.options.restore_root else path

            if self.options.sourcekeys:
                with closing(dbopen(fpath+'/items')) as db:
                    if not [sk for sk in self.options.sourcekeys if sk.encode('ascii') in db]:
                        continue
            else:
                if self.options.deletes in (None, 'no') and folder_deleted(fpath):
                    continue

                folder = store.subtree.get_folder(restore_path)
                if (folder and not store.public and \
                    ((self.options.skip_junk and folder == store.junk) or \
                    (self.options.skip_deleted and folder == store.wastebasket))):
                        continue

            if not self.options.only_meta:
                folder = store.subtree.folder(restore_path, create=True)
                self.restore_folder(folder, path, fpath, store, store.subtree, stats, user, self.server)
            restored.append((folder, fpath))

        # restore folder-level metadata
        if not (self.options.sourcekeys or self.options.skip_meta):
            self.log.info('restoring metadata')
            for (folder, fpath) in restored:
                load_acl(folder, user, self.server, open(fpath+'/acl', 'rb').read(), stats, self.log)
                load_rules(folder, user, self.server, open(fpath+'/rules', 'rb').read(), stats, self.log)

        # restore store-level metadata (webapp/mapi settings)
        if user and not self.options.folders and not self.options.restore_root and not self.options.skip_meta:
            if os.path.exists('%s/store' % data_path):
                storeprops = pickle_loads(open('%s/store' % data_path, 'rb').read())
                for proptag in WEBAPP_SETTINGS + (PR_EC_OUTOFOFFICE_SUBJECT, PR_EC_OUTOFOFFICE_MSG,
                                PR_EC_OUTOFOFFICE, PR_EC_OUTOFOFFICE_FROM, PR_EC_OUTOFOFFICE_UNTIL):
                    if PROP_TYPE(proptag) == PT_TSTRING:
                        proptag = CHANGE_PROP_TYPE(proptag, PT_UNICODE)
                    value = storeprops.get(proptag)
                    if not value:
                        continue
                    store.mapiobj.SetProps([SPropValue(proptag, value)])
                store.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
            if os.path.exists('%s/delegates' % data_path):
                load_delegates(user, self.server, open('%s/delegates' % data_path, 'rb').read(), stats, self.log)
            if os.path.exists('%s/acl' % data_path):
                load_acl(store, user, self.server, open('%s/acl' % data_path, 'rb').read(), stats, self.log)

        self.log.info('restore completed in %.2f seconds (%d changes, ~%.2f/sec, %d errors)',
            time.time()-t0, stats['changes'], stats['changes']/(time.time()-t0), stats['errors'])

    def purge(self, data_path):
        """ permanently delete old folders/items from backup """

        stats = {'folders': 0, 'items': 0}
        path_folder = folder_struct(data_path, self.options)

        for path, fpath in path_folder.items():
            # check if folder was deleted
            self.log.info('checking folder: %s', path)
            if folder_deleted(fpath):
                if (self.timestamp - folder_deleted(fpath)).days >= self.options.purge:
                    self.log.debug('purging folder')
                    shutil.rmtree(fpath)
                    stats['folders'] += 1

            else: # check all items for deletion
                with closing(dbopen(fpath+'/items')) as db_items:
                    with closing(dbopen(fpath+'/index')) as db_index:
                        delete_items = []
                        for item, idx in db_index.items():
                            d = pickle_loads(idx)
                            backup_deleted = d.get(b'backup_deleted')
                            if backup_deleted and (self.timestamp - backup_deleted).days >= self.options.purge:
                                delete_items.append(item)

                        for item in delete_items:
                            stats['items'] += 1
                            self.log.debug('purging item: %s', item)
                            del db_index[item]
                            if item in db_items:
                                del db_items[item]

        self.log.info('purged %d folders and %d items', stats['folders'], stats['items'])

    def merge(self, data_path):
        """ merge differential backups """

        diff_base = 'differential/'+data_path
        diff_ids = sorted((x for x in os.listdir(diff_base)), key=lambda x: int(x))

        for diff_id in diff_ids:
            diff_path = diff_base+'/'+diff_id
            self.log.info("merging differential backup '%s' into '%s'", diff_path, data_path)

            timestamp = pickle_loads(open(diff_path+'/timestamp', 'rb').read())

            orig_sk_dir = sk_struct(data_path, self.options)
            diff_sk_dir = sk_struct(diff_path, self.options)

            # add new folders
            for new_sk in set(diff_sk_dir) - set(orig_sk_dir):
                from_dir = diff_path+'/'+diff_sk_dir[new_sk]
                to_dir = data_path+'/folders/'+new_sk
                fpath = open(from_dir+'/path', 'rb').read().decode('utf8')
                self.log.debug("merging new folder '%s'", fpath)
                _copy_folder_meta(from_dir, to_dir)

            # update existing folders # XXX check matching & higher syncstate?
            for both_sk in set(orig_sk_dir) & set(diff_sk_dir):
                folder_dir = diff_path+'/'+diff_sk_dir[both_sk]
                fpath = open(folder_dir+'/path', 'rb').read().decode('utf8')
                orig_dir = data_path+'/'+orig_sk_dir[both_sk]

                # now merge new data
                with closing(dbopen(orig_dir+'/items')) as orig_db_items:
                 with closing(dbopen(orig_dir+'/index')) as orig_db_index:
                  with closing(dbopen(folder_dir+'/items')) as diff_db_items:
                   with closing(dbopen(folder_dir+'/index')) as diff_db_index:

                    if diff_db_index: # XXX check higher if file exists
                        self.log.debug("merging '%s' (%d updates)", fpath, len(diff_db_index))

                        for key, value in diff_db_index.items():
                            if key in orig_db_index:
                                idx = pickle_loads(orig_db_index[key])
                                idx.update(pickle_loads(value)) # possibly only update 'backup_deleted' (differential)
                            else:
                                idx = pickle_loads(value)
                            orig_db_index[key] = pickle_dumps(idx)

                            if key in diff_db_items:
                                orig_db_items[key] = diff_db_items[key] # differential may contain pure delete (no item)

                _copy_folder_meta(folder_dir, orig_dir, keep_db=True)

            # timestamp deleted folders
            for del_sk in set(orig_sk_dir) - set(diff_sk_dir):
                orig_dir = data_path+'/'+orig_sk_dir[del_sk]
                fpath = open(orig_dir+'/path', 'rb').read().decode('utf8')

                _mark_deleted(orig_dir+'/index', fpath, timestamp, self.log)

            _copy_store_meta(diff_path, data_path)

    def create_jobs(self):
        """ check command-line options and determine which stores should be backed up """

        output_dir = self.options.output_dir or u''
        jobs = []

        # specified companies/all users
        if self.options.companies or not (self.options.users or self.options.stores):
            servers = self.config['backup_servers']
            if servers is None:
                servers = [self.server.name]
            for company in self.server.companies():
                companyname = company.name if company.name != 'Default' else ''
                for user in company.users():
                    if (not servers or user.home_server in servers) and user.store:
                        jobs.append((user.store, user.name, os.path.join(output_dir, companyname, user.name)))
                public_store = company.public_store
                if public_store and not self.options.skip_public and \
                   (not servers or public_store.home_server in servers):
                    target = 'public@'+companyname if companyname else 'public'
                    jobs.append((company.public_store, None, os.path.join(output_dir, companyname, target)))

        # specified users
        if self.options.users:
            for user in self.server.users():
                if user.store:
                    jobs.append((user.store, user.name, os.path.join(output_dir, user.name)))

        # specified stores
        if self.options.stores:
            for store in self.server.stores():
                if store.public:
                    target = 'public' + ('@'+store.company.name if store.company.name != 'Default' else '')
                else:
                    target = store.guid
                jobs.append((store, None, os.path.join(output_dir, target)))

        # check folders
        if self.options.folders:
            for job in jobs:
                list(job[0].folders())

        return [(job[0].entryid,)+job[1:] for job in sorted(jobs, reverse=True, key=lambda x: x[0].size)]

    def restore_folder(self, folder, path, data_path, store, subtree, stats, user, server):
        """ restore single folder (or potential item in folder) """

        if not self.options.sourcekeys:
            self.log.debug('restoring folder %s', path)

            # restore container class
            folderprops = pickle_loads(open('%s/folder' % data_path, 'rb').read())
            container_class = folderprops.get(PR_CONTAINER_CLASS_W)
            if container_class:
                folder.container_class = container_class

        # load existing sourcekeys in folder, to check for duplicates
        existing = {}
        table = folder.mapiobj.GetContentsTable(0)
        table.SetColumns([PR_SOURCE_KEY, PR_EC_BACKUP_SOURCE_KEY, PR_ENTRYID], 0)
        for row in table.QueryRows(-1, 0):
            if PROP_TYPE(row[1].ulPropTag) != PT_ERROR:
                existing[_hex(row[1].Value)] = _hex(row[2].Value)
            else:
                existing[_hex(row[0].Value)] = _hex(row[2].Value)

        # now dive into 'items', and restore desired items
        with closing(dbopen(data_path+'/items')) as db_items:
          with closing(dbopen(data_path+'/index')) as db_index:
            index = dict((a, pickle_loads(b)) for (a,b) in db_index.iteritems())

            # determine sourcekey(s) to restore
            sourcekeys = db_index.keys()
            if self.options.sourcekeys:
                sourcekeys = [sk for sk in self.options.sourcekeys if sk.encode('ascii') in sourcekeys]
            elif sys.hexversion >= 0x03000000:
                sourcekeys = [sk.decode('ascii') for sk in sourcekeys]

            # restore/delete each item
            for sourcekey2 in sourcekeys:
                sourcekey2a = sourcekey2.encode('ascii')
                with log_exc(self.log, stats):

                    # differential delete
                    if index[sourcekey2a].get(b'backup_deleted') and self.options.differential:
                        if self.options.deletes == 'no':
                            self.log.warning('skipping deleted item with sourcekey %s', sourcekey2)
                            continue
                        if sourcekey2a not in existing:
                            self.log.warning('item with sourcekey %s already deleted', sourcekey2)
                            continue

                        # delete item
                        self.log.debug('deleting item with sourcekey %s', sourcekey2)
                        item = folder.item(entryid=existing[sourcekey2a])
                        folder.delete(item)
                        continue

                    # regular delete
                    if(sourcekey2a not in db_items or \
                       (index[sourcekey2a].get(b'backup_deleted') and self.options.deletes in (None, 'no'))
                    ):
                        continue

                    # date range check
                    last_modified = index[sourcekey2a].get(b'last_modified')
                    if(last_modified and \
                       ((self.options.period_begin and last_modified < self.options.period_begin) or \
                         (self.options.period_end and last_modified >= self.options.period_end))
                    ):
                         continue

                    # restore item
                    if sourcekey2a in existing or index[sourcekey2a][b'orig_sourcekey'] in existing:
                        self.log.warning('skipping duplicate item with sourcekey %s', sourcekey2)
                    else:
                        self.log.debug('restoring item with sourcekey %s', sourcekey2)
                        data = zlib.decompress(db_items[sourcekey2a])
                        item = folder.create_item(loads=data, attachments=not self.options.skip_attachments)

                        # store original sourcekey or it is lost
                        try:
                            item.prop(PR_EC_BACKUP_SOURCE_KEY)
                        except (MAPIErrorNotFound, kopano.NotFoundError):
                            item.mapiobj.SetProps([SPropValue(PR_EC_BACKUP_SOURCE_KEY, _unhex(sourcekey2))])
                            item.mapiobj.SaveChanges(0)

                        stats['changes'] += 1

    def _store(self, username):
        """ lookup store for username """

        if '@' in username:
            u, c = username.split('@')
            if u == 'public' or u == c:
                return self.server.company(c).public_store
            else:
                return self.server.user(username).store
        elif username == 'public':
            return self.server.public_store
        else:
            return self.server.user(username).store

def sk_struct(data_path, options, mapper=None, base_path=None):
    """ determine all folders in backup directory """

    if mapper is None:
        mapper = {}
        base_path = data_path
    if os.path.exists(data_path+'/folders'):
        for f in os.listdir(data_path+'/folders'):
            d = data_path+'/folders/'+f
            if os.path.exists(d+'/path'): # XXX purge empty dirs?
                mapper[d.split('/')[-1]] = d[len(base_path)+1:] # XXX ugly
                sk_struct(d, options, mapper, base_path)
    return mapper

def folder_struct(data_path, options, mapper=None): # XXX deprecate?
    """ determine all folders in backup directory """

    if mapper is None:
        mapper = {}
    if os.path.exists(data_path+'/path'):
        path = open(data_path+'/path', 'rb').read().decode('utf8')
        mapper[path] = data_path
    if os.path.exists(data_path+'/folders'):
        for f in os.listdir(data_path+'/folders'):
            d = data_path+'/folders/'+f
            if os.path.isdir(d):
                folder_struct(d, options, mapper)
    return mapper

def folder_deleted(data_path):
    if os.path.exists(data_path+'/index'):
        with closing(bsddb.hashopen(data_path+'/index')) as db:
           idx = db.get(b'folder')
           if idx and pickle_loads(idx).get(b'backup_deleted'):
               return pickle_loads(idx).get(b'backup_deleted')
    return None

def show_contents(data_path, options):
    """ summary of contents of backup directory, at the item or folder level, in CSV format """

    # setup CSV writer, perform basic checks
    writer = csv.writer(sys.stdout)
    path_folder = folder_struct(data_path, options)
    paths = options.folders or sorted(path_folder)
    for path in paths:
        if path not in path_folder:
            fatal('no such folder: %s' % path)
    if options.recursive:
        paths = [p for p in path_folder if [f for f in paths if p.startswith(f)]]

    # loop over folders
    for path in paths:
        data_path = path_folder[path]
        items = []

        if options.deletes == 'no' and folder_deleted(data_path):
            continue

        # filter items on date using 'index' database
        if os.path.exists(data_path+'/index'):
            with closing(bsddb.hashopen(data_path+'/index')) as db:
                for key, value in db.iteritems():
                    d = pickle_loads(value)
                    if ((key == b'folder') or
                        (options.period_begin and d[b'last_modified'] < options.period_begin) or
                        (options.period_end and d[b'last_modified'] >= options.period_end) or
                        (options.deletes == 'no' and d.get(b'backup_deleted'))):
                        continue
                    if b'last_modified' in d: # ignore sourcekey-only deletes (differential)
                        items.append((key, d))

        # --stats: one entry per folder
        if options.stats:
            writer.writerow([_encode(path), len(items)])

        # --index: one entry per item
        elif options.index:
            items = sorted(items, key=lambda item: item[1][b'last_modified'])
            for key, d in items:
                writer.writerow([key.decode('ascii'), _encode(path), d[b'last_modified'], _encode(d[b'subject'])])

def dump_props(props, stats, log):
    """ dump given MAPI properties """

    data = {}
    with log_exc(log, stats):
        data = dict((prop.proptag, prop.mapiobj.Value) for prop in props)
    return pickle_dumps(data)

def dump_acl(obj, user, server, stats, log):
    """ dump acl for given store or folder """

    rows = []
    with log_exc(log, stats):
        acl_table = obj.mapiobj.OpenProperty(PR_ACL_TABLE, IID_IExchangeModifyTable, 0, 0)
        table = acl_table.GetTable(0)
        for row in table.QueryRows(-1,0):
            entryid = row[1].Value
            try:
                row[1].Value = (b'user', server.sa.GetUser(entryid, MAPI_UNICODE).Username)
            except MAPIErrorNotFound:
                try:
                    row[1].Value = (b'group', server.sa.GetGroup(entryid, MAPI_UNICODE).Groupname)
                except MAPIErrorNotFound:
                    log.warning("skipping access control entry for unknown user/group %s", _hex(entryid))
                    continue
            rows.append(row)
    return pickle_dumps(rows)

def load_acl(obj, user, server, data, stats, log):
    """ load acl for given store or folder """

    with log_exc(log, stats):
        data = pickle_loads(data)
        rows = []
        for row in data:
            try:
                member_type, value = row[1].Value
                if member_type == b'user':
                    entryid = server.user(value).userid
                else:
                    entryid = server.group(value).groupid
                row[1].Value = _unhex(entryid)
                rows.append(row)
            except kopano.NotFoundError:
                log.warning("skipping access control entry for unknown user/group '%s'", value)
        acltab = obj.mapiobj.OpenProperty(PR_ACL_TABLE, IID_IExchangeModifyTable, 0, MAPI_MODIFY)
        acltab.ModifyTable(0, [ROWENTRY(ROW_ADD, row) for row in rows])

def dump_rules(folder, user, server, stats, log):
    """ dump rules for given folder """

    ruledata = None
    with log_exc(log, stats):
        try:
            ruledata = folder.prop(PR_RULES_DATA).value
        except (MAPIErrorNotFound, kopano.NotFoundError):
            pass
        else:
            etxml = ElementTree.fromstring(ruledata)
            for actions in etxml.findall('./item/item/actions'):
                for movecopy in actions.findall('.//moveCopy'):
                    try:
                        s = movecopy.findall('store')[0]
                        store = server.mapisession.OpenMsgStore(0, _unbase64(s.text), None, 0)
                        entryid = _hex(HrGetOneProp(store, PR_STORE_ENTRYID).Value)
                        store = server.store(entryid=entryid)
                        if store.public:
                            s.text = 'public'
                        else:
                            s.text = store.user.name if store != user.store else ''
                        f = movecopy.findall('folder')[0]
                        path = store.folder(entryid=_hex(_unbase64(f.text))).path
                        f.text = path
                    except (MAPIErrorNotFound, kopano.NotFoundError, binascii.Error):
                        log.warning("cannot serialize rule for unknown store/folder")
            ruledata = ElementTree.tostring(etxml)
    return pickle_dumps(ruledata)

def load_rules(folder, user, server, data, stats, log):
    """ load rules for given folder """

    with log_exc(log, stats):
        data = pickle_loads(data)
        if data:
            etxml = ElementTree.fromstring(data)
            for actions in etxml.findall('./item/item/actions'):
                for movecopy in actions.findall('.//moveCopy'):
                    try:
                        s = movecopy.findall('store')[0]
                        if s.text == 'public':
                            store = server.public_store
                        else:
                            store = server.user(s.text).store if s.text else user.store
                        s.text = _base64(_unhex(store.entryid))
                        f = movecopy.findall('folder')[0]
                        f.text = _base64(_unhex(store.folder(f.text).entryid))
                    except kopano.NotFoundError:
                        log.warning("skipping rule for unknown store/folder")
            etxml = ElementTree.tostring(etxml)
            folder.create_prop(PR_RULES_DATA, etxml)

def dump_delegates(user, server, stats, log):
    """ dump delegate users for given user """

    usernames = []
    with log_exc(log, stats):
        try:
            usernames = [d.user.name for d in user.delegations()]
        except (MAPIErrorNotFound, kopano.NotFoundError):
            log.warning("could not load delegations for %s", user.name)

    return pickle_dumps(usernames)

def load_delegates(user, server, data, stats, log):
    """ load delegate users for given user """

    with log_exc(log, stats):
        users = []
        for name in pickle_loads(data):
            try:
                users.append(server.user(name))
            except kopano.NotFoundError:
                log.warning("skipping delegation for unknown user '%s'", name)

        user.delete(user.delegations()) # XXX not in combination with --import-root, -f?
        for user2 in users:
            user.delegation(user2, create=True)

def main():
    # select common options
    parser = kopano.parser('ckpsufwUPCSlObeV', usage='kopano-backup [PATH] [options]')

    # add custom options
    parser.add_option('', '--skip-junk', dest='skip_junk', action='store_true', help='skip junk folder')
    parser.add_option('', '--skip-deleted', dest='skip_deleted', action='store_true', help='skip deleted items folder')
    parser.add_option('', '--skip-public', dest='skip_public', action='store_true', help='skip public store')
    parser.add_option('', '--skip-attachments', dest='skip_attachments', action='store_true', help='skip attachments')
    parser.add_option('', '--skip-meta', dest='skip_meta', action='store_true', help='skip metadata')
    parser.add_option('', '--only-meta', dest='only_meta', action='store_true', help='only backup/restore metadata')
    parser.add_option('', '--deletes', dest='deletes', help='store/restore deleted items/folders', metavar='YESNO')
    parser.add_option('', '--purge', dest='purge', type='int', help='purge items/folders deleted more than N days ago', metavar='N')
    parser.add_option('', '--restore', dest='restore', action='store_true', help='restore from backup')
    parser.add_option('', '--restore-root', dest='restore_root', help='restore under specific folder', metavar='PATH')
    parser.add_option('', '--stats', dest='stats', action='store_true', help='list folders for PATH')
    parser.add_option('', '--index', dest='index', action='store_true', help='list items for PATH')
    parser.add_option('', '--sourcekey', dest='sourcekeys', action='append', help='restore specific sourcekey', metavar='SOURCEKEY')
    parser.add_option('', '--recursive', dest='recursive', action='store_true', help='backup/restore folders recursively')
    parser.add_option('', '--differential', dest='differential', action='store_true', help='create/restore differential backup')
    parser.add_option('', '--merge', dest='merge', action='store_true', help='merge differential backups')

    # parse and check command-line options
    options, args = parser.parse_args()

    options.service = False
    if options.restore or options.merge or options.stats or options.index or (options.purge is not None):
        if len(args) != 1 or not os.path.isdir(args[0]):
            fatal('please specify path to backup data')
    elif len(args) != 0:
        fatal('too many arguments')
    if options.deletes and options.deletes not in ('yes', 'no'):
        fatal("--deletes option takes 'yes' or 'no'")
    if options.folders and (options.differential or (options.purge is not None) or options.merge):
        fatal('invalid use of --folder option')
    if options.output_dir and options.differential:
        fatal('invalid use of --output-dir option')

    if options.stats or options.index:
        # handle --stats/--index
        show_contents(args[0], options)
    else:
        # start backup/restore
        Service('backup', options=options, config=CONFIG, args=args).start()

if __name__ == '__main__':
    main() # pragma: no cover
