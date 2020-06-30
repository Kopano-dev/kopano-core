#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import print_function
from .version import __version__

import getpass
import locale
import sys
import traceback
from optparse import OptionGroup

import kopano
from kopano.parser import _true, _int, _name, _guid, _bool, _list_name, _date, _path

def parser_opt_args():
    parser = kopano.parser('SKQCugcfVUPs')
    parser.add_option('--debug', help='Debug mode', **_true())
    parser.add_option('--lang', help='Create folders in this language')
    parser.add_option('--create', help='Create object', **_true())
    parser.add_option('--delete', help='Delete object', **_true())

    group = OptionGroup(parser, "Listings", "")
    group.add_option('--list-users', help='List users', **_true())
    group.add_option('--list-groups', help='List groups', **_true())
    group.add_option('--list-companies', help='List companies', **_true())
    group.add_option('--list-stores', help='List stores', **_true())
    group.add_option('--list-orphans', help='List orphan stores', **_true())
    group.add_option('--user-count', help='Output user counts', **_true())
    parser.add_option_group(group)

    group = OptionGroup(parser, "User/Group attributes", "")
    group.add_option('--name', help='Name (User/Group)', **_name())
    group.add_option('--fullname', help='Full name (User)', **_name())
    group.add_option('--email', help='Email address (User/Group)', **_name())
    group.add_option('--password', help='Password (User)', **_name())
    group.add_option('--password-prompt', help='Password (prompt) (User)', **_true())
    group.add_option('--active', help='User is active (User)', **_bool())
    group.add_option('--add-feature', help='Feature to add (User)', **_list_name())
    group.add_option('--remove-feature', help='Feature to remove (User)', **_list_name())
    group.add_option('--admin', help='Administrator (User)', **_name())
    group.add_option('--admin-level', help='Admin level (User)', **_int())
    parser.add_option_group(group)

    group = OptionGroup(parser, "Store and archive", "")
    group.add_option('--create-store', help='Create store', **_true())
    group.add_option('--unhook-store', help='Unhook (public) store', **_true())
    group.add_option('--unhook-archive', help='Unhook archive store', **_true())
    group.add_option('--hook-store', help='Hook store', **_guid())
    group.add_option('--hook-archive', help='Hook archive store', **_guid())
    group.add_option('--remove-store', help='Remove orphaned store', **_guid())
    group.add_option('--add-permission', help='Permission to add (member:right1,right2..)', **_list_name())
    group.add_option('--remove-permission', help='Permission to remove', **_list_name())
    parser.add_option_group(group)

    group = OptionGroup(parser, "Group membership", "")
    group.add_option('--add-user', help='User to add', **_list_name())
    group.add_option('--remove-user', help='User to remove', **_list_name())
    parser.add_option_group(group)

    group = OptionGroup(parser, "Meeting request processing", "")
    group.add_option('--mr-accept', help='Auto-accept meeting requests', **_bool())
    group.add_option('--mr-accept-conflicts', help='Auto-accept conflicting meeting requests', **_bool())
    group.add_option('--mr-accept-recurring', help='Auto-accept recurring meeting requests', **_bool())
    group.add_option('--mr-process', help='Auto-process meeting requests', **_bool())
    parser.add_option_group(group)

    group = OptionGroup(parser, "Send-as and delegations", "")
    group.add_option('--add-sendas', help='User to add to send-as', **_list_name())
    group.add_option('--remove-sendas', help='User to remove from send-as', **_list_name())
    group.add_option('--add-delegation', help='Delegation to add (user:flag1,flag2..)', **_list_name())
    group.add_option('--remove-delegation', help='Delegation to remove', **_list_name())
    group.add_option('--send-only-to-delegates', help='Send meeting requests only to delegates', **_bool())
    parser.add_option_group(group)

    group = OptionGroup(parser, "Quota options", "")
    group.add_option('--quota-override', help='Override server quota limits', **_bool())
    group.add_option('--quota-hard', help='Hardquota limit in MB', **_int())
    group.add_option('--quota-soft', help='Softquota limit in MB', **_int())
    group.add_option('--quota-warn', help='Warnquota limit in MB', **_int())
    group.add_option('--add-companyquota-recipient', help='User to add to companyquota recipients', **_list_name())
    group.add_option('--remove-companyquota-recipient', help='User to remove from companyquota recipients', **_list_name())
    group.add_option('--add-userquota-recipient', help='User to add to userquota recipients', **_list_name())
    group.add_option('--remove-userquota-recipient', help='User to remove from userquota recipients', **_list_name())
    parser.add_option_group(group)

    group = OptionGroup(parser, "Out of office options", "")
    group.add_option('--ooo', help='Out-of-office is enabled', **_bool())
    group.add_option('--ooo-clear', help='Clear Out-of-office settings', **_true())
    group.add_option('--ooo-subject', help='Out-of-office subject', **_name())
    group.add_option('--ooo-message', help='Out-of-office message (path to file)', **_path())
    group.add_option('--ooo-from', help='Out-of-office from date', **_date())
    group.add_option('--ooo-until', help='Out-of-office until date', **_date())
    parser.add_option_group(group)

    group = OptionGroup(parser, "Misc commands", "")
    group.add_option('--sync', help='Synchronize users and groups with external source', **_true())
    group.add_option('--clear-cache', help='Clear all server caches', **_true())
    group.add_option('--purge-softdelete', help='Purge items marked as softdeleted more than N days ago', **_int())
    group.add_option('--purge-deferred', help='Purge all items in the deferred update table', **_true())
    group.add_option('--reset-folder-count', help='Reset folder counts', **_true())
    parser.add_option_group(group)

    group = OptionGroup(parser, "Remote", "")
    group.add_option('--add-admin', help='User to add as remote-admin', **_list_name())
    group.add_option('--remove-admin', help='User to remove as remote-admin', **_list_name())
    group.add_option('--add-view', help='Company to add as remote-viewer of address book', **_list_name())
    group.add_option('--remove-view', help='Company to remove as remote-viewer of address book', **_list_name())
    parser.add_option_group(group)

    return (parser,) + parser.parse_args()

