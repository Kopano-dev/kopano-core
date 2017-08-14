#!/usr/bin/env python
from __future__ import print_function
import logging
import sys
import traceback

from MAPI.Tags import *
import kopano

"""
Dump MAPI-level content for given user (store) or folder in a mostly
deterministic fashion, skipping over properties which are always
different for different items, such as entry-ids and creation times.

The goal is to be able to use 'diff' to now check for important
differences between two stores or folders, which have come about
in different ways. Example usages:

-Check that after a backup-restore cycle the resulting data is
identical to the original data.
-Check that equivalent programs result in equivalent output (such
as a Python version and a PHP version, or python2 versus python3).

Usage: ./mapidump.py [-u username] [-f folderpath]

By default, all users/folders are included.

"""

# TODO embedded items (very important for eg calendar data)

if sys.hexversion >= 0x03000000:
    def _encode(s):
        return s
else:
    def _encode(s):
        return s.encode(sys.stdout.encoding or 'utf8')

IGNORE = [
    PR_SOURCE_KEY,
    PR_PARENT_SOURCE_KEY,
    PR_CHANGE_KEY,
    PR_PREDECESSOR_CHANGE_LIST,
    PR_ENTRYID,
    PR_PARENT_ENTRYID,
    PR_CREATION_TIME,
    PR_LAST_MODIFICATION_TIME,
    PR_RECORD_KEY,
    PR_MESSAGE_DELIVERY_TIME,
    PR_STORE_RECORD_KEY,
    PR_STORE_ENTRYID,
    PR_MAPPING_SIGNATURE,
    PR_EC_SERVER_UID,
]

def dump_props(props):
    for prop in props:
        if prop.proptag not in IGNORE:
            print(prop, _encode(prop.strval))

def main():
    parser = kopano.parser('spkuf')
    options, args = parser.parse_args()
    server = kopano.Server(options=options)

    for user in kopano.users():
        if server.options.folders:
            folders = [user.folder(path) for path in server.options.folders]
        else:
            folders = [user.subtree]

        for base in folders:
            for folder in [base] + list(base.folders()):
                print('(FOLDER)', _encode(folder.name))

                items = [item for item in folder.items() if item.received is not None] # XXX

                items = sorted(items, key=lambda x: (x.received, x.subject))
                for item in items:
                    try:
                        print(item.received.strftime('%Y-%M-%D %H:%M:%S'), _encode(item.subject))
                        print('(ITEM)')
                        dump_props(item.props())

                        recipients = item.recipients()
                        recipients = sorted(recipients, key = lambda x: x.name)
                        for recipient in item.recipients():
                            print('(RECIPIENT)')
                            dump_props(recipient.props())

                        attachments = item.attachments()
                        attachments = sorted(item.attachments(), key=lambda x: x.filename)
                        for attachment in attachments:
                            print('(ATTACHMENT)')
                            dump_props(attachment.props())

                    except Exception as e:
                        traceback.print_exc()

if __name__ == '__main__':
    main()
