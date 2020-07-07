#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# some example of working with MAPI tables

# usage: ./tables.py (change USER to username)

import kopano

from MAPI.Tags import PR_MESSAGE_ATTACHMENTS

USER = 'user1'

server = kopano.server()

for table in server.tables():
    print(table)
    print(table.csv(delimiter=';'))

for item in server.user('user1').store.inbox:
    print(item)
    for table in item.tables():
        print(table)
        for row in table:
            print(row)
    print(item.table(PR_MESSAGE_ATTACHMENTS).text())
    print("\n")
