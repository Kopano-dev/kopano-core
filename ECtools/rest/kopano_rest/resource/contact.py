from ..utils import (
    _server_store, _folder
)
from .base import (
    _date,
)
from .item import (
    ItemResource, get_email
)
from .profilephoto import ProfilePhotoResource

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
        'emailAddresses': lambda item: [get_email(a) for a in item.addresses()],
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
        'otherAddress': lambda item: _phys_address(item.other_address),
    })

    set_fields = {
        'displayName': lambda item, arg: setattr(item, 'name', arg),
        'emailAddresses': set_email_addresses,
    }

    def on_get(self, req, resp, userid=None, folderid=None, itemid=None, method=None):
        server, store = _server_store(req, userid, self.options)
        folder = _folder(store, folderid or 'contacts') # TODO all folders?

        if itemid == 'delta':
            self.delta(req, resp, folder)
            return

        if itemid:
            data = folder.item(itemid)
        else:
            data = self.generator(req, folder.items, folder.count)

        if method == 'photo':
            user = server.user(userid=userid)
            photo = data.photo
            if req.path.split('/')[-1] == '$value':
                resp.content_type = photo.mimetype
                resp.data = photo.data
            else:
                self.respond(req, resp, photo, ProfilePhotoResource.fields)
            return


        self.respond(req, resp, data)

    def on_delete(self, req, resp, userid=None, folderid=None, itemid=None):
        server, store = _server_store(req, userid, self.options)
        item = store.item(itemid)
        store.delete(item)