ACTION_MATRIX = {
    ('list_users',): ('global', 'companies', 'groups'),
    ('list_groups',): ('global', 'companies',),
    ('create', 'delete'): ('companies', 'groups', 'users', 'stores'),
    ('sync', 'clear_cache', 'purge_softdelete', 'purge_deferred', 'remove_store'): ('global',),
    ('list_orphans', 'list_companies', 'list_stores', 'user_count'): ('global',),
}

UPDATE_MATRIX = {
    ('name',): ('companies', 'groups', 'users'),
    ('email', 'add_sendas', 'remove_sendas'): ('users', 'groups'),
    ('fullname', 'password', 'password_prompt', 'admin_level', 'active', 'reset_folder_count'): ('users',),
    ('mr_accept', 'mr_accept_conflicts', 'mr_accept_recurring', 'hook_archive', 'unhook_archive', 'mr_process'): ('users',),
    ('ooo', 'ooo_clear', 'ooo_subject', 'ooo_message', 'ooo_from', 'ooo_until'): ('users',),
    ('add_feature', 'remove_feature', 'add_delegation', 'remove_delegation'): ('users',),
    ('send_only_to_delegates',): ('users',),
    ('add_permission', 'remove_permission'): ('global', 'companies', 'users', 'stores'),
    ('add_user', 'remove_user'): ('groups',),
    ('quota_override', 'quota_hard', 'quota_soft', 'quota_warn'): ('global', 'companies', 'users'),
    ('create_store', 'unhook_store', 'hook_store'): ('global', 'companies', 'users'),
    ('add_userquota_recipient', 'remove_userquota_recipient'): ('companies',),
    ('add_companyquota_recipient', 'remove_companyquota_recipient'): ('global', 'companies',),
    ('admin', 'add_view', 'remove_view', 'add_admin', 'remove_admin'): ('companies',),
}

def orig_option(o):
    OBJ_OPT = {'users': '--user', 'groups': '--group', 'companies': '--company', 'stores': '--store'}
    return OBJ_OPT.get(o) or '--'+o.replace('_', '-')

def yesno(x):
    return 'yes' if x else 'no'

# XXX move name_flags() syntax stuff into pyko
def name_flags(s):
    if ':' in s:
        name, flags = s.split(':')
        flags = flags.split(',')
    else:
        name, flags = s, None
    return name, flags

