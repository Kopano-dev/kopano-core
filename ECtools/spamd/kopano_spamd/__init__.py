#!/usr/bin/python
from .version import __version__

import sys
import os
import time
import kopano
import grp
from MAPI.Defs import bin2hex
from MAPI.Tags import PR_SEARCH_KEY
from kopano import Config, log_exc
from contextlib import closing

if sys.hexversion >= 0x03000000:
    import bsddb3 as bsddb
else:
    import bsddb

"""
kopano-spamd - ICS driven spam learning daemon for Kopano / SpamAssasin
"""

CONFIG = {
    'spam_dir': Config.string(default="/var/lib/kopano/spamd/spam"),
    'ham_dir': Config.string(default="/var/lib/kopano/spamd/ham"),
    'spam_db': Config.string(default="/var/lib/kopano/spamd/spam.db"),
    'sa_group': Config.string(default="amavis")
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
                    self.log.info('Error: [%s]' % e)
                time.sleep(1)


class Checker(object):
    def __init__(self, service):
        self.log = service.log
        self.spamdb = service.config['spam_db']
        self.spamdir = service.config['spam_dir']
        self.hamdir = service.config['ham_dir']
        self.sagroup = service.config['sa_group']

    def mark_spam(self, search_key):
        with closing(bsddb.btopen(self.spamdb, 'c')) as db:
            db[search_key] = ''

    def was_spam(self, search_key):
        with closing(bsddb.btopen(self.spamdb, 'c')) as db:
            return search_key in db

    def update(self, item, flags):
        if item.message_class != 'IPM.Note':
            return

        search_key = bin2hex(item.prop(PR_SEARCH_KEY).value)

        if item.folder == item.store.junk and \
           item.header('x-spam-flag') != 'YES':

            fn = os.path.join(self.hamdir, search_key)
            if os.path.isfile(fn):
                os.unlink(fn)

            self.learn(item, True)

        elif item.folder != item.store.junk and \
                self.was_spam(search_key):

            fn = os.path.join(self.spamdir, search_key)
            if os.path.isfile(fn):
                os.unlink(fn)

            self.learn(item, False)

    def learn(self, item, spam):
        try:
            search_key = bin2hex(item.prop(PR_SEARCH_KEY).value)
            spameml = item.eml()
            dir = spam and self.spamdir or self.hamdir
            emlfilename = os.path.join(dir, search_key)

            with closing(open(emlfilename, "wb")) as fh:
                fh.write(spameml)

            uid = os.getuid()
            gid = grp.getgrnam(self.sagroup).gr_gid
            os.chown(emlfilename, uid, gid)
            os.chmod(emlfilename, 0o660)

            if spam:
                self.mark_spam(search_key)
        except Exception as e:
            self.log.error(
                'Exception happend during learning: [%s] [%s]' %
                (e, item.entryid)
            )

def main():
    parser = kopano.parser('ckpsF')  # select common cmd-line options
    options, args = parser.parse_args()
    service = Service('spamd', config=CONFIG, options=options)
    service.start()


if __name__ == '__main__':
    main()
