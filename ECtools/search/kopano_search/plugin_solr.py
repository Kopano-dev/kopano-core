# SPDX-License-Identifier: AGPL-3.0-only
import pysolr

"""
index and query mapi fields via python-pysolr

note how simple this is compared to the xapian plugin.


"""

class Plugin:
    def __init__(self, index_path, log):
        self.log = log
        self.solr = pysolr.Solr(index_path)
        self.data = []

    def extract_terms(self, text):
        return text.split()

    def search(self, server_guid, store_guid, folder_ids, fields_terms, query, log):
        log.info('performing query: %s', query)
        return [r['docid'] for r in self.solr.search(query, fl=['docid'])]

    def suggest(self, server_guid, store_guid, terms, orig, log):
        return orig

    def update(self, doc):
        self.data.append(doc)

    def delete(self, doc):
        self.commit()
        self.solr.delete(q='sourcekey:%s' % doc['sourcekey'])

    def commit(self):
        if self.data:
            try:
                self.solr.add(self.data)
            finally:
                self.data = []
                self.charcount = 0

    def reindex(self, server_guid, store_guid):
        pass