def list_users(intro, users):
    users = list(users)
    print(intro + ' (%d):' % len(users))
    fmt = '{0:<16} {1:<20} {2:<20}'
    print(fmt.format('User', 'Full Name', 'Homeserver'))
    print(58*'-')
    for user in sorted(users, key=lambda u: u.name):
        print(fmt.format(user.name, user.fullname, user.home_server))
    print()

def list_groups(intro, groups):
    groups = list(groups)
    print(intro + ' (%d):' % len(groups))
    fmt = '{0:<16}'
    print(fmt.format('Groupname'))
    print(16*'-')
    for group in sorted(groups, key=lambda g: g.name):
        print(fmt.format(group.name))
    print()

def list_companies(intro, companies):
    companies = list(companies)
    print(intro + ' (%d):' % len(companies))
    fmt = '{0:<32} {1:<32}'
    print(fmt.format('Companyname', 'System Administrator'))
    print(65*'-')
    for company in sorted(companies, key=lambda c: c.name):
        print(fmt.format(company.name, company.admin.name if company.admin else ''))

def list_stores(server):
    fmt = '{0:<32}'
    print(fmt.format('Store guid'))
    print(32*'-')
    for store in server.stores():
        print(fmt.format(store.guid))

def list_orphans(server):
    print('Stores without users:')
    fmt = '{0:<32} {1:<20} {2:<16} {3:<16} {4:<16}'
    print(fmt.format('Store guid', 'Username', 'Last login', 'Store size', 'Store type'))
    print(104*'-')
    for store in server.stores():
        if store.orphan:
            username = store.user.name if store.user else ''
            storesize = '%.2f MB' % (float(store.size)/2**20)
            storetype = 'public' if store.public else 'private' # XXX archive
            print(fmt.format(store.guid, username, '<unknown>', storesize, storetype))
    print()
    users = [user for user in server.users() if not user.store]
    list_users('Users without stores', users)

def list_permissions(store):
    if store:
        print('Permissions:')
        for perm in store.permissions():
            print('    (store): ' + perm.member.name + ':' + ','.join(perm.rights))
        for folder in store.folders():
            for perm in folder.permissions():
                print('    ' + folder.path + ': ' + perm.member.name + ':' + ','.join(perm.rights))

def user_counts(server): # XXX allowed/available
    stats = server.stats()
    print('User counts:')
    fmt = '\t{0:>12} {1:>10}'
    print(fmt.format('', 'Used'))
    print('\t'+43*'-')
    print(fmt.format('Active', stats['usercnt_active']))
    print(fmt.format('Non-active', stats['usercnt_nonactive']))
    print(fmt.format('NA Users', stats['usercnt_na_user']))
    print(fmt.format('NA Rooms', stats['usercnt_room']))
    print(fmt.format('NA Equipment', stats['usercnt_equipment']))
    print(fmt.format('Total', int(stats['usercnt_active'])+int(stats['usercnt_nonactive'])))

def quota_str(limit): # TODO in pyko?
    if not limit:
        return 'unlimited'
    else:
        return '%d MB' % (float(limit) / 2**20)

