#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# delete items which contain character string in subject

# usage: ./delete_pattern.py -u username spam

import kopano

options, args = kopano.parser('CSKQUPufm').parse_args()
assert args, 'please specify search pattern'

server = kopano.server(parse_args=True, options=options)

for user in server.users():
    print("Running for user:", user.name)
    for folder in user.store.folders():
        print("Folder:", folder.name)
        for item in folder:
            if args[0].lower() in item.subject.lower():
                print("Deleting:", item)
                if options.modify:
                    folder.delete(item)
