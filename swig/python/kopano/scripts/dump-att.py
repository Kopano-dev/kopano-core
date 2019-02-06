#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# Dumps all attachments for a given user to files.
# This can be used to verify that attachments did not
# get corrupt during e.g. backup/restore.

# usage: ./dump-att.py -u username 

import md5
import kopano
from MAPI.Util import *

server = kopano.server(parse_args=True)

for user in server.users():
    for folder in user.store.folders():
        for item in folder:
            for att in item.attachments():
                h = md5.new(item.subject + ' ' + att.filename + ' ' + item.sourcekey).hexdigest()
                filename = h + '_' + att.filename
                print 'file %s: %d bytes' % (filename, len(att.data))
                with open(filename, 'wb') as f:
                    f.write(att.data)
