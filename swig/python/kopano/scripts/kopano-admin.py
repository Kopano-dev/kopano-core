#!/usr/bin/env python

# playing around with a python-kopano based version of kopano-admin (far from complete, patches/ideas welcome!)

# usage: ./kopano-admin.py -h

from MAPI.Util import PR_EC_STATSTABLE_SYSTEM, PR_DISPLAY_NAME, PR_EC_STATS_SYSTEM_VALUE # XXX

import kopano

def parser_opt_args():
    parser = kopano.parser('skpc')
    parser.add_option('--create-public-store', dest='public_store', action='store_true',  help='Create public store')
    parser.add_option('--sync', dest='sync', action='store_true', help='Synchronize users and groups with external source')
    parser.add_option('--list-users', dest='list_users', action='store_true', help='List users')
    parser.add_option('--list-companies', dest='list_companies', action='store_true', help='List companies')
    parser.add_option('--list-stores', dest='list_stores', action='store_true', help='List stores')
    parser.add_option('--user-details', dest='user_details', action='store', help='Show user details', metavar='NAME')
    parser.add_option('--user-count', dest='usercount', action='store_true', help='Output the system users counts')
    # DB Plugin
    parser.add_option('--create-group', dest='create_group', action='store', help='Create group, -e options optional', metavar='NAME')
    parser.add_option('--delete-group', dest='delete_group', action='store', help='Delete group', metavar='NAME')
    return (parser,) + parser.parse_args()

def main():
    parser, options, args = parser_opt_args()
    server = kopano.Server(options)

    if options.sync:
        server.sync_users()

    elif options.public_store:
        if not server.public_store:
            server.create_store(public=True)
        else:
            print('public store already exists')

    elif options.list_users:
        fmt = '{:>16}{:>20}{:>20}{:>40}'
        print(fmt.format('User', 'Full Name', 'Homeserver', 'Store'))
        print(96*'-')
        for user in server.users(system=True):
            print(fmt.format(user.name, user.fullname, user.home_server, user.store.guid))

    elif options.list_companies:
        fmt = '{:>16}'
        print(fmt.format('Company'))
        print(16*'-')
        for company in server.companies():
            print(fmt.format(company.name))

    elif options.list_stores:
        fmt = '{:>40}'+'{:>16}'*3
        print(fmt.format('Store', 'User', 'Public', 'Orphan'))
        print(64*'-')
        for store in server.stores():
            print(fmt.format(store.guid, store.user.name if store.user else '', store.public, store.orphan))

    elif options.user_details:
        user = server.user(options.user_details)
        print('Username:\t' + user.name)
        print('Fullname:\t' + user.fullname)
        print('Emailaddress:\t' + user.email)
        print('Active:\t\t' + ('yes' if user.active else 'no'))
        print('Administrator:\t' + ('yes' if user.admin else 'no'))
        print('Address Book:\t' + ('Hidden' if user.hidden else 'Visible'))
        print('Auto-accept meeting req:') # XXX AutoAccept class?
        print('Out Of Office:\t' + ('Enabled' if user.outofoffice.enabled else 'Disabled')) # XXX show settings
        print('Features:\t' + '; '.join(user.features))

        print('Store:\t\t' + user.store.guid)
        print 'Current user store quota settings:'
        print ' Quota overrides:\t' + ('no' if user.quota.use_default else 'yes')
        print ' Warning level:\t\t' + str(user.quota.warning_limit or 'unlimited')
        print ' Soft level:\t\t' + str(user.quota.soft_limit or 'unlimited')
        print ' Hard level:\t\t' + str(user.quota.hard_limit or 'unlimited')
        print 'Current store size:\t%.2f MB' % (user.store.size / 2**20)

        groups = list(user.groups())
        print 'Groups (%d):' % len(groups)
        for group in user.groups():
            print '\t' + group.name

    elif options.usercount:
        stats = server.table(PR_EC_STATSTABLE_SYSTEM).dict_(PR_DISPLAY_NAME, PR_EC_STATS_SYSTEM_VALUE)
        print(stats)
        print('Active:\t\t', int(stats['usercnt_active']))
        print('Non-active:\t', int(stats['usercnt_nonactive']))
        print('  Users:\t', int(stats['usercnt_na_user']))
        print('  Rooms:\t', int(stats['usercnt_room']))
        print('  Equipment:\t', int(stats['usercnt_equipment']))

    elif options.create_group:
        server.create_group(options.create_group)

    elif options.delete_group:
        server.remove_group(options.delete_group)

    else:
        parser.print_help()

if __name__ == '__main__':
    main()
