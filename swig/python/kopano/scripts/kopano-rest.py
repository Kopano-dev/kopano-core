import base64
import dateutil.parser
import json
import os
from threading import Thread
try:
    import urlparse
except ImportError:
    import urllib.parse as urlparse

from MAPI.Util import kc_session_save, kc_session_restore, GetDefaultStore

import falcon
import kopano

kopano.set_bin_encoding('base64')
kopano.set_missing_none()

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
# TODO Message.{isDraft, webLink, inferenceClassification}?
# TODO Event.{webLink, onlineMeetingUrl, uniqueBody}?
# TODO Attachment.{contentId, contentLocation}?
# TODO /me/{mailFolders,contactFolders,calendars} -> which folders exactly?
# TODO @odata.context: check exact structure?
# TODO delta: delete requires entryid.. finally fix ICS?
# TODO ICS, filters etc & pagination?.. ugh
# TODO calendarresource fields
# TODO $count seems broken for ms graph?
# TODO $filter, $search

def _server(req):
    userid = os.getenv('DEBUG_KOPANO_REST_USERID') or req.get_header('X-Kopano-UserEntryID', required=True)
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

def _server_notif(req):
    userid = os.getenv('DEBUG_KOPANO_REST_USERID') or req.get_header('X-Kopano-UserEntryID', required=True)
    username = admin_server.user(userid=userid).name
    return kopano.Server(auth_user=username, auth_pass='',
                           parse_args=False, store_cache=False, notifications=True)

def _store(server, userid):
    if userid:
        return server.user(userid=userid).store
    else:
        return kopano.Store(server=server,
                            mapiobj=GetDefaultStore(server.mapisession))

def _date(d, local=False, time=True):
    if d is None:
        return '0001-01-01T00:00:00Z'
    fmt = '%Y-%m-%d'
    if time:
        fmt += 'T%H:%M:%S'
    if d.microsecond:
        fmt += '.%f'
    if not local:
        fmt += 'Z'
    return d.strftime(fmt)

