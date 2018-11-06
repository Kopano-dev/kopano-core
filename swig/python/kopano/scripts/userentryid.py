#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later

from __future__ import print_function

import base64
import codecs

import kopano


server = kopano.Server()
if server.options.users:
    user = server.user(server.options.users[0])
    print("X-Kopano-UserEntryID: {}".format(base64.b64encode(codecs.decode(user.userid, 'hex'))))
else:
    print('Missing argument -u user')

