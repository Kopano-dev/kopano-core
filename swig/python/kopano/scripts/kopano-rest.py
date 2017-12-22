import dateutil.parser
import json
try:
    import urlparse
except ImportError:
    import urllib.parse as urlparse
import types

from MAPI.Util import kc_session_save, kc_session_restore, GetDefaultStore

import falcon
import kopano

TOP = 10

# TODO /me/messages does not return _all_ messages in store
# TODO pagination for non-messages
# TODO check @odata.etag, $orderby
# TODO post, put, delete
# TODO copy/move/send actions
# TODO unicode/encoding checks
# TODO use base64

def _server(req):
    userid = USERID #req.get_header('X-Kopano-UserEntryID', required=True)
    if userid in userid_sessiondata:
        sessiondata = userid_sessiondata[userid]
        mapisession = kc_session_restore(sessiondata)
        server = kopano.Server(mapisession=mapisession, parse_args=False)
    else:
        username = admin_server.user(userid=userid).name
        server = kopano.Server(auth_user=username, auth_pass='',
                               parse_args=False, store_cache=False)
        sessiondata = kc_session_save(server.mapisession)
        userid_sessiondata[userid] = sessiondata
    return server

def _store(server, userid):
    if userid:
        return server.user(userid=userid).store
    else:
        return kopano.Store(server=server,
                            mapiobj=GetDefaultStore(server.mapisession))

class Resource(object):
    def get_fields(self, obj, fields, all_fields):
        return {f: all_fields[f](obj) for f in fields}

    def json(self, obj, fields, all_fields):
        return json.dumps(self.get_fields(obj, fields, all_fields),
            indent=4, separators=(',', ': ')
        )

    def json_multi(self, req, obj, fields, all_fields, top, skip, count):
        header = b'{\n'
        header += b'    "@odata.context": "%s",\n' % req.path
        if skip+top < count:
              header += b'    "@odata.nextLink": "%s?$skip=%d",\n' % (req.path, skip+top)
        header += b'    "value": [\n'
        yield header
        first = True
        for o in obj:
            if not first:
                yield b',\n'
            first = False
            wa = self.json(o, fields, all_fields).encode('utf-8')
            yield '\n'.join(['        '+line for line in wa.splitlines()])
        yield b'\n    ]\n}'

    def respond(self, req, resp, obj, all_fields=None, count=None):
        # determine fields (default all)
        args = urlparse.parse_qs(req.query_string)
        fields = all_fields or self.fields
        if 'fields' in args:
            fields = args['fields'][0].split(',')
        else:
            fields = fields.keys()

        # jsonify result (as stream)
        resp.content_type = "application/json"
        if isinstance(obj, tuple):
            obj, top, skip, count = obj
            resp.stream = self.json_multi(req, obj, fields, all_fields or self.fields, top, skip, count)
        else:
            resp.body = self.json(obj, fields, all_fields or self.fields)

    def generator(self, req, generator, count=0):
        # determine pagination and ordering
        args = urlparse.parse_qs(req.query_string)
        top = int(args['$top'][0]) if '$top' in args else TOP
        skip = int(args['$skip'][0]) if '$skip' in args else 0
        order = tuple(args['$orderby'][0].split(',')) if '$orderby' in args else None

        return (generator(page_start=skip, page_limit=top, order=order), top, skip, count)

class UserResource(Resource):
    fields = {
        'id': lambda user: user.userid,
        'userPrincipalName': lambda user: user.name,
    }

    def on_get(self, req, resp, userid=None):
        server = _server(req)

        if req.path.split('/')[-1] == 'me':
            userid = kopano.Store(server=server,
                mapiobj = GetDefaultStore(server.mapisession)).user.userid

        if userid:
            data = server.user(userid=userid)
        else:
            data = server.users()
        self.respond(req, resp, data)

