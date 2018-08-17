#!/usr/bin/python
# SPDX-License-Identifier: AGPL-3.0-only

# print names and email addresses of sender/recipients

# usage: ./fromto.py (change USER into username)

import kopano

USER = 'user1'

server = kopano.Server()

for item in server.user(USER).inbox:
    print(item)
    print('from:', repr(item.sender.name), repr(item.sender.email))
    for rec in item.recipients():
        print('to:', repr(rec.name), repr(rec.email))
    print("")
