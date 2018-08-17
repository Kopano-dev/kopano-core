#!/usr/bin/python
# SPDX-License-Identifier: AGPL-3.0-only

# delete items which contain character string in subject

# usage: ./delete_pattern.py -u username spam

import kopano

options, args = kopano.parser('cskpUPufm').parse_args()
assert args, 'please specify search pattern'

server = kopano.Server()

for user in server.users(): # checks -u/--user command-line option
    print("Running for user:", user.name)
    for folder in user.store.folders(): # checks -f/--folder command-line option
        print("Folder:", folder.name)
        for item in folder:
            if args[0].lower() in item.subject.lower():
                print("Deleting:", item)
                if options.modify: # checks -m/--modify command-line option
                    folder.delete(item)
