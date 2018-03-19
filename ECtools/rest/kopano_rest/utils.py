import codecs
from contextlib import closing
import fcntl
import sys

if sys.hexversion >= 0x03000000:
    import bsddb3 as bsddb
else:
    import bsddb

from MAPI.Util import kc_session_save, kc_session_restore, GetDefaultStore
import kopano

SESSIONDATA = {}

def _auth(req, options):
    auth_header = req.get_header('Authorization')

    if (auth_header and auth_header.startswith('Bearer ') and \
        (not options or options.auth_bearer)):
        token = codecs.encode(auth_header[7:], 'ascii')
        return {
            'method': 'bearer',
            'user': req.get_header('X-Kopano-UserEntryID', ''),
            'token': token,
        }

    elif (auth_header and auth_header.startswith('Basic ') and \
        (not options or options.auth_basic)):
        user, password = codecs.decode(codecs.encode(auth_header[6:], 'ascii'),
                             'base64').split(b':')
        return {
            'method': 'basic',
            'user': user,
            'password': password,
        }

    elif not options or options.auth_passthrough:
        userid = req.get_header('X-Kopano-UserEntryID')
        if userid:
            return {
                'method': 'passthrough',
                'userid': userid,
            }

def db_get(key):
    with closing(bsddb.hashopen('mapping_db', 'c')) as db:
        return codecs.decode(db.get(codecs.encode(key, 'ascii')), 'ascii')

def db_put(key, value):
    with open('mapping_db.lock', 'w') as lockfile:
        fcntl.flock(lockfile.fileno(), fcntl.LOCK_EX)
        with closing(bsddb.hashopen('mapping_db', 'c')) as db:
            db[codecs.encode(key, 'ascii')] = codecs.encode(value, 'ascii')

def _server(req, options):
    global SERVER
    auth = _auth(req, options)

    if auth['method'] == 'bearer':
        return kopano.Server(auth_user=auth['user'], auth_pass=auth['token'],
            parse_args=False, oidc=True)

    elif auth['method'] == 'basic':
        return kopano.Server(auth_user=auth['user'], auth_pass=auth['password'],
            parse_args=False)

    elif auth['method'] == 'passthrough':
        userid = auth['userid']
        if userid in SESSIONDATA:
            sessiondata = SESSIONDATA[userid]
            mapisession = kc_session_restore(sessiondata)
            server = kopano.Server(mapisession=mapisession, parse_args=False)
        else:
            try:
                SERVER
            except NameError:
                SERVER = kopano.Server(parse_args=False, store_cache=False)
            username = SERVER.user(userid=userid).name
            server = kopano.Server(auth_user=username, auth_pass='',
                                   parse_args=False, store_cache=False)
            sessiondata = kc_session_save(server.mapisession)
            SESSIONDATA[userid] = sessiondata
        return server

def _store(server, userid):
    if userid:
        return server.user(userid=userid).store
    else:
        return kopano.Store(server=server,
                            mapiobj=GetDefaultStore(server.mapisession))

def _server_store(req, userid, options):
    server = _server(req, options)
    store = _store(server, userid)
    return server, store

def _folder(store, folderid):
    name = folderid.lower()
    if name == 'inbox':
        return store.inbox
    elif name == 'drafts':
        return store.drafts
    elif name == 'calendar':
        return store.calendar
    elif name == 'contacts':
        return store.contacts
    elif name == 'deleteditems':
        return store.wastebasket
    elif name == 'junkemail':
        return store.junk
    elif name == 'sentitems':
        return store.sentmail
    else:
        return store.folder(entryid=folderid)
