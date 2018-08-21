#!/usr/bin/python
# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import print_function
import logging
import os
import sys
import traceback

import kopano

"""
Backup/restore user store as .eml/.ics/.vcf files

Usage:

./rfcdump.py -u username

This will create a directory with the same name as the user.

./rfcdump.py --restore path [-u targetusername]

This will restore the given directory, optionally to a different user store.

"""

def logger(options):
    logging.basicConfig(stream=sys.stdout, level=options.loglevel)
    return logging.getLogger('rfcdump')

def opt_args():
    parser = kopano.parser('skpul')
    parser.add_option('', '--restore', dest='restore', action='store_true', help='restore from backup')
    return parser.parse_args()

def backup(user, log):
    os.makedirs(user.name)
    log.info('backup user: %s', user.name)
    count = warnings = errors = 0
    for folder in user.folders():
        log.debug('backing up folder: %s', folder.name)
        for item in folder:
            try:
                log.debug('backing up item: %s', item.subject)
                if item.message_class.startswith('IPM.Note'):
                    data, ext = item.eml(), 'eml'
                elif item.message_class == 'IPM.Appointment':
                    data, ext = item.ics(), 'ics'
                elif item.message_class.startswith('IPM.Schedule.Meeting'):
                    data, ext = item.ics(), 'ics'
                elif item.message_class == 'IPM.Contact':
                    data, ext = item.vcf(), 'vcf'
                else:
                    log.warning('unsupported message class %s:', item.message_class)
                    warnings += 1
                    continue
                fpath = os.path.join(user.name, folder.path)
                if not os.path.isdir(fpath):
                    os.makedirs(fpath)
                open(os.path.join(fpath, item.sourcekey+'.'+ext), 'wb').write(data)
                count += 1
            except Exception as e:
                log.error(traceback.format_exc())
                errors += 1
    log.info('backed up %d items (%d errors, %d warnings)', count, errors, warnings)
    if errors:
        sys.exit(1)

def restore(path, user, log):
    count = warnings = errors = 0
    log.info('restoring: %s', path)
    for dirpath, _, filenames in os.walk(path):
        dirpath = dirpath[len(path)+1:]
        folder = user.folder(dirpath, create=True)
        log.info('restoring folder: %s', folder.name)
        for filename in filenames:
            try:
                data = open(os.path.join(path, dirpath, filename), 'rb').read()
                if filename.endswith('.eml'):
                    folder.create_item(eml=data)
                elif filename.endswith('.ics'):
                    folder.create_item(ics=data)
                elif filename.endswith('.vcf'):
                    folder.create_item(vcf=data)
                count += 1
            except Exception as e:
                log.error(traceback.format_exc())
                errors += 1
    log.info('restored %d items (%d errors, %d warnings)', count, errors, warnings)
    if errors:
        sys.exit(1)

def main():
    try:
        options, args = opt_args()
        if len(options.users) != 1:
            raise Exception('please specify one user')
        log = logger(options)
        server = kopano.Server(options=options)
        if options.restore:
            if len(args) != 1:
                raise Exception('please specify one directory')
            if not options.users:
                user = server.user(args[0])
            else:
                user = server.user(options.users[0])
            restore(args[0], user, log)
        else:
            user = server.user(options.users[0])
            backup(user, log)
    except Exception as e:
        print(e.message or e.strerror)
        sys.exit(1)

if __name__ == '__main__':
    main()
