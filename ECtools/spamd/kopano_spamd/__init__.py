#!/usr/bin/python
from .version import __version__

import sys
import os
import time
import kopano
import grp
from kopano import Config, log_exc
from contextlib import closing

if sys.hexversion >= 0x03000000:
    import bsddb3 as bsddb
else: # pragma: no cover
    import bsddb

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
        state = server.state
        catcher = Checker(self)
        with log_exc(self.log):
            while True:
                try:
                    state = server.sync(catcher, state)
                except Exception as e:
                    self.log.info('Error: [%s]', e)
                time.sleep(1)


class Checker(object):
    def __init__(self, service):
        self.log = service.log
        self.spamdb = service.config['spam_db']
        self.spamdir = service.config['spam_dir']
        self.hamdir = service.config['ham_dir']
        self.sagroup = service.config['sa_group']
        self.learnham = service.config['learn_ham']
        self.headertag = service.config['header_tag'].lower()

    def mark_spam(self, searchkey):
        with closing(bsddb.btopen(self.spamdb, 'c')) as db:
            db[searchkey] = ''

    def was_spam(self, searchkey):
        with closing(bsddb.btopen(self.spamdb, 'c')) as db:
            return searchkey in db

    def update(self, item, flags):
        if item.message_class != 'IPM.Note':
            return

        searchkey = item.searchkey

        if item.folder == item.store.junk and \
           item.header(self.headertag).upper() != 'YES':

            fn = os.path.join(self.hamdir, searchkey + '.eml')
            if os.path.isfile(fn):
                os.unlink(fn)

            self.log.info("Learning message as SPAM, entryid: %s", item.entryid)
            self.learn(item, True)

        elif item.folder == item.store.inbox and \
                self.learnham and self.was_spam(searchkey):

            fn = os.path.join(self.spamdir, searchkey + '.eml')
            if os.path.isfile(fn):
                os.unlink(fn)

            self.log.info("Learning message as HAM, entryid: %s", item.entryid)
            self.learn(item, False)

    def learn(self, item, spam):
        try:
            searchkey = item.searchkey
            spameml = item.eml()
            dir = spam and self.spamdir or self.hamdir
            emlfilename = os.path.join(dir, searchkey + '.eml')

            with closing(open(emlfilename, "wb")) as fh:
                fh.write(spameml)

        except Exception as e:
            self.log.error(
                'Exception happend during learning: %s, entryid: %s',
                e, item.entryid)
            return

        try:
            uid = os.getuid()
            gid = grp.getgrnam(self.sagroup).gr_gid
            os.chown(emlfilename, uid, gid)
            os.chmod(emlfilename, 0o660)
        except Exception as e:
            self.log.warning('Unable to set ownership: %s, entryid %s',
                             e, item.entryid)

        if spam:
            self.mark_spam(searchkey)


def main():
    parser = kopano.parser('ckpsFl')  # select common cmd-line options
    options, args = parser.parse_args()
    service = Service('spamd', config=CONFIG, options=options)
    service.start()


if __name__ == '__main__':
    main()
