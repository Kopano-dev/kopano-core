# SPDX-License-Identifier: AGPL-3.0-or-later
import falcon

from .config import PREFIX

class BackendMiddleware(object):
    def __init__(self, name_backend, default_backend, options):
        self.name_backend = name_backend
        self.default_backend = default_backend
        self.options = options

    def process_resource(self, req, resp, resource, params):
        # redirects resource to correct backend

        backend = None

        # userid prefixed with backend name, e.g. imap.userid
        userid = params.get('userid')
        if userid:
            # TODO handle unknown backend
            for name in self.name_backend:
                if userid.startswith(name+'.'):
                    backend = self.name_backend[name]
                    params['userid'] = userid[len(name)+1:]
                    break

        # userresource method determines backend type # TODO solve nicer in routing? (fight the falcon)
        method = params.get('method')
        if not backend and resource.name == 'UserResource' and method:
            if method in (
                'messages',
                'mailFolders'
            ):
                backend = self.default_backend['mail']
            elif method in (
                'contacts',
                'contactFolders',
                'memberOf',
                'photos'
            ):
                backend = self.default_backend['directory']
            else:
                backend = self.default_backend['calendar']

        # fall back to default backend for type
        if not backend:
            backend = resource.default_backend

        # result: eg ldap.UserResource() or kopano.MessageResource()
        resource.resource = getattr(backend, resource.name)(self.options)

class BackendResource(object):
    def __init__(self, default_backend, resource_name):
        self.default_backend = default_backend
        self.name = resource_name
        # self.resource is set by BackendMiddleware

    def on_get(self, *args, **kwargs):
        return self.resource.on_get(*args, **kwargs)

    def on_post(self, *args, **kwargs):
        return self.resource.on_post(*args, **kwargs)

    def on_patch(self, *args, **kwargs):
        return self.resource.on_patch(*args, **kwargs)

    def on_delete(self, *args, **kwargs):
        return self.resource.on_delete(*args, **kwargs)


class RestAPI(falcon.API):
    def __init__(self, options=None, middleware=None, backends=None):
#        backends = ['ldap', 'imap', 'caldav']

        if backends is None:
            backends = ['kopano']

        name_backend = {}
        for name in backends:
            backend = self.import_backend(name)
            name_backend[name] = backend

        backend_types = {
            'ldap': ['directory'],
            'kopano': ['directory', 'mail', 'calendar'],
            'imap': ['mail'],
            'caldav': ['calendar'],
        }

        default_backend = {}
        for type_ in ('directory', 'mail', 'calendar'):
            for name, types in backend_types.items():
                if name in backends and type_ in types:
                    default_backend[type_] = name_backend[name] # TODO type occurs twice

        middleware = (middleware or []) + [BackendMiddleware(name_backend, default_backend, options)]
        super().__init__(media_type=None, middleware=middleware)

        self.add_routes(default_backend, options)

    def route(self, path, resource, method=True):
        self.add_route(path, resource)
        if method: # TODO make optional in a better way?
            self.add_route(path+'/{method}', resource)

    def import_backend(self, name):
        # import ..backend.<name>
        return __import__('backend.'+name, globals=globals(), fromlist=[''], level=2)

    def add_routes(self, default_backend, options):
        directory = default_backend['directory']
        mail = default_backend['mail']
        calendar = default_backend['calendar']

        users = BackendResource(directory, 'UserResource')
        groups = BackendResource(directory, 'GroupResource')
        contactfolders = BackendResource(directory, 'ContactFolderResource')
        contacts = BackendResource(directory, 'ContactResource')
        photos = BackendResource(directory, 'ProfilePhotoResource')

        messages = BackendResource(mail, 'MessageResource')
        attachments = BackendResource(mail, 'AttachmentResource')
        mailfolders = BackendResource(mail, 'MailFolderResource')

        calendars = BackendResource(calendar, 'CalendarResource')
        events = BackendResource(calendar, 'EventResource')
        calendar_attachments = BackendResource(calendar, 'AttachmentResource')

        self.route(PREFIX+'/me', users)
        self.route(PREFIX+'/users', users, method=False) # TODO method == ugly
        self.route(PREFIX+'/users/{userid}', users)
        self.route(PREFIX+'/groups', groups, method=False)
        self.route(PREFIX+'/groups/{groupid}', groups)

        for user in (PREFIX+'/me', PREFIX+'/users/{userid}'):
            self.route(user+'/contactFolders/{folderid}', contactfolders)
            self.route(user+'/contacts/{itemid}', contacts)
            self.route(user+'/contactFolders/{folderid}/contacts/{itemid}', contacts)
            self.route(user+'/photo', photos)
            self.route(user+'/photos/{photoid}', photos)
            self.route(user+'/contacts/{itemid}/photo', photos)
            self.route(user+'/contacts/{itemid}/photos/{photoid}', photos)
            self.route(user+'/contactFolders/{folderid}/contacts/{itemid}/photo', photos)
            self.route(user+'/contactFolders/{folderid}/contacts/{itemid}/photos/{photoid}', photos)

            self.route(user+'/mailFolders/{folderid}', mailfolders)
            self.route(user+'/messages/{itemid}', messages)
            self.route(user+'/mailFolders/{folderid}/messages/{itemid}', messages)
            self.route(user+'/messages/{itemid}/attachments/{attachmentid}', attachments)
            self.route(user+'/mailFolders/{folderid}/messages/{itemid}/attachments/{attachmentid}', attachments)

            self.route(user+'/events/{eventid}/attachments/{attachmentid}', calendar_attachments) # TODO other routes
            self.route(user+'/calendar/events/{eventid}/attachments/{attachmentid}', calendar_attachments)
            self.route(user+'/calendars/{folderid}/events/{eventid}/attachments/{attachmentid}', calendar_attachments)

            self.route(user+'/calendar', calendars)
            self.route(user+'/calendars/{folderid}', calendars)
            self.route(user+'/events/{eventid}', events)
            self.route(user+'/calendar/events/{eventid}', events)
            self.route(user+'/calendars/{folderid}/events/{eventid}', events)
