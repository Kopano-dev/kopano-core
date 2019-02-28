#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only
from __future__ import print_function
import datetime
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
    PR_MESSAGE_SIZE, # XXX why would there be a valid difference?
]

DEFAULT_DATETIME = datetime.datetime(1978, 1, 1)

def dump_folder(folder):
    print('(FOLDER)', folder.name) # XXX show folder.path

    def item_key(item):
        # extend as needed to make as much items unique as possible
        # XXX generic solution based on all common properties
        return (
            item.received or DEFAULT_DATETIME,
            item.subject,
            item.name,
            item.get('address:32896'),
            item.get(PR_CLIENT_SUBMIT_TIME) or DEFAULT_DATETIME,
            item.get(PR_COMPANY_NAME_W),
            item.get(PR_INTERNET_ARTICLE_NUMBER) or 0,
        )

    items = sorted(folder.items(), key=item_key)
    for item in items:
        dump_item(item)

def dump_item(item, depth=0):
    print('(ITEM)' if depth == 0 else '(EMBEDDED ITEM)')
    print(item.subject, item.received.isoformat(' ') if item.received else '')
    try:
        dump_props(item.props())

        recipients = sorted(item.recipients(), key=lambda x: x.name)
        for recipient in item.recipients():
            print('(RECIPIENT)')
            dump_props(recipient.props())

        attachments = item.attachments()
        attachments = sorted(item.attachments(), key=lambda x: x.filename)
        for attachment in attachments:
            print('(ATTACHMENT)')
            dump_props(attachment.props())

        for item in item.items():
            dump_item(item, depth+1)
    except:
        print('(ERROR)')
        traceback.print_exc()

def dump_props(props):
    for prop in props:
        if prop.proptag not in IGNORE:
            print(prop, prop.strval)

def main():
    parser = kopano.parser('SPQuf')
    options, args = parser.parse_args()
    server = kopano.server(options=options, parse_args=True)

    for user in kopano.users():
        if server.options.folders:
            folders = [user.folder(path) for path in server.options.folders]
        else:
            folders = [user.subtree]

        for base in folders:
            for folder in [base] + list(base.folders()):
                dump_folder(folder)

if __name__ == '__main__':
    main()
