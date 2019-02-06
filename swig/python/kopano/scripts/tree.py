#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# print store structure, down to attachments

# usage: ./tree.py [-u username]

import kopano

server = kopano.server(parse_args=True)
for company in server.companies():
    print(company)
    for user in company.users():
         print('  ' + user.name)
         for folder in user.store.folders():
             indent = (folder.depth + 2) * '  '
             print(indent + str(folder))
             for item in folder:
                 print(indent + '  ' + str(item))
                 for attachment in item.attachments():
                     print(indent + '    ' + attachment.filename)
