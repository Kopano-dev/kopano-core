import random
import string
import time
import sys

from MAPI.Util import MAPIErrorCollision, MAPIErrorFolderCycle

import kopano

# make N continuous, random store changes:
#
# folder/message create, update, read-unread, copy, move, delete
#
# usage: ./random_changes.py Username N [SEED]
#
# (N==0 means infinite)

username = sys.argv[1]
total = int(sys.argv[2])
if len(sys.argv) > 3:
    seed = int(sys.argv[3])
    random.seed(seed)

def randname():
    return ''.join(random.choice(string.ascii_lowercase) for x in range(10))

user = kopano.user(username)
store = user.store
subtree = store.subtree
inbox = store.inbox
special_folders = [store.subtree] + [f for f in store.folders()]

folder_actions = ['create', 'update', 'copy', 'move', 'delete']
message_actions = folder_actions + ['status']

count = 0

while True:
    folders = [store.subtree] + [f for f in store.folders() if f.type_ == 'mail']

    folder = random.choice(folders)
    folder2 = random.choice(folders)

    try:
        if random.random() < 0.6:
            objtype = 'message'
            action = random.choice(message_actions)

            if folder == subtree:
                continue
        else:
            objtype = 'folder'
            action = random.choice(folder_actions)

            if folder in special_folders and action in ('update', 'move', 'delete'):
                continue

        if objtype == 'message':
            if action != 'create':
                if folder.count == 0:
                    continue
                else:
                    item = random.choice(list(folder.items()))

            if action == 'create':
                if folder == subtree:
                    continue
                item = folder.create_item(subject=randname())

            elif action == 'update':
                item.subject = randname()

            elif action == 'copy':
                if folder2 == subtree:
                    continue
                item.copy(folder2)

            elif action == 'move':
                if folder2 == subtree:
                    continue
                item.move(folder2)

            elif action == 'status':
                item.read = not item.read

            elif action == 'delete':
                if random.choice(['head', 'tail']) == 'head':
                    continue
                folder.delete(item)

            else:
                stop

        elif objtype == 'folder':
            if action == 'create':
                folder.create_folder(name=randname())

            elif action == 'update':
                folder.name = randname()

            elif action == 'copy':
                folder.parent.copy(folder, folder2)

            elif action == 'move':
                folder.parent.move(folder, folder2)

            elif action == 'delete':
                if random.choice(['head', 'tail']) == 'head':
                    continue
                store.delete(folder)

            else:
                stop

        if action in ('copy', 'move'):
            print(objtype, action, '"%s" "%s"' % (folder.path, folder2.path or '/'))
        else:
            print(objtype, action, '"%s"' % folder.path)

        count += 1
        if total > 0 and count >= total:
            break

    except (MAPIErrorCollision, MAPIErrorFolderCycle):
        pass
