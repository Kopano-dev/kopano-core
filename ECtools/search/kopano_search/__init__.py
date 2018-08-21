#!/usr/bin/python
# SPDX-License-Identifier: AGPL-3.0-only
from __future__ import print_function
from .version import __version__

import collections
from contextlib import closing
import codecs
import fcntl
import os.path

from multiprocessing import Queue, Value
import time
import sys

if sys.hexversion >= 0x03000000:
    import bsddb3 as bsddb
    from queue import Empty
else: # pragma: no cover
    import bsddb
    from Queue import Empty

from kopano_search import plaintext
import kopano
from kopano import log_exc, Config
sys.path.insert(0, os.path.dirname(__file__)) # XXX for __import__ to work

"""
kopano-search v3 - a process-based indexer and query handler built on python-kopano

initial indexing is performed in parallel using N instances of class IndexWorker, fed with a queue containing folder-ids.

incremental indexing is later performed in the same fashion. when there is no pending work, we
check if there are reindex requests and handle one of these (again parallellized). so incremental syncing is currently paused
during reindexing.

search queries from outlook/webapp are dealt with by a single instance of class SearchWorker.

since ICS does not know for deletion changes which store they belong to, we remember ourselves using a berkeleydb file ("serverguid_mapping").

we also maintain folder and server ICS states in a berkeleydb file, for example so we can resume initial indexing ("serverguid_state").
after initial indexing folder states are not updated anymore.

the used search engine is pluggable, but by default we use xapian (plugin_xapian.py).

a tricky bit is that outlook/exchange perform 'prefix' searches by default and we want to be compatible with this, so terms get an
implicit '*' at the end. not every search engine may perform well for this, but we could make this configurable perhaps.

"""

CONFIG = {
    'coredump_enabled': Config.ignore(),
    'index_attachments': Config.boolean(default=True),
    'index_attachment_extension_filter': Config.ignore(),
    'index_attachment_mime_filter': Config.ignore(),
    'index_attachment_max_size': Config.size(default=2**24),
    'index_attachment_parser': Config.ignore(),
    'index_attachment_parser_max_memory': Config.ignore(),
    'index_attachment_parser_max_cputime': Config.ignore(),
    # 0x007D: PR_TRANSPORT_MESSAGE_HEADERS
    # 0x0064: PR_SENT_REPRESENTING_ADDRTYPE
    # 0x0C1E: PR_SENDER_ADDRTYPE
    # 0x0075: PR_RECEIVED_BY_ADDRTYPE
    # 0x678E: PR_EC_IMAP_BODY
    # 0x678F: PR_EC_IMAP_BODYSTRUCTURE
    # 0x001A: PR_MESSAGE_CLASS # XXX add to cfg
    'index_exclude_properties': Config.integer(multiple=True, base=16, default=[0x007D, 0x0064, 0x0C1E, 0x0075, 0x678E, 0x678F, 0x001A]),
    'index_path': Config.string(default='/var/lib/kopano/search/'),
    'index_processes': Config.integer(default=1),
    'limit_results': Config.integer(default=0),
    'optimize_age': Config.ignore(),
    'optimize_start': Config.ignore(),
    'optimize_stop': Config.ignore(),
    'run_as_user': Config.string(default="kopano"),
    'run_as_group': Config.string(default="kopano"),
    'search_engine': Config.string(default='xapian'),
    'suggestions': Config.boolean(default=True),
    'index_junk': Config.boolean(default=True),
    'index_drafts': Config.boolean(default=True),
    'server_bind_name': Config.string(default='file:///var/run/kopano/search.sock'),
    'ssl_private_key_file': Config.path(default=None, check=False), # XXX don't check when default=None?
    'ssl_certificate_file': Config.path(default=None, check=False),
    'term_cache_size': Config.size(default=64000000),
}

if sys.hexversion >= 0x03000000:
    unicode = str

    def _is_unicode(s):
        return isinstance(s, str)

    def _decode(s):
        return s

    def _decode_ascii(s):
        return s.decode('ascii')

    def _encode(s):
        return s.encode()
