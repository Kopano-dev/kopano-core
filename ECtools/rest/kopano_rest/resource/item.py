import dateutil

from ..config import TOP
from .base import (
    Resource, _header_sub_arg, _date, urlparse
)
from .attachment import (
    ItemAttachmentResource, FileAttachmentResource,
)

def get_body(req, item):
    type_ = _header_sub_arg(req, 'Prefer', 'outlook.body-content-type') or item.body_type

    if type_ == 'text':
        return {'contentType': 'text', 'content': item.text}
    else:
        return {'contentType': 'html', 'content': item.html.decode('utf8')}, # TODO if not utf8?

def set_body(item, arg):
    if arg['contentType'] == 'text':
        item.text = arg['content']
    elif arg['contentType'] == 'html':
        item.html = arg['content']

def get_email(addr):
    return {'emailAddress': {'name': addr.name, 'address': addr.email} }

class ItemResource(Resource):
    fields = {
        '@odata.etag': lambda item: 'W/"'+item.changekey+'"',
        'id': lambda item: item.entryid,
        'changeKey': lambda item: item.changekey,
        'createdDateTime': lambda item: _date(item.created),
        'lastModifiedDateTime': lambda item: _date(item.last_modified),
        'categories': lambda item: item.categories,
    }


def get_attachments(item):
    for attachment in item.attachments(embedded=True):
        if attachment.embedded:
            yield (attachment, ItemAttachmentResource)
        else:
            yield (attachment, FileAttachmentResource)
