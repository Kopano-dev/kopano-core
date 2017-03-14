#!/usr/bin/env python
import getpass
import locale
import sys
import traceback

from MAPI.Tags import PR_EC_STATSTABLE_SYSTEM, PR_DISPLAY_NAME, PR_EC_STATS_SYSTEM_VALUE, PR_EC_RESYNC_ID
import kopano
from kopano.parser import _true, _int, _name, _guid, _bool, _list_name, _date

def parser_opt_args():
    parser = kopano.parser('skpcuGCf')
    parser.add_option('--debug', help='Debug mode', **_true())
    parser.add_option('--lang', help='Create folders in this language')
    parser.add_option('--sync', help='Synchronize users and groups with external source', **_true())
    parser.add_option('--clear-cache', help='Clear all caches in the server', **_true())
    parser.add_option('--purge-softdelete', help='Purge items in marked as softdeleted that are older than N days', **_int())
    parser.add_option('--purge-deferred', help='Purge all items in the deferred update table', **_true())
    parser.add_option('--list-users', help='List users', **_true())
    parser.add_option('--list-groups', help='List groups', **_true())
    parser.add_option('--list-companies', help='List companies', **_true())
    parser.add_option('--list-orphans', help='List orphan stores', **_true())
    parser.add_option('--user-count', help='Output the system user counts', **_true())
    parser.add_option('--remove-store', help='Remove orphaned store', **_guid())
    parser.add_option('--details', help='Show details', **_true())
    parser.add_option('--create', help='Create object', **_true())
    parser.add_option('--delete', help='Delete object', **_true())
    parser.add_option('--name', help='Name', **_name())
    parser.add_option('--fullname', help='Full name', **_name())
    parser.add_option('--email', help='Email address', **_name())
    parser.add_option('--password', help='Password', **_name())
    parser.add_option('--password-prompt', help='Password (prompt)', **_true())
    parser.add_option('--create-store', help='Create store', **_true())
    parser.add_option('--unhook-store', help='Unhook store', **_true()) # XXX archive
    parser.add_option('--hook-store', help='Hook store', **_guid()) # XXX archive
    parser.add_option('--reset-folder-count', help='Reset folder counts', **_true())
    parser.add_option('--resync-offline', help='Force offline resync', **_true())
    parser.add_option('--add-companyquota-recipient', help='User to add to companyquota recipients', **_list_name())
    parser.add_option('--remove-companyquota-recipient', help='User to remove from companyquota recipients', **_list_name())
    parser.add_option('--add-userquota-recipient', help='User to add to userquota recipients', **_list_name())
    parser.add_option('--remove-userquota-recipient', help='User to remove from userquota recipients', **_list_name())
    parser.add_option('--active', help='User is active', **_bool())
    parser.add_option('--mr-accept', help='Auto-accept meeting requests', **_bool())
    parser.add_option('--mr-accept-conflicts', help='Auto-accept conflicting meeting requests', **_bool())
    parser.add_option('--mr-accept-recurring', help='Auto-accept recurring meeting requests', **_bool())
    parser.add_option('--add-feature', help='Feature to add', **_list_name())
    parser.add_option('--remove-feature', help='Feature to remove', **_list_name())
    parser.add_option('--admin', help='Administrator', **_name())
    parser.add_option('--add-admin', help='User to add as remote-admin', **_list_name())
    parser.add_option('--remove-admin', help='User to remove as remote-admin', **_list_name())
    parser.add_option('--add-view', help='Company to add as remote-viewer of address book', **_list_name())
    parser.add_option('--remove-view', help='Company to remove as remote-viewer of address book', **_list_name())
    parser.add_option('--admin-level', help='Admin level', **_int())
    parser.add_option('--add-sendas', help='User to add to send-as', **_list_name())
    parser.add_option('--remove-sendas', help='User to remove from send-as', **_list_name())
    parser.add_option('--add-delegate', help='User to add to delegation', **_list_name())
    parser.add_option('--remove-delegate', help='User to remove from delegation', **_list_name())
    parser.add_option('--add-permission', help='Permission to add (member:right1,right2..)', **_list_name())
    parser.add_option('--remove-permission', help='Permission to remove', **_list_name())
    parser.add_option('--add-user', help='User to add', **_list_name())
    parser.add_option('--remove-user', help='User to remove', **_list_name())
    parser.add_option('--quota-override', help='Override server quota limits', **_bool())
    parser.add_option('--quota-hard', help='Hardquota limit in MB', **_int())
    parser.add_option('--quota-soft', help='Softquota limit in MB', **_int())
    parser.add_option('--quota-warn', help='Warnquota limit in MB', **_int())
    parser.add_option('--ooo-active', help='Out-of-office is active', **_bool())
    parser.add_option('--ooo-clear', help='Clear Out-of-office settings', **_true())
    parser.add_option('--ooo-subject', help='Out-of-office subject', **_name())
    parser.add_option('--ooo-message', help='Out-of-office message (file)', **_name())
    parser.add_option('--ooo-from', help='Out-of-office from date', **_date())
    parser.add_option('--ooo-until', help='Out-of-office until date', **_date())

    return (parser,) + parser.parse_args()