class Resource(object):
    def get_fields(self, req, obj, fields, all_fields):
        fields = fields or all_fields or self.fields
        result = {}
        for f in all_fields:
            try:
                result[f] = all_fields[f](obj)
            except TypeError: # TODO
                result[f] = all_fields[f](req, obj)
        # TODO do not handle here
        if '@odata.type' in result and not result['@odata.type']:
            del result['@odata.type']
        return result

    def json(self, req, obj, fields, all_fields, multi=False, expand=None):
        data = self.get_fields(req, obj, fields, all_fields)
        if not multi:
            data['@odata.context'] = req.path
        if expand:
            data.update(expand)
        return json.dumps(data, indent=2, separators=(',', ': '))

    def json_multi(self, req, obj, fields, all_fields, top, skip, count, deltalink, add_count=False):
        header = b'{\n'
        header += b'  "@odata.context": "%s",\n' % req.path.encode('utf-8')
        if add_count:
              header += b'  "@odata.count": "%d",\n' % count
        if deltalink:
              header += b'  "@odata.deltaLink": "%s",\n' % deltalink
        elif skip+top < count:
              header += b'  "@odata.nextLink": "%s?$skip=%d",\n' % (req.path.encode('utf-8'), skip+top)
        header += b'  "value": [\n'
        yield header
        first = True
        for o in obj:
            if isinstance(o, tuple):
                o, resource = o
                all_fields = resource.fields
            if not first:
                yield b',\n'
            first = False
            wa = self.json(req, o, fields, all_fields, multi=True).encode('utf-8')
            yield b'\n'.join([b'    '+line for line in wa.splitlines()])
        yield b'\n  ]\n}'

    def respond(self, req, resp, obj, all_fields=None, deltalink=None):
        # determine fields
        args = urlparse.parse_qs(req.query_string)
        if '$select' in args:
            fields = set(args['$select'][0].split(',') + ['@odata.type', '@odata.etag', 'id'])
        else:
            fields = None

        resp.content_type = "application/json"

        # multiple objects: stream
        if isinstance(obj, tuple):
            obj, top, skip, count = obj
            add_count = '$count' in args and args['$count'][0] == 'true'

            resp.stream = self.json_multi(req, obj, fields, all_fields or self.fields, top, skip, count, deltalink, add_count)

        # single object
        else:
            # expand sub-objects # TODO stream?
            expand = None
            if '$expand' in args:
                expand = {}
                for field in args['$expand'][0].split(','):
                    if hasattr(self, 'relations') and field in self.relations:
                        objs, resource = self.relations[field](obj)
                        expand[field] = [self.get_fields(req, obj2, resource.fields, resource.fields) for obj2 in objs()]

                    elif hasattr(self, 'expansions') and field in self.expansions:
                        obj2, resource = self.expansions[field](obj)
                        # TODO item@odata.context, @odata.type..
                        expand[field.split('/')[1]] = self.get_fields(req, obj2, resource.fields, resource.fields)

            resp.body = self.json(req, obj, fields, all_fields or self.fields, expand=expand)

    def generator(self, req, generator, count=0):
        # determine pagination and ordering
        args = urlparse.parse_qs(req.query_string)
        top = int(args['$top'][0]) if '$top' in args else TOP
        skip = int(args['$skip'][0]) if '$skip' in args else 0
        order = args['$orderby'][0].split(',') if '$orderby' in args else None
        if order:
            order = tuple(('-' if len(o.split()) > 1 and o.split()[1] == 'desc' else '')+o.split()[0] for o in order)
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
            data = self.generator(req, store.mail_folders, 0)
            self.respond(req, resp, data, MailFolderResource.fields)

        elif method == 'contactFolders':
            data = self.generator(req, store.contacts.folders, 0)
            self.respond(req, resp, data, ContactFolderResource.fields)

        elif method == 'calendars':
            data = self.generator(req, store.calendars, 0)
            self.respond(req, resp, data, CalendarResource.fields)

        elif method == 'calendar':
            data = store.calendar
            self.respond(req, resp, data, CalendarResource.fields)

        elif method == 'messages':
            inbox = store.inbox
            args = urlparse.parse_qs(req.query_string) # TODO generalize
            if '$search' in args:
                text = args['$search'][0]
                def yielder(**kwargs):
                    for item in inbox.search(text):
                        yield item
                data = self.generator(req, yielder, 0)
            else:
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

class FolderResource(Resource):
    fields = {
        'id': lambda folder: folder.entryid,
    }

    def on_delete(self, req, resp, userid=None, folderid=None):
        server = _server(req)
        store = _store(server, userid)
        folder = store.folder(entryid=folderid)

        store.delete(folder)

class ItemResource(Resource):
    fields = {
        '@odata.etag': lambda item: 'W/"'+item.changekey+'"',
        'id': lambda item: item.entryid,
        'changeKey': lambda item: item.changekey,
        'createdDateTime': lambda item: _date(item.created),
        'lastModifiedDateTime': lambda item: _date(item.last_modified),
        'categories': lambda item: item.categories,
    }