else: # pragma: no cover
    def _is_unicode(s):
        return isinstance(s, unicode)

    def _decode_ascii(s):
        return s

    def _decode(s):
        return s.decode(getattr(sys.stdin, 'encoding', 'utf8') or 'utf8')

    def _encode(s):
        return s.encode(getattr(sys.stdin, 'encoding', 'utf8') or 'utf8')

def db_get(db_path, key):
    """ get value from db file """
    if not isinstance(key, bytes): # python3
        key = key.encode('ascii')
    with closing(bsddb.hashopen(db_path, 'c')) as db:
        value = db.get(key)
        if value is not None:
            return _decode_ascii(db.get(key))

def db_put(db_path, key, value):
    """ store key, value in db file """
    if not isinstance(key, bytes): # python3
        key = key.encode('ascii')
    with open(db_path+'.lock', 'w') as lockfile:
        fcntl.flock(lockfile.fileno(), fcntl.LOCK_EX)
        with closing(bsddb.hashopen(db_path, 'c')) as db:
            db[key] = value

class SearchWorker(kopano.Worker):
    """ process which handles search requests coming from outlook/webapp, according to our internal protocol """

    def main(self):
        config, plugin = self.service.config, self.service.plugin
        def response(conn, msg):
            self.log.info('Response: %s', msg)
            conn.sendall(_encode(msg) + _encode('\r\n'))
        s = kopano.server_socket(config['server_bind_name'], ssl_key=config['ssl_private_key_file'], ssl_cert=config['ssl_certificate_file'], log=self.log)
        while True:
            with log_exc(self.log):
                conn, _ = s.accept()
                fields_terms = []
                for data in conn.makefile():
                    data = _decode(data.strip())
                    self.log.info('Command: %s', data)
                    cmd, args = data.split()[0], data.split()[1:]
                    if cmd == 'PROPS':
                        response(conn, 'OK:'+' '.join(map(str, config['index_exclude_properties'])))
                        break
                    if cmd == 'SYNCRUN': # wait for syncing to be up-to-date (only used in tests)
                        self.syncrun.value = time.time()
                        while self.syncrun.value:
                            time.sleep(1)
                        response(conn, 'OK:')
                        break
                    elif cmd == 'SCOPE':
                        server_guid, store_guid, folder_ids = args[0], args[1], args[2:]
                        response(conn, 'OK:')
                    elif cmd == 'FIND':
                        pos = data.find(':')
                        fields = [int(x) for x in data[:pos].split()[1:]]
                        orig = data[pos+1:].lower()
                        # Limit number of terms (32) so people do not
                        # inadvertently DoS it if they paste prose.
                        terms = plugin.extract_terms(orig)[:32]
                        if fields and terms:
                            fields_terms.append((fields, terms))
                        response(conn, 'OK:')
                    elif cmd == 'SUGGEST':
                        suggestion = u''
                        if config['suggestions'] and len(fields_terms) == 1:
                            for fields, terms in fields_terms:
                                suggestion = plugin.suggest(server_guid, store_guid, terms, orig, self.log)
                                if suggestion == orig:
                                    suggestion = u''
                        response(conn, 'OK: '+suggestion)
                    elif cmd == 'QUERY':
                        t0 = time.time()
                        restrictions = []
                        if folder_ids:
                            restrictions.append('('+' OR '.join(['folderid:%s' % f for f in folder_ids])+')')
                        for fields, terms in fields_terms:
                            if fields:
                                restrictions.append('('+' OR '.join('('+' AND '.join('mapi%d:%s*' % (f, term) for term in terms)+')' for f in fields)+')')
                            else:
                                restrictions.append('('+' AND '.join('%s*' % term for term in terms)+')')
                        query = ' AND '.join(restrictions) # plugin doesn't have to use this relatively standard query format
                        docids = plugin.search(server_guid, store_guid, folder_ids, fields_terms, query, config['limit_results'], self.log)
                        response(conn, 'OK: '+' '.join(map(str, docids)))
                        self.log.info('found %d results in %.2f seconds', len(docids), time.time()-t0)
                        break
                    elif cmd == 'REINDEX':
                        self.reindex_queue.put(args[0])
                        response(conn, 'OK:')
                        self.log.info("queued store %s for reindexing", args[0])
                        break
                    else:
                        self.log.error("unknown command: %s", cmd)
                        break
                conn.close()