ACTION_MATRIX = {
    ('list_users',): ('global', 'companies', 'groups'),
    ('list_groups',): ('global', 'companies',),
    ('create', 'delete'): ('companies', 'groups', 'users'),
    ('sync', 'clear_cache', 'purge_softdelete', 'purge_deferred', 'remove_store'): ('global',),
    ('list_orphans', 'list_companies', 'user_count'): ('global',),
}

UPDATE_MATRIX = {
    ('name',): ('companies', 'groups', 'users'),
    ('email', 'fullname', 'add_sendas', 'remove_sendas'): ('users', 'groups'),
    ('password', 'password_prompt', 'admin_level', 'active', 'reset_folder_count', 'resync_offline'): ('users',),
    ('mr_accept', 'mr_accept_conflicts', 'mr_accept_recurring'): ('users',),
    ('ooo_active', 'ooo_clear', 'ooo_subject', 'ooo_message', 'ooo_from', 'ooo_until'): ('users',),
    ('add_feature', 'remove_feature', 'add_delegate', 'remove_delegate', 'add_permission', 'remove_permission'): ('users',),
    ('add_user', 'remove_user'): ('groups',),
    ('quota_override', 'quota_hard', 'quota_soft', 'quota_warn'): ('global', 'companies', 'users'),
    ('create_store', 'unhook_store', 'hook_store'): ('global', 'companies', 'users'),
    ('add_userquota_recipient', 'remove_userquota_recipient'): ('companies',),
    ('add_companyquota_recipient', 'remove_companyquota_recipient'): ('global', 'companies',),
    ('admin', 'add_view', 'remove_view', 'add_admin', 'remove_admin'): ('companies',),
}

def _encode(s):
    return s.encode(sys.stdout.encoding or 'utf8')

def orig_option(o):
    OBJ_OPT = {'users': '--user', 'groups': '--group', 'companies': '--company'}
    return OBJ_OPT.get(o) or '--'+o.replace('_', '-')

def yesno(x):
    return 'yes' if x else 'no'

def list_users(intro, users):
    users = list(users)
    print(intro + ' (%d):' % len(users))
    fmt = '{:>16}{:>20}{:>40}'
    print(fmt.format('User', 'Full Name', 'Store'))
    print(76*'-')
    for user in users:
        print(fmt.format(_encode(user.name), _encode(user.fullname), user.store.guid if user.store else ''))
    print

def list_groups(intro, groups):
    groups = list(groups)
    print(intro + ' (%d):' % len(groups))
    fmt = '\t{:>16}'
    print(fmt.format('Groupname'))
    print('\t'+16*'-')
    for group in groups:
        print(fmt.format(_encode(group.name)))
    print

