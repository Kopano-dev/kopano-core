#!/usr/bin/python
# SPDX-License-Identifier: AGPL-3.0-only

# basic system and folder level synchronization

# usage: ./sync.py (change USER to username)

import time
import kopano

USER = 'user1'

class Importer:
    def update(self, item, flags):
        print('update', item.store.user, item.received, item.subject, item.sender.email, [r.email for r in item.recipients()])

    def delete(self, item, flags): # only item.sourcekey is available here!
        pass

server = kopano.Server()
folder = server.user(USER).store.folder('Inbox')

folder_state = folder.state
print('current folder state:', folder_state)

print('initial sync..')
syncstate = folder.sync(Importer()) # from the beginning of time
print('incremental sync..')
for x in range(5):
    newstate = folder.sync(Importer(), folder_state) # from last known state
    if newstate != folder_state:
        print('new state:', newstate)
        folder_state = newstate
    time.sleep(1)

system_state = server.state
print('current system state:', system_state)

print('incremental system sync..') # initial sync currently not possible system-wide
for x in range(5):
    system_state = server.sync(Importer(), system_state)
    time.sleep(1)
