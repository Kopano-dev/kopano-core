#!/usr/bin/python

# basic looping examples (users, items, properties, folders, attachments, companies..)

# usage: ./loop.py (change USER to username)

import kopano

USER = 'user1'

server = kopano.Server()

for prop in server.admin_store.props():
    print(prop.idname if not prop.named else prop.name, hex(prop.proptag), repr(prop.value))

for company in server.companies():
	print('company:', company.name)

for user in server.users():
	print('local user:', user.name)

print(server.guid, [user.store.guid for user in server.users()])

for user in server.users():
    if user.name == USER:
        print([folder.name for folder in user.store.folders()])

for user in server.users():
    if user.name == USER:
        print(list(user.store.props()))
        for folder in user.store.folders():
            if folder.name == 'Sent Items':
                for item in folder:
                    print('item:', item.subject, list(item.props()), [(att.filename, att.mimetype, len(att.data)) for att in item.attachments()])

for user in server.users():
    for folder in user.store.folders():
        for item in folder:
            print(user, folder, item)