def list_companies(intro, companies):
    companies = list(companies)
    print(intro + ' (%d):' % len(companies))
    fmt = '\t{:>32}{:>32}'
    print(fmt.format('Companyname', 'System Administrator'))
    print('\t'+64*'-')
    for company in companies:
        print(fmt.format(_encode(company.name), _encode(company.admin.name) if company.admin else ''))

def list_orphans(server):
    print 'Stores without users:'
    fmt = '\t{:>32}{:>20}{:>16}{:>16}{:>16}'
    print(fmt.format('Store guid', 'Username', 'Last login', 'Store size', 'Store type'))
    print('\t'+100*'-')
    for store in server.stores():
        if store.orphan:
            username = store.user.name if store.user else ''
            storesize = '%.2f MB' % (float(store.size)/2**20)
            storetype = 'public' if store.public else 'private' # XXX archive
            print(fmt.format(store.guid, _encode(username), '<unknown>', storesize, storetype))
    print
    users = [user for user in server.users() if not user.store]
    list_users('Users without stores', users)

def user_counts(server): # XXX allowed/available
    stats = server.table(PR_EC_STATSTABLE_SYSTEM).dict_(PR_DISPLAY_NAME, PR_EC_STATS_SYSTEM_VALUE)
    print 'User counts:'
    fmt = '\t{:>12}{:>10}{:>10}{:>10}'
    print(fmt.format('', 'Allowed', 'Used', 'Available'))
    print('\t'+42*'-')
    print(fmt.format('Active', stats['usercnt_licensed'], stats['usercnt_active'], int(stats['usercnt_licensed'])-int(stats['usercnt_active'])))
    print(fmt.format('Non-active', '', stats['usercnt_nonactive'], ''))
    print(fmt.format('NA Users', '', stats['usercnt_na_user'], ''))
    print(fmt.format('NA Rooms', '', stats['usercnt_room'], ''))
    print(fmt.format('NA Equipment', '', stats['usercnt_equipment'], ''))
    print(fmt.format('Total', '', int(stats['usercnt_active'])+int(stats['usercnt_nonactive']), ''))

def user_details(user):
    print('Username:\t' + _encode(user.name))
    print('Fullname:\t' + _encode(user.fullname))
    print('Emailaddress:\t' + user.email)
    print('Active:\t\t' + yesno(user.active))
    print('Administrator:\t' + yesno(user.admin) + (' (system)' if user.admin_level == 2 else ''))
    print('Address Book:\t' + ('Hidden' if user.hidden else 'Visible'))
    print('Features:\t' + '; '.join(user.features))

    if user.store:
        print('Store:\t\t' + user.store.guid)
        print('Store size:\t%.2f MB' % (user.store.size / 2**20))

        print('Send-as:\t' + ', '.join(_encode(sendas.name) for sendas in user.send_as()))
        print('Delegation:\t' + ', '.join(_encode(dlg.user.name) for dlg in user.delegations()))
        print('Auto-accept meeting req:\t' + yesno(user.autoaccept.enabled))
        if user.autoaccept.enabled:
            print('Decline dbl meetingreq:\t' + yesno(not user.autoaccept.conflicts))
            print('Decline recur meet.req:\t' + yesno(not user.autoaccept.recurring))

        ooo = 'disabled'
        if user.outofoffice.enabled:
            ooo = user.outofoffice.period_desc + (' (currently %s)' % ('active' if user.outofoffice.active else 'inactive'))
        print('Out-Of-Office:\t' + ooo)

    print('Current user store quota settings:')
    print(' Quota overrides:\t' + yesno(not user.quota.use_default))
    print(' Warning level:\t\t' + str(user.quota.warning_limit or 'unlimited'))
    print(' Soft level:\t\t' + str(user.quota.soft_limit or 'unlimited'))
    print(' Hard level:\t\t' + str(user.quota.hard_limit or 'unlimited'))

    list_groups('Groups', user.groups())

    if user.store:
        print('Permissions:')
        for perm in user.permissions():
            if perm.rights: # XXX pyko remove ACE if empty
                print(_encode(' (store): ' + perm.member.name + ':' + ','.join(perm.rights)))
        for delegate in user.delegations(): # XXX merge all rights into permissions()?
            if delegate.see_private:
                print(_encode(' (store): ' + delegate.user.name + ':see_private'))
        for folder in user.folders():
            for perm in folder.permissions():
                if perm.rights:
                    print(_encode(' ' + folder.path + ': ' + perm.member.name + ':' + ','.join(perm.rights)))

