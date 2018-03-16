import codecs

import jwt

def _auth(req, options):
    auth_header = req.get_header('Authorization')

    if (auth_header and auth_header.startswith('Bearer ') and \
        (not options or options.auth_bearer)):
        token = codecs.encode(auth_header[7:], 'ascii')
        # TODO passing user should not be necessary
        user = jwt.decode(token, verify=False)['kc.identity']['kc.i.un']
        return {
            'method': 'bearer',
            'user': user,
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
