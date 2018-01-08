import base64
import dateutil.parser
import json
import pytz
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
# TODO Message.{bodyPreview, isDraft}?
# TODO /me/{contactFolders,calendars} -> which folders exactly?

def _server(req):
    userid = req.get_header('X-Kopano-UserEntryID', required=True)
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

# TODO use json hook?
# TODO make python-kopano add UTC
def _date(d):
    return d.replace(microsecond=0, tzinfo=pytz.UTC).isoformat()

class Resource(object):
    def get_fields(self, obj, fields, all_fields):
        result = {f: all_fields[f](obj) for f in fields if f in all_fields}
        # TODO move to resp. resource classes
        if (isinstance(obj, kopano.Item) and \
            # TODO pyko shortcut
            obj.message_class.startswith('IPM.Schedule.Meeting.')):
            result['@odata.type'] = '#microsoft.graph.eventMessage'
        elif isinstance(obj, kopano.Attachment):
            result['@odata.type'] = '#microsoft.graph.fileAttachment'
        return result

    def json(self, obj, fields, all_fields):
        return json.dumps(self.get_fields(obj, fields, all_fields),
            indent=4, separators=(',', ': ')
        )

    def json_multi(self, req, obj, fields, all_fields, top, skip, count):
        header = b'{\n'
        header += b'    "@odata.context": "%s",\n' % req.path.encode('utf-8')
        if skip+top < count:
              header += b'    "@odata.nextLink": "%s?$skip=%d",\n' % (req.path.encode('utf-8'), skip+top)
        header += b'    "value": [\n'
        yield header
        first = True
        for o in obj:
            if not first:
                yield b',\n'
            first = False
            wa = self.json(o, fields, all_fields).encode('utf-8')
            yield b'\n'.join([b'        '+line for line in wa.splitlines()])
        yield b'\n    ]\n}'

    def respond(self, req, resp, obj, all_fields=None):
        # determine fields
        args = urlparse.parse_qs(req.query_string)
        fields = all_fields or self.fields
        if '$select' in args:
            fields = set(args['$select'][0].split(',') + ['@odata.type', '@odata.etag', 'id'])
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
        'mail': lambda user: user.email,
    }

    def on_get(self, req, resp, userid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        if not method:
            if req.path.split('/')[-1] == 'me':
                userid = kopano.Store(server=server,
                    mapiobj = GetDefaultStore(server.mapisession)).user.userid

            if userid:
                data = server.user(userid=userid)
            else:
                data = self.generator(req, server.users)

            self.respond(req, resp, data)

        elif method == 'mailFolders':
            data = self.generator(req, store.folders, store.subtree.subfolder_count_recursive)
            self.respond(req, resp, data, MailFolderResource.fields)

        elif method == 'contactFolders':
            def yielder(**kwargs):
                yield store.contacts # TODO which folders exactly?
            data = self.generator(req, yielder, 1)
            self.respond(req, resp, data, MailFolderResource.fields)

        elif method == 'calendars':
            def yielder(**kwargs):
                yield store.calendar # TODO which folders exactly?
            data = self.generator(req, yielder, 1)
            self.respond(req, resp, data, CalendarResource.fields)

        elif method == 'calendar':
            data = store.calendar
            self.respond(req, resp, data, CalendarResource.fields)

        elif method == 'messages':
            inbox = store.inbox
            data = self.generator(req, inbox.items, inbox.count)
            self.respond(req, resp, data, MessageResource.fields)

        elif method == 'contacts':
            contacts = store.contacts
            data = self.generator(req, contacts.items, contacts.count)
            self.respond(req, resp, data, ContactResource.fields)

        elif method == 'events':
            calendar = store.calendar
            data = self.generator(req, calendar.items, calendar.count)
            self.respond(req, resp, data, EventResource.fields)

    def on_post(self, req, resp, userid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        if method == 'sendMail':
            # TODO save in sent items?
            fields = json.loads(req.stream.read().decode('utf-8'))['message']
            self.create_message(store.outbox, fields, MessageResource.set_fields).send()

class MailFolderResource(Resource):
    fields = {
        'id': lambda folder: folder.entryid,
        'parentFolderId': lambda folder: folder.parent.entryid,
        'displayName': lambda folder: folder.name,
        'unreadItemCount': lambda folder: folder.unread,
        'totalItemCount': lambda folder: folder.count,
    }

    def on_get(self, req, resp, userid=None, folderid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid:
            if folderid == 'inbox': # XXX more
                data = store.inbox
            else:
                data = store.folder(entryid=folderid)
        else:
            data = self.generator(req, store.folders, store.subtree.subfolder_count_recursive)

        if method == 'childFolders':
            data = self.generator(req, data.folders, data.subfolder_count_recursive)
        elif method == 'messages':
            data = self.generator(req, data.items, data.count)
            self.respond(req, resp, data, MessageResource.fields)
            return

        self.respond(req, resp, data)

    def on_post(self, req, resp, userid=None, folderid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid == 'inbox': # XXX more
            folder = store.inbox
        else:
            folder = store.folder(entryid=folderid)

        if method == 'messages':
            fields = json.loads(req.stream.read().decode('utf-8'))
            item = self.create_message(folder, fields, MessageResource.set_fields)

            self.respond(req, resp, item, MessageResource.fields)

    def on_delete(self, req, resp, userid=None, folderid=None):
        server = _server(req)
        store = _store(server, userid)
        folder = store.folder(entryid=folderid)

        store.delete(folder)

class CalendarResource(Resource):
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
            if folderid == 'calendar':
                folder = store.calendar
            elif folderid:
                folder = store.folder(entryid=folderid)
            else:
                folder = store.calendar

            if method == 'calendarView':
                args = urlparse.parse_qs(req.query_string)
                start = dateutil.parser.parse(args['startDateTime'][0])
                end = dateutil.parser.parse(args['endDateTime'][0])
                data = (folder.occurrences(start, end), TOP, 0, 0)
                fields = EventResource.fields

            elif method == 'events':
                data = self.generator(req, folder.items, folder.count)
                fields = EventResource.fields

            else:
                data = folder

        self.respond(req, resp, data, fields)

    def on_post(self, req, resp, userid=None, folderid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        folder = store.calendar # TODO

        if method == 'events':
            fields = json.loads(req.stream.read().decode('utf-8'))
            item = self.create_message(folder, fields, EventResource.set_fields)

            self.respond(req, resp, item, EventResource.fields)

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
        'createdDateTime': lambda item: _date(item.created),
        'categories': lambda item: item.categories,
        'changeKey': lambda item: item.changekey,
        'subject': lambda item: item.subject,
        'body': lambda item: {'contentType': 'html', 'content': item.html.decode('utf8')}, # TODO if not utf8?
        'from': lambda item: {'emailAddress': {'name': item.from_.name, 'address': item.from_.email} },
        'sender': lambda item: {'emailAddress': {'name': item.sender.name, 'address': item.sender.email} },
        'toRecipients': lambda item: [{'emailAddress': {'name': to.name, 'address': to.email}} for to in item.to],
        'ccRecipients': lambda item: [{'emailAddress': {'name': cc.name, 'address': cc.email}} for cc in item.cc],
        'bccRecipients': lambda item: [{'emailAddress': {'name': bcc.name, 'address': bcc.email}} for bcc in item.bcc],
        'lastModifiedDateTime': lambda item: _date(item.last_modified),
        'sentDateTime': lambda item: _date(item.sent) if item.sent else None,
        'receivedDateTime': lambda item: _date(item.received) if item.received else None,
        'hasAttachments': lambda item: item.has_attachments,
        'internetMessageId': lambda item: item.messageid,
        'importance': lambda item: item.urgency,
        'parentFolderId': lambda item: item.folder.entryid,
        'conversationId': lambda item: item.conversationid,
        'isRead': lambda item: item.read,
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
            if folderid == 'inbox': # XXX more
                folder = store.inbox
            else:
                folder = store.folder(entryid=folderid)
        else:
            folder = store.inbox # TODO messages from all folders?

        item = folder.item(messageid)

        if method:
            body = json.loads(req.stream.read().decode('utf-8'))
            folder = store.folder(entryid=body['destinationId'].encode('ascii')) # TODO ascii?

            if method == 'copy':
                item = item.copy(folder)
            elif method == 'move':
                item = item.move(folder)

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
        'contentBytes': lambda attachment: str(base64.urlsafe_b64encode(attachment.data)),
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
                'startDate': _date(recurrence._start), # TODO .start?
                'endDate': _date(recurrence._end), # TODO .start?
                # TODO timezone
            },
        }

def setdate(item, field, arg):
    date = dateutil.parser.parse(arg['dateTime'])
    setattr(item, field, date)

class EventResource(Resource):
    fields = {
        'id': lambda item: item.entryid,
        'subject': lambda item: item.subject,
        'recurrence': recurrence_json,
        'start': lambda item: {'dateTime': _date(item.start), 'timeZone': 'UTC'} if item.start else None,
        'end': lambda item: {'dateTime': _date(item.end), 'timeZone': 'UTC'} if item.end else None,
    }

    set_fields = {
        'subject': lambda item, arg: setattr(item, 'subject', arg),
        'start': lambda item, arg: setdate(item, 'start', arg),
        'end': lambda item, arg: setdate(item, 'end', arg),
    }

    def on_get(self, req, resp, userid=None, folderid=None, eventid=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid:
            folder = store.folder(entryid=folderid)
        else:
            folder = store.calendar

        data = folder.item(eventid)
        self.respond(req, resp, data)

    def on_delete(self, req, resp, userid=None, folderid=None, eventid=None):
        server = _server(req)
        store = _store(server, userid)

        item = store.item(eventid)
        store.delete(item)

class ContactFolderResource(Resource):
    fields = {
        'id': lambda folder: folder.entryid,
        'displayName': lambda folder: folder.name,
        'parentFolderId': lambda folder: folder.parent.entryid,
    }

    def on_get(self, req, resp, userid=None, folderid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid == 'contacts':
            folder = store.contacts
        else:
            folder = store.folder(entryid=folderid)

        if method == 'contacts':
            data = self.generator(req, folder.items, folder.count)
            fields = ContactResource.fields
        else:
            data = folder
            fields = self.fields

        self.respond(req, resp, data, fields)

    def on_post(self, req, resp, userid=None, folderid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid == 'contacts':
            folder = store.contacts
        else:
            folder = store.folder(entryid=folderid)

        if method == 'contacts':
            fields = json.loads(req.stream.read().decode('utf-8'))
            item = self.create_message(folder, fields, ContactResource.set_fields)

            self.respond(req, resp, item, ContactResource.fields)

def set_email_addresses(item, arg): # TODO multiple via pyko
    item.address1 = '%s <%s>' % (arg[0]['name'], arg[0]['address'])

class ContactResource(Resource):
    fields = {
        '@odata.etag': lambda item: 'W/"'+item.changekey+'"',
        'id': lambda item: item.entryid,
        'displayName': lambda item: item.name,
        'emailAddresses': lambda item: [{'name': a.name, 'address': a.email} for a in item.addresses()],
    }

    set_fields = {
        'displayName': lambda item, arg: setattr(item, 'name', arg),
        'emailAddresses': set_email_addresses,
    }

    def on_get(self, req, resp, userid=None, folderid=None, contactid=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid:
            folder = store.folder(entryid=folderid)
        else:
            folder = store.contacts # TODO contacts from all folders?

        if contactid:
            data = folder.item(contactid)
        else:
            data = self.generator(req, folder.items, folder.count)

        self.respond(req, resp, data)

    def on_delete(self, req, resp, userid=None, folderid=None, contactid=None):
        server = _server(req)
        store = _store(server, userid)
        item = store.item(contactid)

        store.delete(item)

admin_server = kopano.Server(parse_args=False, store_cache=False)
userid_sessiondata = {}

users = UserResource()
messages = MessageResource()
attachments = AttachmentResource()
folders = MailFolderResource()
calendars = CalendarResource()
events = EventResource()
contactfolders = ContactFolderResource()
contacts = ContactResource()

app = falcon.API()

# users (method=mailFolders,contactFolders,calendar,calendars,messages,contacts,events,sendMail)
app.add_route(PREFIX+'/me', users)
app.add_route(PREFIX+'/me/{method}', users)
app.add_route(PREFIX+'/users', users)
app.add_route(PREFIX+'/users/{userid}', users)
app.add_route(PREFIX+'/users/{userid}/{method}', users)

for user in (PREFIX+'/me', PREFIX+'/users/{userid}'):
    # mailFolders (method=childFolders,messages)
    app.add_route(user+'/mailFolders/{folderid}', folders)
    app.add_route(user+'/mailFolders/{folderid}/{method}', folders)

    # messages (method=copy,move)
    app.add_route(user+'/messages/{messageid}', messages)
    app.add_route(user+'/messages/{messageid}/{method}', messages)
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}', messages)
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}/{method}', messages)

    # calendars (method=calendarView,events)
    app.add_route(user+'/calendar/{method}', calendars)
    app.add_route(user+'/calendars/{folderid}', calendars)
    app.add_route(user+'/calendars/{folderid}/{method}', calendars)

    # events
    app.add_route(user+'/events/{eventid}', events)
    app.add_route(user+'/calendar/events/{eventid}', events)
    app.add_route(user+'/calendars/{folderid}/events', events) # TODO to folder
    app.add_route(user+'/calendars/{folderid}/events/{eventid}', events)

    # attachments
    app.add_route(user+'/messages/{messageid}/attachments', attachments) # TODO to message?
    app.add_route(user+'/messages/{messageid}/attachments/{attachmentid}', attachments)
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}/attachments', attachments) # TODO to message?
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}/attachments/{attachmentid}', attachments)

    app.add_route(user+'/events/{eventid}/attachments', attachments) # TODO to event?
    app.add_route(user+'/events/{eventid}/attachments/{attachmentid}', attachments) # TODO to attachment?

    # contactFolders (method=contacts)
    app.add_route(user+'/contactFolders/{folderid}', contactfolders)
    app.add_route(user+'/contactFolders/{folderid}/{method}', contactfolders)

    # contacts
    app.add_route(user+'/contacts/{contactid}', contacts)
