# -*- indent-tabs-mode: nil -*-

import os.path
import random
import sys

# MAPI stuff
from MAPI.Defs import *
from MAPI.Tags import *
from MAPI.Struct import *

# For backward compatibility
from MAPI.Util.AddressBook import GetUserList

# flags = 1 == EC_PROFILE_FLAGS_NO_NOTIFICATIONS
def OpenECSession(user, password, path, **keywords):
    profname = '__pr__%d' % random.randint(0,100000)
    profadmin = MAPIAdminProfiles(0)
    profadmin.CreateProfile(profname, None, 0, 0)
    try:
        admin = profadmin.AdminServices(profname, None, 0, 0)
        if keywords.has_key('providers'):
            for provider in keywords['providers']:
                admin.CreateMsgService(provider, provider, 0, 0)
        else:
            admin.CreateMsgService("ZARAFA6", "Zarafa", 0, 0)
        table = admin.GetMsgServiceTable(0)
        rows = table.QueryRows(1,0)
        prop = PpropFindProp(rows[0], PR_SERVICE_UID)
        uid = prop.Value
        profprops = list()
        profprops.append(SPropValue(PR_EC_PATH, path if path else "default:"))
        if isinstance(user, unicode):
            profprops.append(SPropValue(PR_EC_USERNAME_W, user))
        else:
            assert isinstance(user, str)
            profprops.append(SPropValue(PR_EC_USERNAME_A, user))
        if isinstance(password, unicode):
            profprops.append(SPropValue(PR_EC_USERPASSWORD_W, password))
        else:
            assert isinstance(password, str)
            profprops.append(SPropValue(PR_EC_USERPASSWORD_A, password))

        sslkey_file = keywords.get('sslkey_file')
        if isinstance(sslkey_file, basestring) and os.path.isfile(sslkey_file):
            profprops.append(SPropValue(PR_EC_SSLKEY_FILE, str(sslkey_file)))
            sslkey_pass = keywords.get('sslkey_pass')
            if isinstance(sslkey_pass, basestring):
                profprops.append(SPropValue(PR_EC_SSLKEY_PASS, str(sslkey_pass)))

        flags = EC_PROFILE_FLAGS_NO_NOTIFICATIONS
        if keywords.has_key('flags'):
            flags = keywords['flags']
        profprops.append(SPropValue(PR_EC_FLAGS, flags))

        impersonate = keywords.get('impersonate')
        if impersonate and isinstance(impersonate, unicode):
            profprops.append(SPropValue(PR_EC_IMPERSONATEUSER_W, impersonate))
        elif impersonate:
            profprops.append(SPropValue(PR_EC_IMPERSONATEUSER_A, impersonate))

        profprops.append(SPropValue(PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION, sys.version))
        profprops.append(SPropValue(PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC, sys.argv[0]))
    
        admin.ConfigureMsgService(uid, 0, 0, profprops)
        
        session = MAPILogonEx(0,profname,None,0)
    finally:
        profadmin.DeleteProfile(profname, 0)
    return session
    
def GetDefaultStore(session):
    table = session.GetMsgStoresTable(0)
    
    table.SetColumns([PR_DEFAULT_STORE, PR_ENTRYID], 0)
    rows = table.QueryRows(25,0)
    
    for row in rows:
        if(row[0].ulPropTag == PR_DEFAULT_STORE and row[0].Value):
            return session.OpenMsgStore(0, row[1].Value, None, MDB_WRITE)
            
    return None

def GetPublicStore(session):
    table = session.GetMsgStoresTable(0)

    table.SetColumns([PR_MDB_PROVIDER, PR_ENTRYID], 0)
    rows = table.QueryRows(25,0)

    for row in rows:
        if(row[0].ulPropTag == PR_MDB_PROVIDER and row[0].Value == ZARAFA_STORE_PUBLIC_GUID):
            return session.OpenMsgStore(0, row[1].Value, None, MDB_WRITE)
