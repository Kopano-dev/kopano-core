import falcon

from .config import PREFIX

from ..resource.user import UserResource
from ..resource.group import GroupResource
from ..resource.message import MessageResource
from ..resource.attachment import AttachmentResource
from ..resource.mailfolder import MailFolderResource
from ..resource.contactfolder import ContactFolderResource
from ..resource.calendar import CalendarResource
from ..resource.event import EventResource
from ..resource.contact import ContactResource
from ..resource.profilephoto import ProfilePhotoResource

class RestAPI(falcon.API):
    def __init__(self, options=None, middleware=None):
        super().__init__(media_type=None, middleware=middleware)
        self.options = options

        users = UserResource(options)
        groups = GroupResource(options)
        messages = MessageResource(options)
        attachments = AttachmentResource(options)
        mailfolders = MailFolderResource(options)
        calendars = CalendarResource(options)
        events = EventResource(options)
        contactfolders = ContactFolderResource(options)
        contacts = ContactResource(options)
        photos = ProfilePhotoResource(options)

        def route(path, resource, method=True):
            self.add_route(path, resource)
            if method: # TODO make optional in a better way?
                self.add_route(path+'/{method}', resource)

        route(PREFIX+'/me', users)
        route(PREFIX+'/users', users, method=False) # TODO method == ugly
        route(PREFIX+'/users/{userid}', users)
        route(PREFIX+'/groups', groups, method=False)
        route(PREFIX+'/groups/{groupid}', groups)

        for user in (PREFIX+'/me', PREFIX+'/users/{userid}'):
            route(user+'/mailFolders/{folderid}', mailfolders)
            route(user+'/messages/{itemid}', messages)
            route(user+'/mailFolders/{folderid}/messages/{itemid}', messages)
            route(user+'/calendar', calendars)
            route(user+'/calendars/{folderid}', calendars)
            route(user+'/events/{eventid}', events)
            route(user+'/calendar/events/{eventid}', events)
            route(user+'/calendars/{folderid}/events/{eventid}', events)
            route(user+'/messages/{itemid}/attachments/{attachmentid}', attachments)
            route(user+'/mailFolders/{folderid}/messages/{itemid}/attachments/{attachmentid}', attachments)
            route(user+'/events/{eventid}/attachments/{attachmentid}', attachments) # TODO other routes
            route(user+'/calendar/events/{eventid}/attachments/{attachmentid}', attachments)
            route(user+'/calendars/{folderid}/events/{eventid}/attachments/{attachmentid}', attachments)
            route(user+'/contactFolders/{folderid}', contactfolders)
            route(user+'/contacts/{itemid}', contacts)
            route(user+'/contactFolders/{folderid}/contacts/{itemid}', contacts)
            route(user+'/photo', photos)
            route(user+'/photos/{photoid}', photos)
            route(user+'/contacts/{itemid}/photo', photos)
            route(user+'/contacts/{itemid}/photos/{photoid}', photos)
            route(user+'/contactFolders/{folderid}/contacts/{itemid}/photo', photos)
            route(user+'/contactFolders/{folderid}/contacts/{itemid}/photos/{photoid}', photos)
