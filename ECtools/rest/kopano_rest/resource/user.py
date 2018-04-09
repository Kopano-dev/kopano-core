import falcon
from prometheus_client import Counter

from ..utils import _server_store
from .resource import (
    DEFAULT_TOP, Resource, urlparse, _start_end, json
)
from .calendar import CalendarResource
from .contact import ContactResource
from .contactfolder import ContactFolderResource
from .event import EventResource
from .group import GroupResource
from .mailfolder import MailFolderResource
from .message import MessageResource

from MAPI.Util import GetDefaultStore
import kopano # TODO remove?

COUNTER = Counter('user_messages_count', 'User messages count')

class UserImporter:
    def __init__(self):
        self.updates = []
        self.deletes = []

    def update(self, user):
        self.updates.append(user)

    def delete(self, user):
        self.deletes.append(user)


class DeletedUserResource(Resource):
    fields = {
        'id': lambda user: user.userid,
#        '@odata.type': lambda item: '#microsoft.graph.message', # TODO
        '@removed': lambda item: {'reason': 'deleted'} # TODO soft deletes
    }

class UserResource(Resource):
    fields = {
        'id': lambda user: user.userid,
        'displayName': lambda user: user.fullname,
        'jobTitle': lambda user: user.job_title,
        'givenName': lambda user: user.first_name,
        'mail': lambda user: user.email,
        'mobilePhone': lambda user: user.mobile_phone,
        'officeLocation': lambda user: user.office_location,
        'surname': lambda user: user.last_name,
        'userPrincipalName': lambda user: user.name,
    }

    def delta(self, req, resp, server):
        args = urlparse.parse_qs(req.query_string)
        token = args['$deltatoken'][0] if '$deltatoken' in args else None
        importer = UserImporter()
        newstate = server.sync_gab(importer, token)
        changes = [(o, UserResource) for o in importer.updates] + \
            [(o, DeletedUserResource) for o in importer.deletes]
        data = (changes, DEFAULT_TOP, 0, len(changes))
        deltalink = b"%s?$deltatoken=%s" % (req.path.encode('utf-8'), codecs.encode(newstate, 'ascii'))
        self.respond(req, resp, data, UserResource.fields, deltalink=deltalink)

    # TODO redirect to other resources?
    def on_get(self, req, resp, userid=None, method=None):
        server, store = _server_store(req, userid if userid != 'delta' else None, self.options)

        if not userid and req.path.split('/')[-1] != 'users':
            userid = kopano.Store(server=server,
                mapiobj = GetDefaultStore(server.mapisession)).user.userid

        if method and not store:
            raise falcon.HTTPNotFound(description="The user store has no store")

        if not method:
            if userid:
                if userid == 'delta':
                    self.delta(req, resp, server)
                    return
                else:
                    data = server.user(userid=userid)
            else:
                data = self.generator(req, server.users)

            self.respond(req, resp, data)

        elif method == 'mailFolders':
            data = self.generator(req, store.mail_folders, 0)
            self.respond(req, resp, data, MailFolderResource.fields)

        elif method == 'contactFolders':
            data = self.generator(req, store.contact_folders, 0)
            self.respond(req, resp, data, ContactFolderResource.fields)

        elif method == 'messages': # TODO store-wide?
            COUNTER.inc()
            data = self.folder_gen(req, store.inbox)
            self.respond(req, resp, data, MessageResource.fields)

        elif method == 'contacts':
            data = self.folder_gen(req, store.contacts)
            self.respond(req, resp, data, ContactResource.fields)

        elif method == 'calendar':
            data = store.calendar
            self.respond(req, resp, data, CalendarResource.fields)

        elif method == 'calendars':
            data = self.generator(req, store.calendars, 0)
            self.respond(req, resp, data, CalendarResource.fields)

        elif method == 'events': # TODO multiple calendars?
            calendar = store.calendar
            data = self.generator(req, calendar.items, calendar.count)
            self.respond(req, resp, data, EventResource.fields)

        elif method == 'calendarView': # TODO multiple calendars?
            start, end = _start_end(req)
            data = (store.calendar.occurrences(start, end), DEFAULT_TOP, 0, 0)
            self.respond(req, resp, data, EventResource.fields)

        elif method == 'memberOf':
            user = server.user(userid=userid)
            data = (user.groups(), DEFAULT_TOP, 0, 0)
            self.respond(req, resp, data, GroupResource.fields)

        else:
            raise falcon.HTTPBadRequest(None, "Unsupported segment '%s'" % method)

    # TODO redirect to other resources?
    def on_post(self, req, resp, userid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        fields = json.loads(req.stream.read().decode('utf-8'))

        if method == 'sendMail':
            # TODO save in sent items?
            self.create_message(store.outbox, fields['message'],
                MessageResource.set_fields).send()
            resp.status = falcon.HTTP_202

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
