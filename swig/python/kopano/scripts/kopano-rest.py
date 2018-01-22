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
# TODO unicode/encoding checks
# TODO efficient attachment streaming? using our own $value approach for now..
#      which _is_ used by graph for contact photos, interestingly
# TODO bulk copy/move/delete?
# TODO childFolders recursion/relative paths
# TODO Message.{bodyPreview, isDraft, webLink, inferenceClassification}?
# TODO Event.{bodyPreview, webLink, onlineMeetingUrl}?
# TODO Attachment.{contentId, contentLocation}?
# TODO /me/{contactFolders,calendars} -> which folders exactly?
# TODO @odata.context: check exact structure
# TODO overlapping class functionality (eg MessageResource, EventResource)
# TODO delta: delete requires entryid.. finally fix ICS?
# TODO ICS, filters etc & pagination?

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

def _date(d, local=False):
    fmt = '%Y-%m-%dT%H:%M:%S'
    if d.microsecond:
        fmt += '.%f'
    if not local:
        fmt += 'Z'
    return d.strftime(fmt)

class Resource(object):
    def get_fields(self, obj, fields, all_fields):
        if isinstance(obj, kopano.Attachment) and obj.embedded: # TODO
            fields = [f for f in fields if f != 'contentBytes']
        result = {f: all_fields[f](obj) for f in fields if f in all_fields}
        # TODO move to resp. resource classes
        if (isinstance(obj, kopano.Item) and \
            # TODO pyko shortcut
            obj.message_class.startswith('IPM.Schedule.Meeting.')):
            result['@odata.type'] = '#microsoft.graph.eventMessage'
        elif isinstance(obj, kopano.Attachment):
            if obj.embedded:
                result['@odata.type'] = '#microsoft.graph.itemAttachment'
            else:
                result['@odata.type'] = '#microsoft.graph.fileAttachment'
        return result

    def json(self, req, obj, fields, all_fields, multi=False):
        data = self.get_fields(obj, fields, all_fields)
        if not multi:
            data['@odata.context'] = req.path
        return json.dumps(data, indent=2, separators=(',', ': '))

    def json_multi(self, req, obj, fields, all_fields, top, skip, count, deltalink):
        header = b'{\n'
        header += b'  "@odata.context": "%s",\n' % req.path.encode('utf-8')
        if deltalink:
              header += b'  "@odata.deltaLink": "%s",\n' % deltalink
        elif skip+top < count:
              header += b'  "@odata.nextLink": "%s?$skip=%d",\n' % (req.path.encode('utf-8'), skip+top)
        header += b'  "value": [\n'
        yield header
        first = True
        for o in obj:
            if not first:
                yield b',\n'
            first = False
            wa = self.json(req, o, fields, all_fields, multi=True).encode('utf-8')
            yield b'\n'.join([b'    '+line for line in wa.splitlines()])
        yield b'\n  ]\n}'

    def respond(self, req, resp, obj, all_fields=None, deltalink=None):
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
            resp.stream = self.json_multi(req, obj, fields, all_fields or self.fields, top, skip, count, deltalink)
        else:
            resp.body = self.json(req, obj, fields, all_fields or self.fields)

    def generator(self, req, generator, count=0):
        # determine pagination and ordering
        args = urlparse.parse_qs(req.query_string)
        top = int(args['$top'][0]) if '$top' in args else TOP
        skip = int(args['$skip'][0]) if '$skip' in args else 0
        order = tuple(args['$orderby'][0].split(',')) if '$orderby' in args else None

        return (generator(page_start=skip, page_limit=top, order=order), top, skip, count)

    def create_message(self, folder, fields, all_fields=None):
        # TODO item.update and/or only save in the end

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

    # TODO redirect to other resources?
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

    # TODO redirect to other resources?
    def on_post(self, req, resp, userid=None, method=None):
        server = _server(req)
        store = _store(server, userid)
        fields = json.loads(req.stream.read().decode('utf-8'))

        if method == 'sendMail':
            # TODO save in sent items?
            self.create_message(store.outbox, fields['message'],
                MessageResource.set_fields).send()

        elif method == 'contacts':
            item = self.create_message(store.contacts, fields,
                ContactResource.set_fields)
            self.respond(req, resp, item, ContactResource.fields)

        elif method == 'messages':
            item = self.create_message(store.drafts, fields,
                MessageResource.set_fields)
            self.respond(req, resp, item, MessageResource.fields)

        elif method == 'events':
            item = self.create_message(store.calendar, fields,
                EventResource.set_fields)
            self.respond(req, resp, item, EventResource.fields)

        elif method == 'mailFolders':
            folder = store.create_folder(fields['displayName']) # TODO exception on conflict
            self.respond(req, resp, folder, MailFolderResource.fields)