class MailFolderResource(FolderResource):
    fields = FolderResource.fields.copy()
    fields.update({
        'parentFolderId': lambda folder: folder.parent.entryid,
        'displayName': lambda folder: folder.name,
        'unreadItemCount': lambda folder: folder.unread,
        'totalItemCount': lambda folder: folder.count,
        'childFolderCount': lambda folder: folder.subfolder_count,
    })

    relations = {
        'childFolders': lambda folder: (folder.folders, MailFolderResource),
        'messages': lambda folder: (folder.items, MessageResource) # TODO event msgs
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
            args = urlparse.parse_qs(req.query_string) # TODO generalize
            if '$search' in args:
                text = args['$search'][0]
                folder = data
                def yielder(**kwargs):
                    for item in folder.search(text):
                        yield item
                data = self.generator(req, yielder, 0)
            else:
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

class CalendarResource(FolderResource):
    fields = FolderResource.fields.copy()
    fields.update({
        'displayName': lambda folder: folder.name,
    })

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

def get_body(req, item):
    if req.get_header('Prefer') == 'outlook.body-content-type="text"' or item.body_type == 'text':
        return {'contentType': 'text', 'content': item.text}
    else:
        return {'contentType': 'html', 'content': item.html.decode('utf8')}, # TODO if not utf8?

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

def get_attachments(item):
    for attachment in item.attachments(embedded=True):
        if attachment.embedded:
            yield (attachment, ItemAttachmentResource)
        else:
            yield (attachment, FileAttachmentResource)

class MessageResource(ItemResource):
    fields = ItemResource.fields.copy()
    fields.update({
        # TODO pyko shortcut for event messages
        # TODO eventMessage resource?
        '@odata.type': lambda item: '#microsoft.graph.eventMessage' if item.message_class.startswith('IPM.Schedule.Meeting.') else None,
        'subject': lambda item: item.subject,
        'body': lambda req, item: get_body(req, item),
        'from': lambda item: {'emailAddress': {'name': item.from_.name, 'address': item.from_.email} },
        'sender': lambda item: {'emailAddress': {'name': item.sender.name, 'address': item.sender.email} },
        'toRecipients': lambda item: [{'emailAddress': {'name': to.name, 'address': to.email}} for to in item.to],
        'ccRecipients': lambda item: [{'emailAddress': {'name': cc.name, 'address': cc.email}} for cc in item.cc],
        'bccRecipients': lambda item: [{'emailAddress': {'name': bcc.name, 'address': bcc.email}} for bcc in item.bcc],
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
        'bodyPreview': lambda item: item.text[:255],
    })

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
            attachments = list(get_attachments(item))
            data = (attachments, TOP, 0, len(attachments))
            self.respond(req, resp, data)
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

    def on_patch(self, req, resp, userid=None, folderid=None, messageid=None, method=None):
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
        fields = json.loads(req.stream.read().decode('utf-8'))

        for field, value in fields.items():
            if field in self.set_fields:
                self.set_fields[field](item, value)

        self.respond(req, resp, item, MessageResource.fields)

    def on_delete(self, req, resp, userid=None, folderid=None, messageid=None):
        server = _server(req)
        store = _store(server, userid)
        item = store.item(messageid)

        store.delete(item)

class EmbeddedMessageResource(MessageResource):
    fields = MessageResource.fields.copy()
    fields.update({
        'id': lambda item: '',
    })
    del fields['parentFolderId'] # TODO more?

class AttachmentResource(Resource):
    fields = {
        'id': lambda attachment: attachment.entryid,
        'name': lambda attachment: attachment.name,
        'lastModifiedDateTime': lambda attachment: _date(attachment.last_modified),
        'size': lambda attachment: attachment.size,
        'isInline': lambda attachment: False, # TODO
        'contentType': lambda attachment: attachment.mimetype,
    }

    # TODO to ItemAttachmentResource
    expansions = {
        'microsoft.graph.itemAttachment/item': lambda attachment: (attachment.item, EmbeddedMessageResource),
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
            if data.embedded:
                all_fields = ItemAttachmentResource.fields # TODO to sub resource
            else:
                all_fields = FileAttachmentResource.fields
            self.respond(req, resp, data, all_fields=all_fields)

    def on_delete(self, req, resp, userid=None, folderid=None, messageid=None, eventid=None, attachmentid=None, method=None):
        server = _server(req)
        store = _store(server, userid)

        item = store.item(messageid or eventid)
        attachment = item.attachment(attachmentid)

        item.delete(attachment)

class FileAttachmentResource(AttachmentResource):
    fields = AttachmentResource.fields.copy()
    fields.update({
        '@odata.type': lambda attachment: '#microsoft.graph.fileAttachment',
        'contentBytes': lambda attachment: base64.urlsafe_b64encode(attachment.data).decode('ascii'),
    })

class ItemAttachmentResource(AttachmentResource):
    fields = AttachmentResource.fields.copy()
    fields.update({
        '@odata.type': lambda attachment: '#microsoft.graph.itemAttachment',
    })

pattern_map = {
    'month': 'absoluteMonthly',
    'month_rel': 'relativeMonthly',
    'day': 'daily',
    'week': 'weekly',
    'year': 'absoluteYearly',
    'year_rel': 'relativeYearly',
}

range_end_map = {
    'end_date': 'endDate',
    'occ_count': 'numberOfOccurrences',
    'no_end': 'noEnd',
}

sensitivity_map = {
    'normal': 'Normal',
    'personal': 'Personal',
    'private': 'Private',
    'confidential': 'Confidential',
}

show_as_map = {
    'free': 'Free',
    'tentative': 'Tentative',
    'busy': 'Busy',
    'out_of_office': 'Oof',
    'working_elsewhere': 'WorkingElsewhere',
    'unknown': 'Unknown',
}

def recurrence_json(item):
    if isinstance(item, kopano.Item) and item.recurring:
        recurrence = item.recurrence
        # graph outputs some useless fields here, so we do too!
        j = {
            'pattern': {
                'type': pattern_map[recurrence.pattern],
                'interval': recurrence.interval,
                'month': recurrence.month or 0,
                'dayOfMonth': recurrence.monthday or 0,
                'index': recurrence.index or 'first',
                'firstDayOfWeek': recurrence.first_weekday,
            },
            'range': {
                'type': range_end_map[recurrence.range_type],
                'startDate': _date(recurrence._start, True, False), # TODO .start?
                'endDate': _date(recurrence._end, True, False) if recurrence.range_type != 'no_end' else '0001-01-01',
                'numberOfOccurrences': recurrence.occurrence_count if recurrence.range_type == 'occ_count' else 0,
                'recurrenceTimeZone': "", # TODO
            },
        }
        if recurrence.weekdays:
            j['pattern']['daysOfWeek'] = recurrence.weekdays
        return j

def attendees_json(item):
    result = []
    for attendee in item.attendees():
        address = attendee.address
        result.append({
            # TODO map response field names
            'status': {'response': attendee.response or 'none', 'time': _date(attendee.response_time)},
            'type': attendee.type,
            'emailAddress': {'name': address.name, 'address': address.email},
        })
    return result

def setdate(item, field, arg):
    date = dateutil.parser.parse(arg['dateTime'])
    setattr(item, field, date)

class EventResource(ItemResource):
    fields = ItemResource.fields.copy()
    fields.update({
        'subject': lambda item: item.subject,
        'recurrence': recurrence_json,
        'start': lambda item: {'dateTime': _date(item.start, True), 'timeZone': 'UTC'} if item.start else None,
        'end': lambda item: {'dateTime': _date(item.end, True), 'timeZone': 'UTC'} if item.end else None,
        'location': lambda item: { 'displayName': item.location, 'address': {}}, # TODO
        'importance': lambda item: item.urgency,
        'sensitivity': lambda item: sensitivity_map[item.sensitivity],
        'hasAttachments': lambda item: item.has_attachments,
        'body': lambda item: {'contentType': 'html', 'content': item.html.decode('utf8')}, # TODO if not utf8?
        'isReminderOn': lambda item: item.reminder,
        'reminderMinutesBeforeStart': lambda item: item.reminder_minutes,
        'attendees': lambda item: attendees_json(item),
        'bodyPreview': lambda item: item.text[:255],
        'isAllDay': lambda item: item.all_day,
        'showAs': lambda item: show_as_map[item.show_as],
    })

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
            attachments = list(item.attachments(embedded=True))
            data = (attachments, TOP, 0, len(attachments))
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

class ContactFolderResource(FolderResource):
    fields = FolderResource.fields.copy()
    fields.update({
        'displayName': lambda folder: folder.name,
        'parentFolderId': lambda folder: folder.parent.entryid,
    })

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

def _phys_address(addr):
    data = {
        'street': addr.street,
        'city': addr.city,
        'postalCode': addr.postal_code,
        'state': addr.state,
        'countryOrRegion': addr.country
    }
    return {a:b for (a,b) in data.items() if b}

class ContactResource(ItemResource):
    fields = ItemResource.fields.copy()
    fields.update({
        'displayName': lambda item: item.name,
        'emailAddresses': lambda item: [{'name': a.name, 'address': a.email} for a in item.addresses()],
        'parentFolderId': lambda item: item.folder.entryid,
        'givenName': lambda item: item.first_name or None,
        'middleName': lambda item: item.middle_name or None,
        'surname': lambda item: item.last_name or None,
        'nickName': lambda item: item.nickname or None,
        'title': lambda item: item.title or None,
        'companyName': lambda item: item.company_name or None,
        'mobilePhone': lambda item: item.mobile_phone or None,
        'personalNotes': lambda item: item.text,
        'generation': lambda item: item.generation or None,
        'children': lambda item: item.children,
        'spouseName': lambda item: item.spouse or None,
        'birthday': lambda item: _date(item.birthday),
        'initials': lambda item: item.initials or None,
        'yomiGivenName': lambda item: item.yomi_first_name or None,
        'yomiSurname': lambda item: item.yomi_last_name or None,
        'yomiCompanyName': lambda item: item.yomi_company_name or None,
        'fileAs': lambda item: item.file_as,
        'jobTitle': lambda item: item.job_title or None,
        'department': lambda item: item.department or None,
        'officeLocation': lambda item: item.office_location or None,
        'profession': lambda item: item.profession or None,
        'manager': lambda item: item.manager or None,
        'assistantName': lambda item: item.assistant or None,
        'businessHomePage': lambda item: item.business_homepage or None,
        'homePhones': lambda item: item.home_phones,
        'businessPhones': lambda item: item.business_phones,
        'imAddresses': lambda item: item.im_addresses,
        'homeAddress': lambda item: _phys_address(item.home_address),
        'businessAddress': lambda item: _phys_address(item.business_address),
        'otherAddress': lambda item: _phys_address(item.business_address),
        'otherAddress': lambda item: _phys_address(item.other_address),
    })

    set_fields = {
        'displayName': lambda item, arg: setattr(item, 'name', arg),
        'emailAddresses': set_email_addresses,
    }

    def on_get(self, req, resp, userid=None, folderid=None, contactid=None, method=None, value=None):
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

        if method == 'photo':
            photo = data.photo
            if value == '$value':
                resp.content_type = photo.mimetype
                resp.data = photo.data
            else:
                self.respond(req, resp, photo, ProfilePhotoResource.fields)
            return

        self.respond(req, resp, data)

    def on_delete(self, req, resp, userid=None, folderid=None, contactid=None):
        server = _server(req)
        store = _store(server, userid)
        item = store.item(contactid)

        store.delete(item)

class ProfilePhotoResource(Resource):
    fields = {
        '@odata.mediaContentType': lambda photo: photo.mimetype,

    }

    # TODO so empty..

class NotificationThread(Thread):
    def __init__(self, store, url):
        Thread.__init__(self)

        self.store = store
        self.url = url

    def run(self):
        sink = self.store.advise()
        for n in sink.notifications(time=1000000):
            print('notify URL:', self.url)

class SubscriptionResource(Resource):

    def on_post(self, req, resp):
        server = _server_notif(req)
        store = _store(server, None)

        fields = json.loads(req.stream.read().decode('utf-8'))

        NotificationThread(store, fields['notificationUrl']).start()

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
subscriptions = SubscriptionResource()

app = falcon.API()

# users (method=mailFolders,contactFolders,calendar,calendars,messages,contacts,events,sendMail)
app.add_route(PREFIX+'/subscriptions', subscriptions)
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
    app.add_route(user+'/contacts/{contactid}/{method}', contacts)
    app.add_route(user+'/contacts/{contactid}/{method}/{value}', contacts) # TODO
