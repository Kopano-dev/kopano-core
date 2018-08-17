# SPDX-License-Identifier: AGPL-3.0-only
from contextlib import closing
import fcntl
import os.path
import shutil
import time

import xapian

"""

index and query mapi fields via python-xapian

useful reading to understand the code:

http://xapian.org/docs/omega/termprefixes.html
http://xapian.org/docs/apidoc/html/classXapian_1_1TermGenerator.html

things would be a lot simpler if we stored everything in 1 xapian database,
but in the old situation we have one database per store which is nice.


"""

class Plugin:
    def __init__(self, index_path, log):
        self.index_path = index_path
        self.log = log
        self.data = []
        self.deletes = []

    def open_db(self, server_guid, store_guid, writable=False, log=None):
        """ open xapian database; if locked, wait until unlocked """

        dbpath = os.path.join(self.index_path, '%s-%s' % (server_guid, store_guid))
        try:
            if writable:
                with open(os.path.join(dbpath+'.lock'), 'w') as lockfile: # avoid compaction using an external lock to be safe
                    fcntl.flock(lockfile.fileno(), fcntl.LOCK_EX)
                    while True:
                        try:
                            return xapian.WritableDatabase(dbpath, xapian.DB_CREATE_OR_OPEN)
                        except xapian.DatabaseLockError:
                            time.sleep(0.1)
            else:
                return xapian.Database(dbpath)
        except xapian.DatabaseOpeningError:
            if log:
                log.warn('could not open database: %s', dbpath)

    def extract_terms(self, text):
        """ extract terms as if we are indexing """
        doc = xapian.Document()
        tg = xapian.TermGenerator()
        tg.set_document(doc)
        text = text.replace('_', ' ') # xapian sees '_' as a word-character (to search for identifiers in source code)
        tg.index_text(text)
        return [t.term.decode('utf-8') for t in doc.termlist()]

    def search(self, server_guid, store_guid, folder_ids, fields_terms, query, limit_results, log):
        """ handle query; see links in the top for a description of the Xapian API """

        db = self.open_db(server_guid, store_guid, log=log)
        if not db:
            return []

        qp = xapian.QueryParser()
        qp.add_prefix("sourcekey", "XK:")
        qp.add_prefix("folderid", "XF:")
        for fields, terms in fields_terms:
            for field in fields:
                qp.add_prefix('mapi%d' % field, "XM%d:" % field)
        log.info('performing query: %s', query)
        qp.set_database(db)
        query = qp.parse_query(query, xapian.QueryParser.FLAG_BOOLEAN|xapian.QueryParser.FLAG_PHRASE|xapian.QueryParser.FLAG_WILDCARD)
        enquire = xapian.Enquire(db)
        enquire.set_query(query)
        matches = []
        for match in enquire.get_mset(0, limit_results or db.get_doccount()): # XXX catch exception if database is being updated?
            matches.append(match.document.get_value(0).decode('ascii'))
        db.close()
        return matches

    def suggest(self, server_guid, store_guid, terms, orig, log):
        """ update original search text with suggested terms """

        db = self.open_db(server_guid, store_guid, log=log)
        if not db:
            return orig

        with closing(db) as db:
            # XXX revisit later. looks like xapian cannot do this for us? :S
            for term in sorted(terms, key=lambda s: len(s), reverse=True):
                suggestion = db.get_spelling_suggestion(term).decode('utf8') or term
                orig = orig.replace(term, suggestion, 1)
            return orig

    def update(self, doc):
        """ new/changed document """

        self.data.append(doc)

    def delete(self, doc):
        """ deleted document """

        self.deletes.append(doc)

    def commit(self, suggestions):
        """ index pending documents; see links in the top for a description of the Xapian API """

        if not self.data and not self.deletes:
            return
        t0 = time.time()
        nitems = len(self.data)
        try:
            # XXX we assume here that all data is from the same store
            doc = (self.data or self.deletes)[0]
            with closing(self.open_db(doc['serverid'], doc['storeid'], writable=True, log=self.log)) as db:
                termgenerator = xapian.TermGenerator()
                termgenerator.set_database(db)
                flags = 0
                if suggestions:
                    flags |= termgenerator.FLAG_SPELLING
                termgenerator.set_flags(flags)
                for doc in self.data:
                    xdoc = xapian.Document()
                    termgenerator.set_document(xdoc)
                    for key, value in doc.items():
                        if key.startswith('mapi'):
                            value = value.replace('_', ' ') # xapian sees '_' as a word-character (to search for identifiers in source code)
                            termgenerator.index_text_without_positions(value) # add to full-text, needed for spelling dict?
                            termgenerator.index_text_without_positions(value, 1, 'XM%s:' % key[4:])
                    xdoc.add_value(0, str(doc['docid']))
                    sourcekey_term = 'XK:'+doc['sourcekey'].lower()
                    xdoc.add_term(sourcekey_term)
                    xdoc.add_term('XF:'+str(doc['folderid'])) #XXX
                    xdoc.set_data(doc['data'])
                    db.replace_document(sourcekey_term, xdoc)
                for doc in self.deletes:
                    db.delete_document('XK:'+doc['sourcekey'].lower())
            self.log.debug('commit took %.2f seconds (%d items)', time.time()-t0, nitems)
        finally:
            self.data = []
            self.deletes = []

    def reindex(self, server_guid, store_guid):
        """ remove database so we can cleanly reindex the store """

        dbpath = os.path.join(self.index_path, '%s-%s' % (server_guid, store_guid))
        self.log.info('removing %s', dbpath)
        shutil.rmtree(dbpath, ignore_errors=True) # may not exist yet (no items to index)