class IndexWorker(kopano.Worker):
    """ process which gets folders from input queue and indexes them, putting the nr of changes in output queue """

    def main(self):
        config, server, plugin = self.service.config, self.server, self.service.plugin
        state_db = os.path.join(config['index_path'], server.guid+'_state')
        while True:
            changes = 0
            with log_exc(self.log):
                (_, storeguid, folderid, reindex) = self.iqueue.get()
                store = server.store(storeguid)
                folder = kopano.Folder(store, folderid)
                path = folder.path
                if path and \
                   (folder != store.outbox) and \
                   (folder != store.junk or config['index_junk']) and \
                   (folder != store.drafts or config['index_drafts']):
                    suggestions = config['suggestions'] and folder != store.junk
                    self.log.info('syncing folder: "%s" "%s"', store.name, path)
                    importer = FolderImporter(server.guid, config, plugin, suggestions, self.log)
                    state = db_get(state_db, folder.entryid) if not reindex else None
                    if state:
                        self.log.info('found previous folder sync state: %s', state)
                    t0 = time.time()
                    new_state = folder.sync(importer, state, log=self.log)
                    if new_state != state:
                        plugin.commit(suggestions)
                        db_put(state_db, folder.entryid, new_state)
                        self.log.info('saved folder sync state: %s', new_state)
                        changes = importer.changes + importer.deletes 
                        self.log.info('syncing folder "%s" took %.2f seconds (%d changes, %d attachments)', path, time.time()-t0, changes, importer.attachments)
            self.oqueue.put(changes)

