# -*- indent-tabs-mode: nil -*-
# SPDX-License-Identifier: AGPL-3.0-only

import os.path
import random
import sys

# MAPI stuff
from MAPI.Defs import *
from MAPI.Tags import *
from MAPI.Struct import *

# For backward compatibility
from MAPI.Util.AddressBook import GetUserList

def is_unicode(s):
    if sys.hexversion >= 0x03000000:
        return isinstance(s, str)
    else:
        return isinstance(s, unicode)

def is_str(s):
    if sys.hexversion >= 0x03000000:
        return isinstance(s, bytes)
    else:
        return isinstance(s, str)

def to_str(s):
    if sys.hexversion >= 0x03000000:
        if isinstance(s, bytes):
            return s
        else:
            return bytes(s, 'ascii')
    else:
        return str(s)

# flags = 1 == EC_PROFILE_FLAGS_NO_NOTIFICATIONS
def OpenECSession(user, password, path, **keywords):
    profname = to_str('__pr__%d' % random.randint(0,100000))
    profadmin = MAPIAdminProfiles(0)
    profadmin.CreateProfile(profname, None, 0, 0)
    try:
        admin = profadmin.AdminServices(profname, None, 0, 0)
        if 'providers' in keywords:
            for provider in keywords['providers']:
                admin.CreateMsgService(provider, provider, 0, 0)
        else:
            admin.CreateMsgService(b"ZARAFA6", b"Zarafa", 0, 0)
        table = admin.GetMsgServiceTable(0)
        rows = table.QueryRows(1,0)
        prop = PpropFindProp(rows[0], PR_SERVICE_UID)
        uid = prop.Value
        profprops = list()
        profprops.append(SPropValue(PR_EC_PATH, to_str(path if path else "default:")))
        if is_unicode(user):
            profprops.append(SPropValue(PR_EC_USERNAME_W, user))
        else:
            assert is_str(user)
            profprops.append(SPropValue(PR_EC_USERNAME_A, user))
        if is_unicode(password):
            profprops.append(SPropValue(PR_EC_USERPASSWORD_W, password))
        else:
            assert is_str(password)
            profprops.append(SPropValue(PR_EC_USERPASSWORD_A, password))

        sslkey_file = keywords.get('sslkey_file')
        if sslkey_file and os.path.isfile(sslkey_file):
            profprops.append(SPropValue(PR_EC_SSLKEY_FILE, to_str(sslkey_file)))
            sslkey_pass = keywords.get('sslkey_pass')
            if sslkey_pass:
                profprops.append(SPropValue(PR_EC_SSLKEY_PASS, to_str(sslkey_pass)))

        flags = EC_PROFILE_FLAGS_NO_NOTIFICATIONS
        if 'flags' in keywords:
            flags = keywords['flags']
        profprops.append(SPropValue(PR_EC_FLAGS, flags))

        impersonate = keywords.get('impersonate')
        if impersonate and is_unicode(impersonate):
            profprops.append(SPropValue(PR_EC_IMPERSONATEUSER_W, impersonate))
        elif impersonate:
            profprops.append(SPropValue(PR_EC_IMPERSONATEUSER_A, impersonate))

        profprops.append(SPropValue(PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION, to_str(sys.version)))
        profprops.append(SPropValue(PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC, to_str(sys.argv[0])))
    
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
            
def GetPublicStore(session):
    table = session.GetMsgStoresTable(0)

    table.SetColumns([PR_MDB_PROVIDER, PR_ENTRYID], 0)
    rows = table.QueryRows(25,0)

    for row in rows:
        if(row[0].ulPropTag == PR_MDB_PROVIDER and row[0].Value == ZARAFA_STORE_PUBLIC_GUID):
            return session.OpenMsgStore(0, row[1].Value, None, MDB_WRITE)
