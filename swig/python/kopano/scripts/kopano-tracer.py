#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# traces ICS events of a user and displays the changed/new MAPI properties

# usage: ./kopano-tracer.py -u username -f foldername

import difflib
import sys
import time

from contextlib import contextmanager

from MAPI.Tags import SYNC_NEW_MESSAGE
import kopano

ITEM_MAPPING = {}

def proplist(item):
    biggest = max((len(prop.strid or 'None') for prop in item))
    props = []
    for prop in item:
        offset = biggest - len(prop.strid or 'None')
        props.append('%s %s%s\n' % (prop.strid, ' ' * offset, prop.strval))
    return props

def diffitems(item, old_item=[], delete=False):
    if delete:
        oldprops = proplist(item)
        newprops = []
        new_name = ''
        old_name = item.subject
    else:
        oldprops = proplist(old_item) if old_item else []
        newprops = proplist(item)
        new_name = item.subject
        old_name = item.subject if old_item else ''

    for line in difflib.unified_diff(oldprops, newprops, tofile=new_name, fromfile=old_name):
        sys.stdout.write(line)

@contextmanager
def print_action(item, action='Update'):
    fmt = '\033[1;41m{}: subject: {} folder: {} sender: {} ({})\033[1;m'
    print(fmt.format(action, item.subject, item.folder.name,
                     item.sender.email, time.strftime('%a %b %d %H:%M:%S %Y')))
    yield
    print('\033[1;41mEnd {}\033[1;m\n'.format(action))


class Importer:
    def update(self, item, flags):
        with print_action(item):
            if not flags & SYNC_NEW_MESSAGE:
                old_item = ITEM_MAPPING[item.sourcekey]
            else:
                ITEM_MAPPING[item.sourcekey] = item
                old_item = False

            diffitems(item, old_item)

    def delete(self, item, flags): # only item.sourcekey is available here!
        rm_item = ITEM_MAPPING[item.sourcekey]
        if not rm_item:
            return

        with print_action(rm_item, 'Delete'):
            diffitems(rm_item, delete=True)
            del ITEM_MAPPING[rm_item.sourcekey]


def item_mapping(folder):
    print('Monitoring folder %s of %s for update and delete events' % (folder.name, folder.store.user.fullname))
    # Create mapping
    for item in folder.items():
        ITEM_MAPPING[item.sourcekey] = item
    print('Mapping of items and sourcekey complete')


def sync_folders(folders):
    [item_mapping(folder) for folder in folders]
    states = {folder.name: folder.state for folder in folders}
    while True:
        for folder in folders:
            folder_state = states[folder.name]
            new_state = folder.sync(Importer(), folder_state) # from last known state
            if new_state != folder_state:
                states[folder.name] = new_state
        time.sleep(1)


def main():
    options, _ = kopano.parser('Suf').parse_args()
    server = kopano.server(options=options, parse_args=True)
    if not server.options.users:
        print('No user specified')
        sys.exit(1)
    user = server.user(server.options.users[0])

    if not server.options.folders:
        folders = list(user.store.folders())
    else:
        folders = [next(user.store.folders())]

    sync_folders(folders)


if __name__ == '__main__':
    main()