def user_details(user):
    fmt = '{0:<30} {1:<}'
    print(fmt.format('Name:', user.name))
    print(fmt.format('Full name:', user.fullname))
    print(fmt.format('Email address:', user.email))
    print(fmt.format('Active:', yesno(user.active)))
    print(fmt.format('Administrator:', yesno(user.admin) + (' (system)' if user.admin_level == 2 else '')))
    print(fmt.format('Address Book:', ('hidden' if user.hidden else 'visible')))
    print(fmt.format('Features:', '; '.join(user.features)))

    if user.store:
        print(fmt.format('Store:', user.store.guid))
        print(fmt.format('Store size:', '%.2f MB' % (user.store.size / 2**20)))

        if user.archive_store:
            print(fmt.format('Archive store:', user.archive_store.guid))
        if user.archive_folder:
            print(fmt.format('Archive folder:', user.archive_folder.path))

        print(fmt.format('Send-as:', ', '.join(sendas.name for sendas in user.send_as())))
        print(fmt.format('Delegation:', '(send only to delegates)' if user.send_only_to_delegates else ''))
        for dlg in user.delegations():
            print('    '+dlg.user.name+':'+','.join(dlg.flags))
        print(fmt.format('Auto-accept meeting requests:', yesno(user.autoaccept.enabled)))
        if user.autoaccept.enabled:
            print(fmt.format('    Accept conflicting:', yesno(user.autoaccept.conflicts)))
            print(fmt.format('    Accept recurring:', yesno(user.autoaccept.recurring)))

        print(fmt.format('Auto-process meeting request:', yesno(user.autoprocess.enabled)))

        ooo = 'disabled'
        if user.outofoffice.enabled:
            ooo = user.outofoffice.period_desc + (' (currently %s)' % ('active' if user.outofoffice.active else 'inactive'))
        print(fmt.format('Out-Of-Office:', ooo.strip()))

    print('Current user store quota settings:')
    print(fmt.format('    Quota overrides:', yesno(not user.quota.use_default)))
    print(fmt.format('    Warning level:', quota_str(user.quota.warning_limit)))
    print(fmt.format('    Soft level:', quota_str(user.quota.soft_limit)))
    print(fmt.format('    Hard level:', quota_str(user.quota.hard_limit)))

    list_groups('Groups', user.groups())
    list_permissions(user.store)

def group_details(group):
    fmt = '{0:<16} {1:<}'
    print(fmt.format('Name:', group.name))
    print(fmt.format('Email address:', group.email))
    print(fmt.format('Address Book:', ('hidden' if group.hidden else 'visible')))
    print(fmt.format('Send-as:', ', '.join(sendas.name for sendas in group.send_as())))
    list_users('Users', group.users())

def company_details(company, server):
    fmt = '{0:<30} {1:<}'
    print(fmt.format('Name:', company.name))
    if company.admin:
        print(fmt.format('Sysadmin:', company.admin.name))
    print(fmt.format('Address Book:', ('hidden' if company.hidden else 'visible')))
    if company.public_store:
        print(fmt.format('Public store:', company.public_store.guid))
        print(fmt.format('Public store size:', '%.2f MB' % (company.public_store.size / 2**20)))
    if server.multitenant:
        print(fmt.format('Remote-admin list:', ', '.join(u.name for u in company.admins())))
        print(fmt.format('Remote-view list:', ', '.join(c.name for c in company.views())))
        user = next(company.users())
        print(fmt.format('Userquota-recipient list:', ', '.join(u.name for u in user.quota.recipients() if u.name != 'SYSTEM')))
        print(fmt.format('Companyquota-recipient list:', ', '.join(u.name for u in company.quota.recipients() if u.name != 'SYSTEM')))
    list_permissions(company.public_store)

def store_details(store, server):
    fmt = '{0:<30} {1:<}'
    print(fmt.format('Type:', store.type_))
    if store.user:
        print(fmt.format('User:', store.user.name))
    print(fmt.format('GUID:', store.guid))
    print(fmt.format('Size:', '%.2f MB' % (store.size / 2**20)))
    list_permissions(store)

def shared_options(obj, options, server):
    if options.name:
        obj.name = options.name
    if options.email:
        obj.email = options.email

    for sendas in options.add_sendas:
        obj.add_send_as(server.user(sendas))
    for sendas in options.remove_sendas:
        obj.remove_send_as(server.user(sendas))

def quota_options(user, options):
    if options.quota_override is not None:
        user.quota.use_default = not options.quota_override
    if options.quota_warn is not None:
        user.quota.warning_limit = options.quota_warn * (2**20) # TODO make pyko accept '2 GB' etc?
    if options.quota_soft is not None:
        user.quota.soft_limit = options.quota_soft * (2**20)
    if options.quota_hard is not None:
        user.quota.hard_limit = options.quota_hard * (2**20)

def delegation_options(user, options, server):
    for delegation in options.add_delegation:
        name, flags = name_flags(delegation)
        dlg = user.delegation(server.user(name), create=True)
        if flags:
            dlg.flags += flags
    for delegation in options.remove_delegation:
        name, flags = name_flags(delegation)
        dlg = user.delegation(server.user(name))
        if flags is None:
            user.delete(dlg)
        else:
            dlg.flags = [f for f in dlg.flags if f not in flags]
    if options.send_only_to_delegates is not None:
        user.store.send_only_to_delegates = options.send_only_to_delegates # XXX user.store

