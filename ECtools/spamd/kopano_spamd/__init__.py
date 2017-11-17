#!/usr/bin/env python
# coding=utf-8
"""
ICS driven spam learning daemon for Kopano / SpamAssasin
See included readme.md for more information.
"""
import shlex
import subprocess
import time
import kopano
from MAPI.Tags import *
from kopano import Config, log_exc

CONFIG = {
    'run_as_user': Config.string(default="kopano"),
    'run_as_group': Config.string(default="kopano"),
    'learncmd': Config.string(default="/usr/bin/sudo -u amavis /usr/bin/sa-learn --spam")
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
        self.learncmd = service.config['learncmd']

    def update(self, item, flags):
	print item
        if item.message_class == 'IPM.Note':
            if item.folder == item.store.user.junk and not item.header('x-spam-flag') == 'YES':
                self.learn(item)

    def learn(self, item):
        with log_exc(self.log):
            try:
                spameml = item.eml()
                havespam = True
            except Exception as e:
                self.log.info('Failed to extract eml of email: [%s] [%s]' % (e, item.entryid))
            if havespam:
                try:
                    p = subprocess.Popen(shlex.split(self.learncmd), stdin=subprocess.PIPE, stdout=subprocess.PIPE)
                    learning, output_err = p.communicate(spameml)
                    self.log.info('[%s] sa-learn: %s' % (item.store.user.name, learning.strip('\n')))
                except Exception as e:
                    self.log.info('sa-learn failed: [%s] [%s]' % (e, item.entryid))


def main():
    parser = kopano.parser('ckpsF')  # select common cmd-line options
    options, args = parser.parse_args()
    service = Service('spamd', config=CONFIG, options=options)
    service.start()


if __name__ == '__main__':
    main()