def group_details(group):
    print('Groupname:\t' + _encode(group.name))
    print('Fullname:\t' + _encode(group.fullname))
    print('Emailaddress:\t' + group.email)
    print('Address Book:\t' + ('Hidden' if group.hidden else 'Visible'))
    print('Send-as:\t' + ', '.join(_encode(sendas.name) for sendas in group.send_as()))

    list_users('Users', group.users())

def company_details(company, server):
    print('Companyname:\t' + _encode(company.name))
    if company.admin:
        print('Admin:\t\t' + _encode(company.admin.name))
    print('Address Book:\t' + ('Hidden' if company.hidden else 'Visible'))
    if company.public_store:
        print('Public store:\t\t' + company.public_store.guid)
        print('Public store size:\t%.2f MB' % (company.public_store.size / 2**20))
    if server.multitenant:
        print('Remote-admin list:\t' + ', '.join(_encode(u.name) for u in company.admins()))
        print('Remote-view list:\t' + ', '.join(_encode(c.name) for c in company.views()))
        user = company.users().next()
        print('Userquota-recipient list:\t' + ', '.join(_encode(u.name) for u in user.quota.recipients() if u.name != 'SYSTEM'))
        print('Companyquota-recipient list:\t' + ', '.join(_encode(u.name) for u in company.quota.recipients() if u.name != 'SYSTEM'))

def shared_options(obj, options, server):
    if options.name:
        obj.name = options.name
    if options.fullname:
        obj.fullname = options.fullname
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
        user.quota.warning_limit = options.quota_warn
    if options.quota_soft is not None:
        user.quota.soft_limit = options.quota_soft
    if options.quota_hard is not None:
        user.quota.hard_limit = options.quota_hard