class FolderImporter:
    """ tracks changes for a given folder """

    def __init__(self, *args):
        self.serverid, self.config, self.plugin, self.suggestions, self.log = args
        self.changes = self.deletes = self.attachments = 0
        self.mapping_db = os.path.join(self.config['index_path'], self.serverid+'_mapping')
        self.excludes = set(self.config['index_exclude_properties']+[0x1000, 0x1009, 0x1013, 0x678C]) # PR_BODY, PR_RTF_COMPRESSED, PR_HTML, PR_EC_IMAP_EMAIL
        self.term_cache_size = 0

    def update(self, item, flags):
        """ called for a new or changed item; get mapi properties, attachments and pass to indexing plugin """

        with log_exc(self.log):
            self.changes += 1
            storeid, folderid, entryid, sourcekey, docid = item.storeid, item.folder.hierarchyid, item.entryid, item.sourcekey, item.docid
            self.log.debug('store %s, folder %d: new/updated document with entryid %s, sourcekey %s, docid %d', storeid, folderid, entryid, sourcekey, docid)

            doc = collections.defaultdict(unicode)
            doc.update({'serverid': self.serverid, 'storeid': storeid, 'folderid': folderid, 'docid': docid, 'sourcekey': item.sourcekey})
            attach_text = []

            for subitem in [item] + list(item.items()):
                for prop in subitem.props():
                    if prop.id_ not in self.excludes:
                        if _is_unicode(prop.value):
                            if prop.value:
                                doc['mapi%d' % prop.id_] += u' ' + prop.value
                        elif isinstance(prop.value, list):
                            doc['mapi%d' % prop.id_] += u' ' + u' '.join(x for x in prop.value if _is_unicode(x))

                for attachment in subitem.attachments():
                    if self.config['index_attachments'] and \
                       0 < len(attachment) < self.config['index_attachment_max_size'] and \
                       attachment.filename != 'inline.txt': # XXX inline attachment check
                        self.log.debug('indexing attachment (filename=%s, size=%d, mimetag=%s)',
                            attachment.filename, len(attachment), attachment.mimetype)
                        self.attachments += 1
                        attach_text.append(plaintext.get(attachment, mimetype=attachment.mimetype, log=self.log))
                    attach_text.append(attachment.filename or u'')

                doc['mapi4096'] += u' ' + subitem.text + u' ' + u' '.join(attach_text) # PR_BODY
                doc['mapi3098'] += u' ' + u' '.join([subitem.sender.name, subitem.sender.email, subitem.from_.name, subitem.from_.email]) # PR_SENDER_NAME
                doc['mapi3588'] += u' ' + u' '.join([a.name + u' ' + a.email for a in subitem.to]) # PR_DISPLAY_TO
                doc['mapi3587'] += u' ' + u' '.join([a.name + u' ' + a.email for a in subitem.cc]) # PR_DISPLAY_CC
                doc['mapi3586'] += u' ' + u' '.join([a.name + u' ' + a.email for a in subitem.bcc]) # PR_DISPLAY_BCC

            doc['data'] = 'subject: %s\n' % item.subject
            db_put(self.mapping_db, item.sourcekey, '%s %s' % (storeid, item.folder.entryid)) # ICS doesn't remember which store a change belongs to..
            self.plugin.update(doc)
            self.term_cache_size += sum(len(v) for k, v in doc.items() if k.startswith('mapi'))
            if (8*self.term_cache_size) > self.config['term_cache_size']:
                self.plugin.commit(self.suggestions)
                self.term_cache_size = 0

    def delete(self, item, flags):
        """ for a deleted item, determine store and ask indexing plugin to delete """

        with log_exc(self.log):
            self.deletes += 1
            ids = db_get(self.mapping_db, item.sourcekey)
            if ids: # when a 'new' item is deleted right away (spooler?), the 'update' function may not have been called
                storeid, folderid = ids.split()
                doc = {'serverid': self.serverid, 'storeid': storeid, 'sourcekey': item.sourcekey}
                self.log.debug('store %s: deleted document with sourcekey %s', doc['storeid'], item.sourcekey)
                self.plugin.delete(doc)

class ServerImporter:
    """ tracks changes for a server node; queues encountered folders for updating """ # XXX improve ICS to track changed folders?

    def __init__(self, serverid, config, iqueue, log):
        self.mapping_db = os.path.join(config['index_path'], serverid+'_mapping')
        self.iqueue = iqueue
        self.queued = set() # sync each folder at most once

    def update(self, item, flags):
        self.queue((0, item.storeid, item.folder.entryid))

    def delete(self, item, flags):
        ids = db_get(self.mapping_db, item.sourcekey)
        if ids:
            self.queue((0,) + tuple(ids.split()))

    def queue(self, folder):
        if folder not in self.queued:
            self.iqueue.put(folder+(False,))
            self.queued.add(folder)

