#!/usr/bin/python
from .version import __version__

import os
import shutil
import time
import kopano
from kopano import Config, log_exc
from contextlib import closing

"""
kopano-spamd - ICS driven spam learning daemon for Kopano / SpamAssasin
"""

CONFIG = {
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
                    self.log.info('Error: [%s]' % e)
                time.sleep(1)


class Checker(object):
    def __init__(self, service):
        self.log = service.log
        self.spamdir = service.config['spam_dir']
        self.sagroup = service.config['sa_group']

    def update(self, item, flags):
        learn = item.message_class == 'IPM.Note' and \
            item.folder == item.store.junk and \
            item.header('x-spam-flag') != 'YES'

        if learn:
            self.learn(item)

    def learn(self, item):
        try:
            entryid = item.entryid
            spameml = item.eml()
            emlfilename = os.path.join(self.spamdir, entryid)
            with closing(open(emlfilename, "wb")) as fh:
                fh.write(spameml)
            shutil.chown(emlfilename, group=self.sagroup)
        except Exception as e:
            self.log.info(
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
