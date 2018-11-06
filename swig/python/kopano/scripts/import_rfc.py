#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# import .eml/.ics/.vcf file

# usage: ./import-rfc.py -u username [-f folderpath] filename

from __future__ import print_function
import sys
import kopano

parser = kopano.parser('skpuf')
options, args = parser.parse_args()
server = kopano.Server(options)

for user in server.users():
    if options.folders:
        folders = list(user.folders())
    else:
        folders = [user.inbox]

    for folder in folders:
        for filename in args:
            data = open(filename, 'rb').read()
            if filename.endswith('.eml'):
                folder.create_item(eml=data)
            elif filename.endswith('.ics'):
                folder.create_item(ics=data)
            elif filename.endswith('.vcf'):
                folder.create_item(vcf=data)
            else:
                print('unknown filetype:')
                sys.exit(1)
