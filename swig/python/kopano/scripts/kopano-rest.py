import base64
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
kopano.set_bin_encoding('base64')

TOP = 10
PREFIX = '/api/gc/v0'

# TODO /me/messages does not return _all_ messages in store
# TODO pagination for non-messages
# TODO check https://developer.microsoft.com/en-us/graph/docs/concepts/query_parameters
# TODO post, put, delete (all resource types)
# TODO copy/move/send actions
# TODO unicode/encoding checks
# TODO efficient attachment streaming
# TODO be able to use special folder names (eg copy/move)
# TODO bulk copy/move/delete?
# TODO childFolders recursion & (custom?) falcon routing
# TODO Field class

def _server(req):
    userid = USERID or req.get_header('X-Kopano-UserEntryID', required=True)
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
        return {f: all_fields[f](obj) for f in fields if f in all_fields}

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
        if '$select' in args:
            fields = set(args['$select'][0].split(',') + ['@odata.etag', 'id'])
        else:
            fields = set(fields.keys())

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

    def create_message(self, folder, fields, all_fields=None):
        # TODO only save in the end

        item = folder.create_item()
        for field, value in fields.items():
            if field in (all_fields or self.set_fields):
                (all_fields or self.set_fields)[field](item, value)

        return item

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
            data = self.generator(req, server.users)

        self.respond(req, resp, data)

    def on_post(self, req, resp, userid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        fields = json.loads(req.stream.read())['message']
        self.create_message(store.outbox, fields, MessageResource.set_fields).send()
        # TODO save in sent items?

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
            data = self.generator(req, store.folders, store.subtree.subfolder_count_recursive)

        self.respond(req, resp, data)

    def on_delete(self, req, resp, userid=None, folderid=None):
        server = _server(req)
        store = _store(server, userid)
        folder = store.folder(entryid=folderid)

        store.delete(folder)

class CalendarResource(Resource): # TODO merge with FolderResource?
    fields = {
        'id': lambda folder: folder.entryid,
        'displayName': lambda folder: folder.name,
    }

    def on_get(self, req, resp, userid=None, folderid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        path = req.path
        fields = None

        if method:
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

def set_body(item, arg):
    if arg['contentType'] == 'text':
        item.text = arg['content']
    elif arg['contentType'] == 'html':
        item.html = arg['content']

def set_torecipients(item, arg):
    item.to = ';'.join('%s <%s>' % (a['name'], a['address']) for a in arg)

class MessageResource(Resource):
    fields = {
        '@odata.etag': lambda item: 'W/"'+item.changekey+'"',
        'id': lambda item: item.entryid,
        'createdDateTime': lambda item: item.created.isoformat(),
        'categories': lambda item: item.categories,
        'changeKey': lambda item: item.changekey,
        'subject': lambda item: item.subject,
        'body': lambda item: {'contentType': 'html', 'content': item.html},
        'from': lambda item: {'emailAddress': {'name': item.sender.name, 'address': item.sender.email} },
        'toRecipients': lambda item: [{'emailAddress': {'name': to.name, 'address': to.email}} for to in item.to],
        'lastModifiedDateTime': lambda item: item.last_modified.isoformat(),
        'sentDateTime': lambda item: item.sent.isoformat() if item.sent else None,
        'receivedDateTime': lambda item: item.received.isoformat() if item.received else None,
        'hasAttachments': lambda item: item.has_attachments,
    }

    set_fields = {
        'subject': lambda item, arg: setattr(item, 'subject', arg),
        'body': set_body,
        'toRecipients': set_torecipients,
    }

    def on_get(self, req, resp, userid=None, folderid=None, messageid=None, method=None):
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

        if method:
            body = json.loads(req.stream.read())
            folder = store.folder(entryid=body['destinationId'].encode('ascii')) # TODO ascii?
            item = data

            if method == 'copy':
                data = item.copy(folder)
            elif method == 'move':
                data = item.move(folder)

        self.respond(req, resp, data)

    def on_post(self, req, resp, userid=None, folderid=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid:
            folder = store.folder(entryid=folderid)
        else:
            folder = store.inbox # TODO messages from all folders?

        fields = json.loads(req.stream.read())
        item = self.create_message(folder, fields)

        self.respond(req, resp, item)

    def on_delete(self, req, resp, userid=None, folderid=None, messageid=None):
        server = _server(req)
        store = _store(server, userid)
        item = store.item(messageid)

        store.delete(item)

class AttachmentResource(Resource):
    fields = {
        'id': lambda attachment: attachment.entryid,
        'name': lambda attachment: attachment.name,
        'contentBytes': lambda attachment: base64.urlsafe_b64encode(attachment.data),
    }

    def on_get(self, req, resp, userid=None, folderid=None, messageid=None, eventid=None, attachmentid=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid:
            folder = store.folder(entryid=folderid)
        elif eventid:
            folder = store.calendar
        else:
            folder = store.inbox # TODO messages from all folders?

        if eventid:
            item = folder.item(eventid)
        else:
            item = folder.item(messageid)

        if attachmentid:
            data = item.attachment(attachmentid)
        else:
            data = self.generator(req, item.attachments)

        self.respond(req, resp, data)

    def on_delete(self, req, resp, userid=None, folderid=None, messageid=None, eventid=None, attachmentid=None):
        server = _server(req)
        store = _store(server, userid)

        item = store.item(messageid or eventid)
        attachment = item.attachment(attachmentid)

        item.delete(attachment)

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

    def on_get(self, req, resp, userid=None, folderid=None, eventid=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid:
            folder = store.folder(entryid=folderid)
        else:
            folder = store.calendar

        if eventid:
            data = folder.item(eventid)
        else:
            data = self.generator(req, folder.items, folder.count)

        self.respond(req, resp, data)

admin_server = kopano.Server(parse_args=False, store_cache=False)
USERID = None #admin_server.user('user1').userid
userid_sessiondata = {}

users = UserResource()
messages = MessageResource()
attachments = AttachmentResource()
folders = FolderResource()
calendars = CalendarResource()
events = EventResource()

app = falcon.API()

# users
app.add_route(PREFIX+'/me', users)
app.add_route(PREFIX+'/me/{method}', users)
app.add_route(PREFIX+'/users', users)
app.add_route(PREFIX+'/users/{userid}', users)
app.add_route(PREFIX+'/users/{userid}/{method}', users)

for user in (PREFIX+'/me', PREFIX+'/users/{userid}'):
    # folders
    app.add_route(user+'/mailFolders', folders)
    app.add_route(user+'/mailFolders/{folderid}', folders)

    # messages
    app.add_route(user+'/messages', messages)
    app.add_route(user+'/messages/{messageid}', messages)
    app.add_route(user+'/messages/{messageid}/{method}', messages)
    app.add_route(user+'/mailFolders/{folderid}/messages', messages)
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}', messages)
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}/{method}', messages)

    # calendars
    app.add_route(user+'/calendar', calendars)
    app.add_route(user+'/calendar/{method}', calendars)
    app.add_route(user+'/calendars', calendars)
    app.add_route(user+'/calendars/{folderid}', calendars)
    app.add_route(user+'/calendars/{folderid}/{method}', calendars)

    # events
    app.add_route(user+'/events', events)
    app.add_route(user+'/events/{eventid}', events)
    app.add_route(user+'/calendar/events', events)
    app.add_route(user+'/calendar/events/{eventid}', events)
    app.add_route(user+'/calendars/{folderid}/events', events)
    app.add_route(user+'/calendars/{folderid}/events/{eventid}', events)

    # attachments
    app.add_route(user+'/messages/{messageid}/attachments', attachments)
    app.add_route(user+'/messages/{messageid}/attachments/{attachmentid}', attachments)
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}/attachments', attachments)
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}/attachments/{attachmentid}', attachments)

    # TODO via calendar(s)
    app.add_route(user+'/events/{eventid}/attachments', attachments)
    app.add_route(user+'/events/{eventid}/attachments/{attachmentid}', attachments)