class MailFolderResource(Resource):
    fields = {
        'id': lambda folder: folder.entryid,
        'parentFolderId': lambda folder: folder.parent.entryid,
        'displayName': lambda folder: folder.name,
        'unreadItemCount': lambda folder: folder.unread,
        'totalItemCount': lambda folder: folder.count,
        'childFolderCount': lambda folder: folder.subfolder_count,
    }

    def on_get(self, req, resp, userid=None, folderid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid:
            if folderid == 'inbox': # XXX more
                data = store.inbox
            elif folderid == 'drafts':
                data = store.drafts
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

        fields = json.loads(req.stream.read().decode('utf-8'))

        if method == 'messages':
            item = self.create_message(folder, fields, MessageResource.set_fields)
            self.respond(req, resp, item, MessageResource.fields)

        elif method == 'childFolders':
            child = folder.create_folder(fields['displayName']) # TODO exception on conflict
            self.respond(req, resp, child, MailFolderResource.fields)

        elif method in ('copy', 'move'):
            to_folder = store.folder(entryid=fields['destinationId'].encode('ascii')) # TODO ascii?
            if method == 'copy':
                folder.parent.copy(folder, to_folder)
            else:
                folder.parent.move(folder, to_folder)

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

class Importer:
    def __init__(self):
        self.updates = []

    def update(self, item, flags):
        self.updates.append(item)

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
        'isReadReceiptRequested': lambda item: item.read_receipt,
        'isDeliveryReceiptRequested': lambda item: item.read_receipt,
        'replyTo': lambda item: [{'emailAddress': {'name': to.name, 'address': to.email}} for to in item.replyto],
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

        if messageid == 'delta': # TODO move to MailFolder resource somehow?
            # TODO deletes
            args = urlparse.parse_qs(req.query_string)
            token = args['$deltatoken'][0] if '$deltatoken' in args else None
            importer = Importer()
            newstate = folder.sync(importer, token)
            data = (importer.updates, TOP, 0, len(importer.updates))
            deltalink = "%s?deltatoken=%s" % (req.path.encode('utf-8'), newstate)
            self.respond(req, resp, data, MessageResource.fields, deltalink=deltalink)
            return
        else:
            item = folder.item(messageid)

        if method == 'attachments':
            attachments = item.attachments(embedded=True)
            data = (attachments, TOP, 0, attachments)
            self.respond(req, resp, data, AttachmentResource.fields)
            return

        elif method in ('copy', 'move'):
            body = json.loads(req.stream.read().decode('utf-8'))
            folder = store.folder(entryid=body['destinationId'].encode('ascii')) # TODO ascii?

            if method == 'copy':
                item = item.copy(folder)
            else:
                item = item.move(folder)

        self.respond(req, resp, item)

    def on_post(self, req, resp, userid=None, folderid=None, messageid=None, method=None):
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

        if method == 'attachments':
            fields = json.loads(req.stream.read().decode('utf-8'))
            if fields['@odata.type'] == '#microsoft.graph.fileAttachment':
                item.create_attachment(fields['name'], base64.urlsafe_b64decode(fields['contentBytes']))

    def on_delete(self, req, resp, userid=None, folderid=None, messageid=None):
        server = _server(req)
        store = _store(server, userid)
        item = store.item(messageid)

        store.delete(item)

class AttachmentResource(Resource):
    fields = {
        'id': lambda attachment: attachment.entryid,
        'name': lambda attachment: attachment.name,
        'contentBytes': lambda attachment: base64.urlsafe_b64encode(attachment.data).decode('ascii'),
        'lastModifiedDateTime': lambda attachment: _date(attachment.last_modified),
        'size': lambda attachment: attachment.size,
        'isInline': lambda attachment: False, # TODO
        'contentType': lambda attachment: attachment.mimetype,
    }

    def on_get(self, req, resp, userid=None, folderid=None, messageid=None, eventid=None, attachmentid=None, method=None):
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

        data = item.attachment(attachmentid)

        if method == '$value': # TODO graph doesn't do this?
            resp.content_type = data.mimetype
            resp.data = data.data
        else:
            self.respond(req, resp, data)

    def on_delete(self, req, resp, userid=None, folderid=None, messageid=None, eventid=None, attachmentid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        item = store.item(messageid or eventid)
        attachment = item.attachment(attachmentid)

        item.delete(attachment)

pattern_map = {
    'monthly': 'absoluteMonthly',
    'monthnth': 'relativeMonthly',
    'daily': 'daily',
    'weekly': 'weekly'
}

def recurrence_json(item):
    if isinstance(item, kopano.Item) and item.recurring:
        recurrence = item.recurrence
        # graph outputs some useless fields here, so we do too!
        j = {
            'pattern': {
                'type': pattern_map[recurrence.pattern],
                'interval': recurrence.interval,
                'month': 0,
                'dayOfMonth': recurrence.monthday or 0,
                'index': recurrence.index or 'first',
                'firstDayOfWeek': recurrence.first_weekday,
            },
            'range': {
                'type': 'endDate', # TODO
                'startDate': _date(recurrence._start, True), # TODO .start?
                'endDate': _date(recurrence._end, True), # TODO .start?
                # TODO timezone
            },
        }
        if recurrence.weekdays:
            j['pattern']['daysOfWeek'] = recurrence.weekdays
        return j

def attendees_json(item):
    result = []
    for attendee in item.attendees():
        address = attendee.address
        result.append({'emailAddress': {'name': address.name, 'address': address.email}})
    return result

def setdate(item, field, arg):
    date = dateutil.parser.parse(arg['dateTime'])
    setattr(item, field, date)

class EventResource(Resource):
    fields = {
        '@odata.etag': lambda item: 'W/"'+item.changekey+'"',
        'id': lambda item: item.entryid,
        'subject': lambda item: item.subject,
        'recurrence': recurrence_json,
        'start': lambda item: {'dateTime': _date(item.start, True), 'timeZone': 'UTC'} if item.start else None,
        'end': lambda item: {'dateTime': _date(item.end, True), 'timeZone': 'UTC'} if item.end else None,
        'createdDateTime': lambda item: _date(item.created),
        'lastModifiedDateTime': lambda item: _date(item.last_modified),
        'categories': lambda item: item.categories,
        'changeKey': lambda item: item.changekey,
        'location': lambda item: { 'displayName': item.location, 'address': {}}, # TODO
        'importance': lambda item: item.urgency,
        'hasAttachments': lambda item: item.has_attachments,
        'body': lambda item: {'contentType': 'html', 'content': item.html.decode('utf8')}, # TODO if not utf8?
        'isReminderOn': lambda item: item.reminder,
        'reminderMinutesBeforeStart': lambda item: item.reminder_minutes,
        'attendees': lambda item: attendees_json(item),
    }

    set_fields = {
        'subject': lambda item, arg: setattr(item, 'subject', arg),
        'start': lambda item, arg: setdate(item, 'start', arg),
        'end': lambda item, arg: setdate(item, 'end', arg),
    }

    def on_get(self, req, resp, userid=None, folderid=None, eventid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid:
            folder = store.folder(entryid=folderid)
        else:
            folder = store.calendar

        item = folder.item(eventid)

        if method == 'attachments':
            attachments = item.attachments(embedded=True)
            data = (attachments, TOP, 0, attachments)
            self.respond(req, resp, data, AttachmentResource.fields)
            return

        self.respond(req, resp, item)

    def on_post(self, req, resp, userid=None, folderid=None, eventid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        if folderid:
            folder = store.folder(entryid=folderid)
        else:
            folder = store.calendar

        item = folder.item(eventid)

        if method == 'attachments':
            fields = json.loads(req.stream.read().decode('utf-8'))
            if fields['@odata.type'] == '#microsoft.graph.fileAttachment':
                item.create_attachment(fields['name'], base64.urlsafe_b64decode(fields['contentBytes']))

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
        'createdDateTime': lambda item: _date(item.created),
        'lastModifiedDateTime': lambda item: _date(item.last_modified),
        'displayName': lambda item: item.name,
        'emailAddresses': lambda item: [{'name': a.name, 'address': a.email} for a in item.addresses()],
        'categories': lambda item: item.categories,
        'changeKey': lambda item: item.changekey,
        'parentFolderId': lambda item: item.folder.entryid,
        'givenName': lambda item: item.given_name,
        'middleName': lambda item: item.middle_name,
        'surname': lambda item: item.surname,
        'nickName': lambda item: item.nickname,
        'title': lambda item: item.title,
        'companyName': lambda item: item.company_name,
        'mobilePhone': lambda item: item.mobile_phone,
        'personalNotes': lambda item: item.text,
        'generation': lambda item: item.generation,
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

# TODO avoid duplication for {method}?

for user in (PREFIX+'/me', PREFIX+'/users/{userid}'):
    # mailFolders (method=childFolders,messages,copy,move)
    app.add_route(user+'/mailFolders/{folderid}', folders)
    app.add_route(user+'/mailFolders/{folderid}/{method}', folders)

    # messages (method=copy,move,attachments)
    app.add_route(user+'/messages/{messageid}', messages)
    app.add_route(user+'/messages/{messageid}/{method}', messages)
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}', messages)
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}/{method}', messages)

    # calendars (method=calendarView,events)
    app.add_route(user+'/calendar/{method}', calendars)
    app.add_route(user+'/calendars/{folderid}', calendars)
    app.add_route(user+'/calendars/{folderid}/{method}', calendars)

    # events (method=attachments)
    app.add_route(user+'/events/{eventid}', events)
    app.add_route(user+'/events/{eventid}/{method}', events)
    app.add_route(user+'/calendar/events/{eventid}', events)
    app.add_route(user+'/calendar/events/{eventid}/{method}', events)
    app.add_route(user+'/calendars/{folderid}/events/{eventid}', events)
    app.add_route(user+'/calendars/{folderid}/events/{eventid}/{method}', events)

    # attachments
    app.add_route(user+'/messages/{messageid}/attachments/{attachmentid}', attachments)
    app.add_route(user+'/messages/{messageid}/attachments/{attachmentid}/{method}', attachments)
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}/attachments/{attachmentid}', attachments)
    app.add_route(user+'/mailFolders/{folderid}/messages/{messageid}/attachments/{attachmentid}/{method}', attachments)
    app.add_route(user+'/events/{eventid}/attachments/{attachmentid}', attachments)
    app.add_route(user+'/events/{eventid}/attachments/{attachmentid}/{method}', attachments)

    # contactFolders (method=contacts)
    app.add_route(user+'/contactFolders/{folderid}', contactfolders)
    app.add_route(user+'/contactFolders/{folderid}/{method}', contactfolders)

    # contacts
    app.add_route(user+'/contacts/{contactid}', contacts)
