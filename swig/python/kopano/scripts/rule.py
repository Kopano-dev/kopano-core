#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# client-side rule, which moves incoming mails with 'spam' in the subject to the junk folder

# usage: ./rule.py -U username -P password

import kopano
import time

class importer:
    def __init__(self, folder, target):
        self.folder = folder
        self.target = target

    def update(self, item, flags):
        if 'spam' in item.subject:
            print('trashing..', item)
            self.folder.move(item, self.target)

    def delete(self, item, flags):
        pass

server = kopano.server()

store = server.user(server.options.auth_user).store # auth_user checks -U/--auth-user command-line option
inbox, junk = store.inbox, store.junk

state = inbox.state
while True:
    state = inbox.sync(importer(inbox, junk), state)
    time.sleep(1)