def permission_options(store, options, server):
    if not store:
        return
    if options.folders:
        objs = [store.folder(f) for f in options.folders]
    else:
        objs = [store]
    for perm in options.add_permission:
        name, rights = name_flags(perm)
        member = server.get_user(name) or server.group(name) # XXX ugly error
        for obj in objs:
            perm = obj.permission(member, create=True)
            if rights:
                perm.rights += rights # XXX
    for perm in options.remove_permission:
        name, rights = name_flags(perm)
        member = server.get_user(name) or server.group(name) # XXX ugly error
        for obj in objs:
            perm = obj.permission(member)
            if rights is None:
                obj.delete(perm)
            else:
                perm.rights = [r for r in perm.rights if r not in rights] # XXX check rights

def user_options(name, options, server):
    if options.create:
        server.create_user(name, create_store=False)
    user = server.user(name)

    shared_options(user, options, server)

    if options.unhook_store:
        user.unhook()
    if options.hook_store:
        user.hook(server.store(options.hook_store))
    if options.unhook_archive:
        user.unhook_archive()
    if options.hook_archive:
        user.hook_archive(server.store(options.hook_archive))
    if options.create_store:
        user.create_store()

    if options.reset_folder_count:
        for folder in [user.root] + list(user.folders()):
            folder.recount()

    if options.mr_accept is not None:
        user.autoaccept.enabled = options.mr_accept
    if options.mr_accept_conflicts is not None:
        user.autoaccept.conflicts = options.mr_accept_conflicts
    if options.mr_accept_recurring is not None:
        user.autoaccept.recurring = options.mr_accept_recurring

    if options.mr_process is not None:
        user.autoprocess.enabled = options.mr_process

    for feature in options.add_feature:
        user.add_feature(feature)
    for feature in options.remove_feature:
        user.remove_feature(feature)

    quota_options(user, options)
    delegation_options(user, options, server)
    permission_options(user.store, options, server)

    if options.ooo is not None:
        user.outofoffice.enabled = options.ooo
    if options.ooo_clear:
        user.outofoffice.subject = None
        user.outofoffice.message = None
        user.outofoffice.start = None
        user.outofoffice.end = None
    if options.ooo_subject is not None:
        user.outofoffice.subject = options.ooo_subject
    if options.ooo_message is not None:
        user.outofoffice.message = open(options.ooo_message).read()
    if options.ooo_from is not None:
        user.outofoffice.start = options.ooo_from
    if options.ooo_until is not None:
        user.outofoffice.end = options.ooo_until

    if options.fullname:
        user.fullname = options.fullname
    if options.password:
        user.password = options.password
    if options.password_prompt:
        user.password = getpass.getpass("Password for '%s': " % user.name)
    if options.admin_level is not None:
        user.admin_level = options.admin_level
    if options.active is not None:
        user.active = options.active

    if options.details:
        user_details(user)
    if options.delete:
        server.delete(user)

def group_options(name, options, server):
    if options.create:
        server.create_group(name)
    group = server.group(name)

    shared_options(group, options, server)

    if options.list_users:
        list_users('User list for %s' % group.name, group.users())
    for user in options.add_user:
        group.add_user(server.user(user))
    for user in options.remove_user:
        group.remove_user(server.user(user))

    if options.details:
        group_details(group)
    if options.delete:
        server.delete(group)

def company_update_options(company, options, server):
    if options.unhook_store:
        company.unhook_public_store()
    if options.hook_store:
        company.hook_public_store(server.store(options.hook_store))
    if options.create_store:
        company.create_public_store()

    shared_options(company, options, server)

    if options.admin:
        company.admin = server.user(options.admin)

    for user in options.add_admin:
        company.add_admin(server.user(user))
    for user in options.remove_admin:
        company.remove_admin(server.user(user))
    for view in options.add_view:
        company.add_view(server.company(view))
    for view in options.remove_view:
        company.remove_view(server.company(view))

    for user in options.add_userquota_recipient:
        company.quota.add_recipient(server.user(user))
    for user in options.remove_userquota_recipient:
        company.quota.remove_recipient(server.user(user))
    for user in options.add_companyquota_recipient:
        company.quota.add_recipient(server.user(user), company=True)
    for user in options.remove_companyquota_recipient:
        company.quota.remove_recipient(server.user(user), company=True)

    for user in company.users(): # there are only server-wide settings
        quota_options(user, options)

