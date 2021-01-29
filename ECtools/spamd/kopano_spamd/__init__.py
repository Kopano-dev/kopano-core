#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
from .version import __version__

from contextlib import closing
import grp
import os
import sys
import time

import bsddb3 as bsddb

import kopano
from kopano import Config, log_exc

"""
kopano-spamd - ICS driven spam learning daemon for Kopano / SpamAssasin
"""

CONFIG = {
    'spam_dir': Config.string(default="/var/lib/kopano/spamd/spam"),
    'ham_dir': Config.string(default="/var/lib/kopano/spamd/ham"),
    'spam_db': Config.string(default="/var/lib/kopano/spamd/spam.db"),
    'sa_group': Config.string(default="amavis"),
    'run_as_user': Config.string(default="kopano"),
    'run_as_group': Config.string(default="kopano"),
    'learn_ham': Config.boolean(default=True),
    'header_tag': Config.string(default="x-spam-flag")
}


class Service(kopano.Service):
    def main(self):
        server = self.server
        state = server.state # start from current state
        importer = Importer(self)
        with log_exc(self.log):
            while True:
                state = server.sync(importer, state)
                time.sleep(1)


class Importer:
    def __init__(self, service):
        self.log = service.log
        self.spamdb = service.config['spam_db']
        self.spamdir = service.config['spam_dir']
        self.hamdir = service.config['ham_dir']
        self.sagroup = service.config['sa_group']
        self.learnham = service.config['learn_ham']
        self.headertag = service.config['header_tag'].lower()

    def mark_spam(self, searchkey):
        if not isinstance(searchkey, bytes): # python3
            searchkey = searchkey.encode('ascii')
        with closing(bsddb.btopen(self.spamdb, 'c')) as db:
            db[searchkey] = ''

    def was_spam(self, searchkey):
        if not isinstance(searchkey, bytes): # python3
            searchkey = searchkey.encode('ascii')
        with closing(bsddb.btopen(self.spamdb, 'c')) as db:
            return searchkey in db

    def update(self, item, flags):
        with log_exc(self.log):
            if item.message_class != 'IPM.Note': # TODO None?
                return

            searchkey = item.searchkey
            header = item.header(self.headertag)

            if (item.folder == item.store.junk and \
                (not header or header.upper() != 'YES')):

                fn = os.path.join(self.hamdir, searchkey + '.eml')
                if os.path.isfile(fn):
                    os.unlink(fn)

                self.log.info("Learning message as SPAM, entryid: %s", item.entryid)
                self.learn(item, searchkey, True)

            elif (item.folder == item.store.inbox and \
                  self.learnham and \
                  (self.was_spam(searchkey) or header.upper() == 'YES')):

                fn = os.path.join(self.spamdir, searchkey + '.eml')
                if os.path.isfile(fn):
                    os.unlink(fn)

                self.log.info("Learning message as HAM, entryid: %s", item.entryid)
                self.learn(item, searchkey, False)

    def learn(self, item, searchkey, spam):
        spameml = item.eml()
        dir_ = self.spamdir if spam else self.hamdir
        emlfilename = os.path.join(dir_, searchkey + '.eml')

        with closing(open(emlfilename, "wb")) as fh:
            fh.write(spameml)

        uid = os.getuid()
        gid = grp.getgrnam(self.sagroup).gr_gid
        os.chown(emlfilename, uid, gid)
        os.chmod(emlfilename, 0o660)

        if spam:
            self.mark_spam(searchkey)

def main():
    parser = kopano.parser('CKQSFl')  # select common cmd-line options
    options = parser.parse_args()[0]
    service = Service('spamd', config=CONFIG, options=options)
    if service.config['learn_ham'] == True and not os.path.exists(service.config['ham_dir']):
        os.makedirs(service.config['ham_dir'])
    service.start()


if __name__ == '__main__':
    main()