class Service(kopano.Service):
    """ main search process """

    def main(self):
        """ start initial syncing if no state found. then start query process and switch to incremental syncing """

        self.reindex_queue = Queue()
        index_path = self.config['index_path']
        os.umask(0o77)
        if not os.path.exists(index_path):
            try:
                os.makedirs(index_path)
            except PermissionError:
                self.log.error("Unable to create directory '%s': permission denied", index_path)
                sys.exit(1)
        self.state_db = os.path.join(index_path, self.server.guid+'_state')
        self.plugin = __import__('plugin_%s' % self.config['search_engine']).Plugin(index_path, self.log)
        self.iqueue, self.oqueue = Queue(), Queue()
        self.index_processes = self.config['index_processes']
        workers = [IndexWorker(self, 'index%d'%i, nr=i, iqueue=self.iqueue, oqueue=self.oqueue) for i in range(self.index_processes)]
        for worker in workers:
            worker.start()
        try:
            self.state = db_get(self.state_db, 'SERVER')
        except bsddb.db.DBAccessError:
            self.log.error("Cannot access '%s': permission denied", self.state_db)
            sys.exit(1)

        if self.state:
            self.log.info('found previous server sync state: %s', self.state)
        else:
            self.log.info('starting initial sync')
            new_state = self.server.state # syncing will reach at least the current state
            self.initial_sync(self.server.stores())
            self.state = new_state
            db_put(self.state_db, 'SERVER', self.state)
            self.log.info('saved server sync state = %s', self.state)
        self.syncrun = Value('d', 0)
        SearchWorker(self, 'query', reindex_queue=self.reindex_queue, syncrun=self.syncrun).start()
        self.log.info('starting incremental sync')
        self.incremental_sync()

    def initial_sync(self, stores, reindex=False):
        """ queue all folders for given stores """

        folders = [(f.count, s.guid, f.entryid, reindex) for s in stores for f in s.folders()]
        for f in sorted(folders, reverse=True):
            self.iqueue.put(f)
        itemcount = sum(f[0] for f in folders)
        self.log.info('queued %d folders (~%d changes) for parallel indexing (%s processes)', len(folders), itemcount, self.index_processes)
        t0 = time.time()
        changes = sum([self.oqueue.get() for i in range(len(folders))]) # blocking
        self.log.info('queue processed in %.2f seconds (%d changes, ~%.2f/sec)', time.time()-t0, changes, changes/(time.time()-t0))

    def incremental_sync(self):
        """ process changes in real-time (not yet parallelized); if no pending changes handle reindex requests """

        while True:
            with log_exc(self.log):
                try:
                    storeid = self.reindex_queue.get(block=False)
                    store = self.server.store(storeid)
                    self.log.info('handling reindex request for "%s"', store.name)
                    self.plugin.reindex(self.server.guid, store.guid)
                    self.initial_sync([store], reindex=True)
                except Empty:
                    pass
                importer = ServerImporter(self.server.guid, self.config, self.iqueue, self.log)
                t0 = time.time()
                new_state = self.server.sync(importer, self.state, log=self.log)
                if new_state != self.state:
                    changes = sum([self.oqueue.get() for i in range(len(importer.queued))]) # blocking
                    for f in importer.queued:
                        self.iqueue.put(f+(False,)) # make sure folders are at least synced to new_state
                    changes += sum([self.oqueue.get() for i in range(len(importer.queued))]) # blocking
                    self.log.info('queue processed in %.2f seconds (%d changes, ~%.2f/sec)', time.time()-t0, changes, changes/(time.time()-t0))
                    self.state = new_state
                    db_put(self.state_db, 'SERVER', self.state)
                    self.log.info('saved server sync state = %s', self.state)
                if t0 > self.syncrun.value+1:
                    self.syncrun.value = 0
            time.sleep(5)

    def reindex(self):
        """ pass usernames/store-ids given on command-line to running search process """

        store_guids = []

        if not (self.options.users or self.options.stores):
            print("no user/store specified (use -u/-S)", file=sys.stderr)
            sys.exit(1)

        for username in self.options.users:
            user = self.server.get_user(username)
            if not user:
                print("no such user: %s" % username, file=sys.stderr)
                sys.exit(1)
            elif user.store:
                store_guids.append(user.store.guid)

        for store_guid in self.options.stores:
            store = self.server.get_store(store_guid)
            if not store:
                print("no such store: %s" % store_guid, file=sys.stderr)
                sys.exit(1)
            else:
                store_guids.append(store.guid)

        for store_guid in store_guids:
            with closing(kopano.client_socket(self.config['server_bind_name'], ssl_cert=self.config['ssl_certificate_file'])) as s:
                s.sendall((u'REINDEX %s\r\n' % store_guid).encode('ascii'))
                s.recv(1024)

def main():
    parser = kopano.parser('ckpsFlVuS') # select common cmd-line options
    parser.add_option('-r', '--reindex', dest='reindex', action='store_true',help='Reindex user/store')
    options, args = parser.parse_args()
    service = Service('search', config=CONFIG, options=options)
    if options.reindex:
        service.reindex()
    else:
        service.start()

if __name__ == '__main__':
    main() # pragma: no cover
