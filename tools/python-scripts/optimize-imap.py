#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

import sys
import logging

from MAPI.Tags import PR_EC_IMAP_EMAIL_SIZE, PR_EC_IMAP_BODYSTRUCTURE, PR_EC_IMAP_BODY, PR_EC_IMAP_EMAIL, PT_STRING8
from MAPI.Struct import MAPIErrorNotFound, SNotRestriction, SExistRestriction
import inetmapi

from kopano import Restriction, server, parser


def logger(options):
    logging.basicConfig(stream=sys.stdout, level=options.loglevel)
    return logging.getLogger('optimize-imap')


def generate_imap_message(item):
    eml = item.eml(received_date=True)
    envelope, body, bodystructure = inetmapi.createIMAPProperties(eml)

    item.create_prop(PR_EC_IMAP_EMAIL, eml)
    item.create_prop(PR_EC_IMAP_EMAIL_SIZE, len(eml))
    item.create_prop(PR_EC_IMAP_BODYSTRUCTURE, bodystructure)
    item.create_prop(PR_EC_IMAP_BODY, body)
    item.create_prop('imap:1', envelope, proptype=PT_STRING8)
    item.mapiobj.SaveChanges(0)


def main():
    options, _ = parser('ksplu').parse_args()
    log = logger(options)
    server = server(options=options, auth_user='SYSTEM', auth_pass='', parse_args=True)
    restriction = Restriction(mapiobj=SNotRestriction(SExistRestriction(PR_EC_IMAP_EMAIL_SIZE)))

    for user in server.users():  # XXX multi-company..
        # Skip users without IMAP enabled
        if not 'imap' in user.features:
            log.info('Skipping user %s, IMAP disabled', user.name)
            continue

        log.debug('Processing user %s', user.name)
        for folder in user.store.folders():
            # Inbox folder's container class is None..
            if folder.container_class != 'IPF.Note' and folder.container_class != None:
                continue

            log.info('Processing folder %s', folder.name)
            for item in folder.items(restriction=restriction):
                log.debug('Processing item %s', item.subject)
                generate_imap_message(item)


if __name__ == "__main__":
    main()
