#!/usr/bin/python
# -*- coding: utf-8; indent-tabs-mode: nil -*-

import os
import sys
import locale
import getopt
import MAPI
from MAPI.Util import *
from MAPI.Time import *
from MAPI.Struct import *
import inetmapi

# @todo add ignore empty recipients
sopt = inetmapi.sending_options()
sopt.no_recipients_workaround = True
sopt.add_received_date = True
dopt = inetmapi.delivery_options()
verbose = False

class emptyCompany: pass
class emptyUser: pass

def CheckUser(store, abook):
    abeid = store.GetProps([PR_MAILBOX_OWNER_ENTRYID], 0)
    user = abook.OpenEntry(abeid[0].Value, None, 0)
    enabled = user.GetProps([PR_EC_ENABLED_FEATURES], 0)
    return enabled[0].Value.index('imap')

def OpenUser(session, store, username):
    admin = store.QueryInterface(IID_IExchangeManageStore)
    userEntryID = admin.CreateStoreEntryID('', username, 0)
    return session.OpenMsgStore(0, userEntryID, None, MDB_WRITE | MDB_NO_DIALOG)

def GetEmptyCompany():
    company = emptyCompany()
    company.Companyname = 'Default'
    company.CompanyID = None
    return [company]

def GetCompanyList(store):
    service = store.QueryInterface(IID_IECServiceAdmin)
    try:
        return service.GetCompanyList(0)
    except:
        return GetEmptyCompany()

def GetUserList(store, company):
    service = store.QueryInterface(IID_IECServiceAdmin)
    return service.GetUserList(company, 0)

def ProcessMessage(session, envPropTag, message):
    rfc822 = inetmapi.IMToINet(session, None, message, sopt)
    (envelope, body, bodystructure) = inetmapi.createIMAPProperties(rfc822)
    message.SetProps([SPropValue(PR_EC_IMAP_EMAIL, rfc822), SPropValue(PR_EC_IMAP_EMAIL_SIZE, len(rfc822)),
                      SPropValue(PR_EC_IMAP_BODY, body), SPropValue(PR_EC_IMAP_BODYSTRUCTURE, bodystructure),
                      SPropValue(envPropTag, envelope)])
    message.SaveChanges(0)

def ProcessFolder(session, envPropTag, folder):
    content = folder.GetContentsTable(0)
    content.SetColumns([PR_ENTRYID, PR_SUBJECT], MAPI_DEFERRED_ERRORS)
    content.Restrict(SNotRestriction(SExistRestriction(PR_EC_IMAP_EMAIL_SIZE)), MAPI_DEFERRED_ERRORS)
    while True:
        try:
            rows = content.QueryRows(100, 0)
        except MAPIError, err:
            break
        if rows == []:
            break
        for row in rows:
            if verbose: print '      Processing \'%s\'' % row[1].Value
            try:
                message = folder.OpenEntry(row[0].Value, None, MAPI_MODIFY)
            except MAPIError, err:
                if verbose: print '        Unable to open message \'%s\', error 0x%08X' % (row[1].Value, err.hr)
                continue
            try:
                ProcessMessage(session, envPropTag, message)
            except MAPIError, err:
                if verbose: print '        Error in message: 0x%08X' % err.hr
                if err.hr == MAPI_E_STORE_FULL:
                    raise err

def ProcessStore(session, store):
    try:
        subtreeEID = store.GetProps([PR_IPM_SUBTREE_ENTRYID], 0)
        if PROP_TYPE(subtreeEID[0].ulPropTag) == PT_ERROR:
            raise Exception('not found')
    except:
        if verbose: print '    No IPM SUBTREE'
        return
    subtree = store.OpenEntry(subtreeEID[0].Value, None, 0)

    # prop 1: dispidIMAPEnvelope
    prop = store.GetIDsFromNames([MAPINAMEID(PS_EC_IMAP, MNID_ID, 1)], MAPI_CREATE)
    prop[0] = CHANGE_PROP_TYPE(prop[0], PT_STRING8)
    
    table = subtree.GetHierarchyTable(CONVENIENT_DEPTH)
    # not exists PR_CONTAINER_CLASS or PR_CONTAINER_CLASS 'IPF.Note'
    restriction = SOrRestriction([
        SNotRestriction(SExistRestriction(PR_CONTAINER_CLASS)),
        SPropertyRestriction(RELOP_EQ, PR_CONTAINER_CLASS, SPropValue(PR_CONTAINER_CLASS, 'IPF.Note'))
        ])
    table.Restrict(restriction, MAPI_DEFERRED_ERRORS)
    table.SetColumns([PR_ENTRYID, PR_DISPLAY_NAME], MAPI_DEFERRED_ERRORS)
    rows = table.QueryRows(-1, 0)
    for row in rows:
        if verbose: print '    Processing folder %s' % (row[1].Value)
        try:
            folder = store.OpenEntry(row[0].Value, None, MAPI_MODIFY)
            ProcessFolder(session, prop[0], folder)
        except MAPIError, err:
            if verbose: print '    Error in folder: 0x%08X' % err.hr
            if err.hr == MAPI_E_STORE_FULL:
                raise err

def MakeUserList(users):
    l = []
    for u in users:
        a = emptyUser()
        a.Username = u
        l.append(a)
    return l


# main()
try:
    opts, arg_users = getopt.gnu_getopt(sys.argv[1:], "v", ["verbose"])
except getopt.GetoptError, err:
    # print help information and exit:
    print str(err)
    exit(1)
for o, a in opts:
    if o in ('-v', '--verbose'):
        verbose = True

locale.setlocale(locale.LC_CTYPE, '')
session = OpenECSession("SYSTEM", "", os.getenv("KOPANO_SOCKET", "default:"))
admin = GetDefaultStore(session)
abook = session.OpenAddressBook(0, None, MAPI_UNICODE)

if arg_users:
    print "Using given user list"
    companies = GetEmptyCompany()
else:
    print "Retrieving company list"
    companies = GetCompanyList(admin)
for company in companies:
    print 'Processing ' + company.Companyname
    if arg_users:
        users = MakeUserList(arg_users)
    else:
        try:
            users = GetUserList(admin, company.CompanyID)
        except Exception, ex:
            print "Unable to list users: " + str(ex)
            exit(1)
    if not users:
        print "No users to process"
        exit(0)
    else:
        print "Processing %d users" % len(users)
    for user in users:
        if user.Username == 'SYSTEM':
            continue
        print '  Processing ' + user.Username
        try:
            ustore = OpenUser(session, admin, user.Username)
        except MAPIError, err:
            print '    Failed to open store: 0x%08X' % err.hr
            continue
        try:
            CheckUser(ustore, abook)
        except Exception:
            print '    IMAP feature disabled, skipping'
            continue
        try:
            ProcessStore(session, ustore)
        except MAPIError, err:
            if err.hr == MAPI_E_STORE_FULL:
                print '    Store Quota reached, unable to continue'
            else:
                print '    Error while processing: 0x%08X' % err.hr
            continue
