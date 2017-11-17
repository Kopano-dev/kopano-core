#!/usr/bin/env python
# coding=utf-8
"""
ICS driven spam learning daemon for Kopano / SpamAssasin
See included readme.md for more information.
"""

from .version import __version__

import os
import shutil
import time
import kopano
from kopano import Config, log_exc
from contextlib import closing

CONFIG = {
    'run_as_user': Config.string(default="kopano"),
    'run_as_group': Config.string(default="kopano"),
    'spam_dir': Config.string(default="/var/lib/kopano/spamd/spam"),
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
                    if e.hr == MAPI_E_NETWORK_ERROR:
                        self.log.info('Trying to reconnect to Server in %s seconds' % 5)
                    else:
                        self.log.info('Error: [%s]' % e)
                    time.sleep(5)
                time.sleep(1)


class Checker(object):
    def __init__(self, service):
        self.log = service.log
        self.spamdir = service.config['spam_dir']
        self.sagroup = service.config['sa_group']

    def update(self, item, flags):
        if item.message_class == 'IPM.Note':
            if item.folder == item.store.user.junk and not item.header('x-spam-flag') == 'YES':
                self.learn(item)

    def learn(self, item):
        with log_exc(self.log):
            try:
                entryid = item.entryid
                spameml = item.eml()
                havespam = True
            except Exception as e:
                self.log.info('Failed to extract eml of email: [%s] [%s]' % (e, item.entryid))
            if havespam:
                try:
                    emlfilename = os.path.join(self.spamdir, entryid)
                    with closing(open(emlfilename, "w")) as fh:
                        fh.write(spameml)
                    shutil.chown(emlfilename, group=self.sagroup)
                except Exception as e:
                    self.log.info('could not write to spam dir: [%s] [%s]' % (e, item.entryid))


def main():
    parser = kopano.parser('ckpsF')  # select common cmd-line options
    options, args = parser.parse_args()
    service = Service('spamd', config=CONFIG, options=options)
    service.start()


if __name__ == '__main__':
    main()