def company_overview_options(company, options, server):
    if options.list_users:
        list_users('User list for %s' % company.name, company.users(system=True))
    if options.list_groups:
        list_groups('Group list for %s' % company.name, company.groups())

    if options.details:
        company_details(company, server)

def company_options(name, options, server):
    if options.create:
        server.create_company(name)
    company = server.company(name)

    company_update_options(company, options, server)
    company_overview_options(company, options, server)

    if options.delete:
        server.delete(company)

def store_options(name, options, server):
    store = server.store(name)

    if options.details:
        store_details(store, server)

    permission_options(store, options, server)

    if options.delete:
        server.delete(store)

def global_options(options, server):
    if options.lang:
        locale.setlocale(locale.LC_MESSAGES, options.lang)

    if options.sync:
        server.sync_users()
    if options.clear_cache:
        server.clear_cache()
    if options.purge_softdelete is not None:
        server.purge_softdeletes(options.purge_softdelete)
    if options.purge_deferred:
        remaining = server.purge_deferred()
        print('Remaining deferred records: %d' % remaining)

    if options.remove_store:
        server.delete(server.store(options.remove_store))

    if options.list_stores:
        list_stores(server)
    if options.list_orphans:
        list_orphans(server)
    if options.list_companies:
        list_companies('Company list', server.companies())
    if options.user_count:
        user_counts(server)

    if (not server.multitenant and \
        not (options.companies or options.groups or options.users or options.stores)):
        company = server.company('Default')
        company_overview_options(company, options, server)
        company_update_options(company, options, server)

def check_options(parser, options, server):
    objtypes = [name for name in ('companies', 'groups', 'users', 'stores') if getattr(options, name)]
    if len(objtypes) > 1:
        raise Exception('cannot combine options: %s' % ', '.join(orig_option(o) for o in objtypes))

    actions = [name for name in sum(ACTION_MATRIX, ()) if getattr(options, name) is not None]
    if len(actions) > 1:
        raise Exception('cannot combine options: %s' % ', '.join(orig_option(a) for a in actions))

    options.details = False
    updates = [name for name in sum(UPDATE_MATRIX, ()) if getattr(options, name) not in (None, [])]

    if not (actions or updates or objtypes):
        raise Exception('no object or action specified')
    if not (actions or updates):
        options.details = True

    for opts, legaltypes in list(ACTION_MATRIX.items()) + list(UPDATE_MATRIX.items()):
        for opt in opts:
            if getattr(options, opt) not in (None, []):
                illegal = set(objtypes)-set(legaltypes)
                if illegal:
                    raise Exception('cannot combine options: %s' % (orig_option(opt) + ', '+orig_option(illegal.pop())))
                if not objtypes and not 'global' in legaltypes:
                    raise Exception('%s option requires %s' % (orig_option(opt), ' or '.join(orig_option(o) for o in legaltypes)))

    if server.multitenant and not (options.companies or options.groups or options.users):
        for opt in ('create_store', 'unhook_store', 'hook_store'):
            if getattr(options, opt) is not None:
                raise Exception('%s option requires --company for multitenant setup' % orig_option(opt))

def main():
    import warnings
    warnings.warn('kopano-cli is deprecated and will be removed in the future: use kopano-admin instead.', stacklevel=3, category=FutureWarning)

    try:
        parser, options, args = parser_opt_args()
        if args:
            raise Exception("extra argument '%s' specified" % args[0])

        server = kopano.server(options, parse_args=True)
        check_options(parser, options, server)

        global_options(options, server)
        for c in options.companies:
            company_options(c, options, server)
        for g in options.groups:
            group_options(g, options, server)
        for u in options.users:
            user_options(u, options, server)
        for s in options.stores:
            store_options(s, options, server)

    except (SystemError, KeyboardInterrupt): # TODO SystemError?
        print()
        sys.exit(130)
    except Exception as e:
        if 'options' in locals() and options.debug:
            print(traceback.format_exc(), file=sys.stderr)
        else:
            print(str(e), file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main() # pragma: no cover
