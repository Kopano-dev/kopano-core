#!/usr/bin/env python
from __future__ import print_function
import os
import sys

import kopano

"""
Backup/restore user store as .eml/.ics/.vcf files

Usage:

./rfcdump.py -u username

This will create a directory with the same name as the user.

./rfcdump.py --restore path [-u targetusername]

This will restore the given directory, optionally to a different user store.

"""

def opt_args():
    parser = kopano.parser('skpu')
    parser.add_option('', '--restore', dest='restore', action='store_true', help='restore from backup')
    return parser.parse_args()

def backup(user):
    try:
        os.makedirs(user.name)
    except FileExistsError:
        print("Target directory '{}' already exists".format(user.name))
        sys.exit(1)

    for folder in user.folders():
        for item in folder:
            if item.message_class == 'IPM.Note':
                data, ext = item.eml(), 'eml'
            elif item.message_class == 'IPM.Appointment':
                data, ext = item.ics(), 'ics'
            elif item.message_class.startswith('IPM.Schedule.Meeting'):
                data, ext = item.ics(), 'ics'
            elif item.message_class == 'IPM.Contact':
                data, ext = item.vcf(), 'vcf'
            else:
                print('unsupported message class:', item.message_class)
                continue

            fpath = os.path.join(user.name, folder.path)
            if not os.path.isdir(fpath):
                os.makedirs(fpath)

            open(os.path.join(fpath, item.sourcekey+'.'+ext), 'wb').write(data)

def restore(path, user):
    for dirpath, _, filenames in os.walk(path):
        dirpath = dirpath[len(path)+1:]

        folder = user.folder(dirpath, create=True)

        for filename in filenames:
            data = open(os.path.join(path, dirpath, filename), 'rb').read()
            if filename.endswith('.eml'):
                folder.create_item(eml=data)
            elif filename.endswith('.ics'):
                folder.create_item(ics=data)
            elif filename.endswith('.vcf'):
                folder.create_item(vcf=data)
            else:
                print('unsupported file type:', filename)

def main():
    options, args = opt_args()
    server = kopano.Server(options=options)

    if options.restore:
        if not options.users:
            user = server.user(args[0])
        else:
            user = server.user(options.users[0])
        restore(args[0], user)
    else:
        user = server.user(options.users[0]) # XXX
        backup(user)

if __name__ == '__main__':
    main()