class FolderResource(Resource):
    fields = {
        'id': lambda folder: folder.entryid,
        'parentFolderId': lambda folder: folder.parent.entryid,
        'displayName': lambda folder: folder.name,
        'unreadItemCount': lambda folder: folder.unread,
        'totalItemCount': lambda folder: folder.count,
    }

    def on_get(self, req, resp, userid=None, folderid=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid:
            data = store.folder(entryid=folderid)
        else:
            data = self.generator(req, store.folders)

        self.respond(req, resp, data)

    def on_post(self, req, resp, userid=None, folderid=None):
        server = _server(req)
        store = _store(server, userid)
        folder = store.folder(entryid=folderid)

        fields = json.loads(req.stream.read())
        item = folder.create_item(**fields) # TODO conversion

        resp.status = falcon.HTTP_201
        resp.location = req.path+'/items/'+item.entryid

    def on_put(self, req, resp, userid=None, folderid=None):
        server = _server(req)
        store = _store(server, userid)
        folder = store.folder(entryid=folderid)

        data = json.loads(req.stream.read())
        if 'action' in data:
            action = data['action']
            items = [store.item(entryid=entryid) for entryid in data['items']]

            if action == 'send':
                for item in items:
                    item.send()
            elif action == 'delete':
                folder.delete(items)
            elif action == 'copy':
                target = store.folder(entryid=data['target'])
                folder.copy(items, target)
            elif action == 'move':
                target = store.folder(entryid=data['target'])
                folder.move(items, target)

class CalendarResource(Resource): # TODO merge with FolderResource?
    fields = {
        'id': lambda folder: folder.entryid,
        'displayName': lambda folder: folder.name,
    }

    def on_get(self, req, resp, userid=None, folderid=None):
        server = _server(req)
        store = _store(server, userid)

        path = req.path
        method = None
        fields = None

        if path.split('/')[-1] == 'calendarView':
            method = 'calendarView'
            path = '/'.join(path.split('/')[:-1])

        if path.split('/')[-1] == 'calendars':
            data = self.generator(req, store.calendars)
        else:
            if folderid:
                folder = store.folder(entryid=folderid)
            else:
                folder = store.calendar

            if method == 'calendarView':
                args = urlparse.parse_qs(req.query_string)
                start = dateutil.parser.parse(args['startDateTime'][0])
                end = dateutil.parser.parse(args['endDateTime'][0])
                data = (folder.occurrences(start, end), TOP, 0, 0)
                fields = EventResource.fields
            else:
                data = folder

        self.respond(req, resp, data, fields)

class MessageResource(Resource):
    fields = {
        'id': lambda item: item.entryid,
        'createdDateTime': lambda item: item.created.isoformat(),
        'categories': lambda item: item.categories,
        'changeKey': lambda item: item.changekey,
        'subject': lambda item: item.subject,
        'body': lambda item: {'contentType': 'html', 'content': item.html},
        'from': lambda item: {'emailAddress': {'name': item.sender.name, 'address': item.sender.email} },
        'toRecipients': lambda item: [{'emailAddress': {'name': to.name, 'address': to.email}} for to in item.to],
        'lastModifiedDateTime': lambda item: item.last_modified.isoformat(),
        'sentDateTime': lambda item: item.sent.isoformat(),
        'receivedDateTime': lambda item: item.received.isoformat(),
        'hasAttachments': lambda item: item.has_attachments,
    }

    def on_get(self, req, resp, userid=None, folderid=None, messageid=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid:
            folder = store.folder(entryid=folderid)
        else:
            folder = store.inbox # TODO messages from all folders?

        if messageid:
            data = folder.item(messageid)
        else:
            data = self.generator(req, folder.items, folder.count)

        self.respond(req, resp, data)

def recurrence_json(item):
    if isinstance(item, kopano.Item) and item.recurring:
        recurrence = item.recurrence
        return {
            'pattern': {
                'type': recurrence.pattern,
                'interval': recurrence.period,
                # TODO patterntype_specific
            },
            'range': {
                'startDate': recurrence._start.isoformat(), # TODO .start?
                'endDate': recurrence._end.isoformat(), # TODO .start?
                # TODO timezone
            },
        }

class EventResource(Resource):
    fields = {
        'id': lambda item: item.entryid,
        'subject': lambda item: item.subject,
        'recurrence': recurrence_json,
        'start': lambda item: {'dateTime': item.start.isoformat(), 'timeZone': 'UTC'},
        'end': lambda item: {'dateTime': item.end.isoformat(), 'timeZone': 'UTC'},
    }

    def on_get(self, req, resp, userid=None, folderid=None, messageid=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid:
            folder = store.folder(entryid=folderid)
        else:
            folder = store.calendar

        if messageid:
            data = folder.item(messageid)
        else:
            data = self.generator(req, folder.items, folder.count)

        self.respond(req, resp, data)

admin_server = kopano.Server(parse_args=False, store_cache=False)
USERID = admin_server.user('user1').userid
userid_sessiondata = {}

users = UserResource()
messages = MessageResource()
folders = FolderResource()
calendars = CalendarResource()
events = EventResource()

app = falcon.API()
app.add_route('/me', users)
app.add_route('/users', users)
app.add_route('/users/{userid}', users)

for user in ('/me', '/users/{userid}'):
    app.add_route(user+'/mailFolders', folders)
    app.add_route(user+'/mailFolders/{folderid}', folders)

    app.add_route(user+'/messages', messages)
    app.add_route(user+'/messages/{messageid}', messages)
    app.add_route(user+'/mailFolders/{folderid}/messages', messages)
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}', messages)

    app.add_route(user+'/calendar', calendars)
    app.add_route(user+'/calendars', calendars)
    app.add_route(user+'/calendars/{folderid}', calendars)
    app.add_route(user+'/calendar/calendarView', calendars)
    app.add_route(user+'/calendars/{folderid}/calendarView', calendars)

    app.add_route(user+'/events', events)
    app.add_route(user+'/calendar/events', events)
    app.add_route(user+'/calendars/{folderid}/events', events)