def user_options(name, options, server):
    if options.create:
        server.create_user(name)
    user = server.user(name)

    shared_options(user, options, server)

    if options.create_store:
        user.create_store()
    if options.unhook_store:
        user.unhook()
    if options.hook_store:
        user.hook(server.store(options.hook_store))

    if options.reset_folder_count:
        for folder in [user.root] + list(user.folders()):
            folder.recount()
    if options.resync_offline:
        user.root.prop(PR_EC_RESYNC_ID, create=True).value += 1

    if options.password:
        user.password = options.password
    if options.password_prompt:
        user.password = getpass.getpass("Password for '%s': " % user.name)
    if options.admin_level is not None:
        user.admin_level = options.admin_level
    if options.active is not None:
        user.active = options.active

    if options.mr_accept is not None:
        user.autoaccept.enabled = options.mr_accept
    if options.mr_accept_conflicts is not None:
        user.autoaccept.conflicts = options.mr_accept_conflicts
    if options.mr_accept_recurring is not None:
        user.autoaccept.recurring = options.mr_accept_recurring

    for feature in options.add_feature:
        user.add_feature(feature)
    for feature in options.remove_feature:
        user.remove_feature(feature)

    quota_options(user, options)

    for delegate in options.add_delegate:
        user.delegation(server.user(delegate), create=True)
    for delegate in options.remove_delegate:
        user.delete(user.delegation(server.user(delegate)))

    def member_rights(perm): # XXX groups
        username, rights = perm.split(':')
        rights = rights.split(',')
        see_private = 'see_private' in rights
        rights = [r for r in rights if r != 'see_private']
        return server.user(username), rights, see_private
    if (options.add_permission or options.remove_permission) and not user.store:
        raise Exception("user '%s' has no store" % user.name)
    if options.folders:
        objs = [user.folder(f) for f in options.folders]
    else:
        objs = [user.store]
    for perm in options.add_permission:
        user2, rights, see_private = member_rights(perm)
        for obj in objs:
            perm = obj.permission(user2, create=True)
            perm.rights += rights
        if see_private:
            user.delegation(user2).see_private = True
    for perm in options.remove_permission:
        user2, rights, see_private = member_rights(perm)
        for obj in objs:
            perm = obj.permission(user2)
            perm.rights = [r for r in perm.rights if r not in rights]
        if see_private:
            user.delegation(user2).see_private = False

    if options.ooo_active is not None:
        user.outofoffice.enabled = options.ooo_active
    if options.ooo_clear:
        user.outofoffice.subject = None
        user.outofoffice.message = None
        user.outofoffice.start = None
        user.outofoffice.end = None
    if options.ooo_subject is not None:
        user.outofoffice.subject = options.ooo_subject
    if options.ooo_message is not None:
        user.outofoffice.message = file(options.ooo_message).read()
    if options.ooo_from is not None:
        user.outofoffice.start = options.ooo_from
    if options.ooo_until is not None:
        user.outofoffice.end = options.ooo_until

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
    if options.create_store:
        company.create_public_store()
    if options.unhook_store:
        company.unhook_public_store()
    if options.hook_store:
        company.hook_public_store(server.store(options.hook_store))

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
        list_users('User list for %s' % company.name, company.users())
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

    if options.list_orphans:
        list_orphans(server)
    if options.list_companies:
        list_companies('Company list', server.companies())
    if options.user_count:
        user_counts(server)

    if not (options.companies or options.groups or options.users):
        for company in server.companies(parse=False):
            company_overview_options(company, options, server)
            print
        company_update_options(server.company('Default'), options, server)

def check_options(options, server):
    objtypes = [name for name in ('companies', 'groups', 'users') if getattr(options, name)]
    if len(objtypes) > 1:
        raise Exception('cannot combine options: %s' % ', '.join(orig_option(o) for o in objtypes))

    actions = [name for name in sum(ACTION_MATRIX, ()) if getattr(options, name) is not None]
    if len(actions) > 1:
        raise Exception('cannot combine options: %s' % ', '.join(orig_option(a) for a in actions))

    for opts, legaltypes in ACTION_MATRIX.items() + UPDATE_MATRIX.items():
        for opt in opts:
            if getattr(options, opt) not in [None, []]:
                illegal = set(objtypes)-set(legaltypes)
                if illegal:
                    raise Exception('cannot combine options: %s' % (orig_option(opt) + ', '+orig_option(illegal.pop())))
                if not objtypes and not 'global' in legaltypes:
                    raise Exception('%s option requires %s' % (orig_option(opt), ' or '.join(orig_option(o) for o in legaltypes)))

    if server.multitenant and not (options.companies or options.groups or options.users):
        for opt in ('create_store', 'unhook_store', 'hook_store'):
            if getattr(options, opt) is not None:
                raise Exception('%s option requires --company for multitenant setup' % orig_option(opt))

def main(options):
    server = kopano.Server(options)
    check_options(options, server)

    global_options(options, server)
    for c in options.companies:
        company_options(c, options, server)
    for g in options.groups:
        group_options(g, options, server)
    for u in options.users:
        user_options(u, options, server)

if __name__ == '__main__':
    try:
        parser, options, args = parser_opt_args()
        if args:
            raise Exception("extra argument '%s' specified" % args[0])
        main(options)
    except Exception as e:
        if 'options' in locals() and options.debug:
            print(traceback.format_exc(e))
        else:
            print(_encode(str(e)))
