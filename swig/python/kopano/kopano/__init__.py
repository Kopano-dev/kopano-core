"""
High-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)

Some goals:

- To be fully object-oriented, pythonic, layer above MAPI
- To be usable for many common system administration tasks
- To provide full access to the underlying MAPI layer if needed
- To return all text as unicode strings
- To return/accept binary identifiers in readable (hex-encoded) form
- To raise well-described exceptions if something goes wrong

Main classes:

:class:`Server`

:class:`Store`

:class:`User`

:class:`Company`

:class:`Store`

:class:`Folder`

:class:`Item`

:class:`Body`

:class:`Attachment`

:class:`Address`

:class:`OutOfOffice`

:class:`Quota`

:class:`Permission`

:class:`Config`

:class:`Service`


"""

from .config import Config, CONFIG
from .errors import (
    Error, ConfigError, DuplicateError, NotFoundError, LogonError,
    NotSupportedError
)
from .server import Server
from .address import Address
from .attachment import Attachment
from .autoaccept import AutoAccept
from .body import Body
from .company import Company
from .folder import Folder
from .group import Group
from .item import Item
from .log import log_exc, QueueListener
from .outofoffice import OutOfOffice
from .prop import Property
from .permission import Permission
from .quota import Quota
from .recurrence import Recurrence, Occurrence
from .restriction import Restriction
from .rule import Rule
from .store import Store
from .table import Table
from .user import User
from .parser import parser
from .service import Service, Worker, server_socket, client_socket

# interactive shortcuts
def user(*args, **kwargs):
    return Server().user(*args, **kwargs)

def users(*args, **kwargs):
    return Server().users(*args, **kwargs)

def store(*args, **kwargs):
    return Server().store(*args, **kwargs)

def stores(*args, **kwargs):
    return Server().stores(*args, **kwargs)

def company(*args, **kwargs):
    return Server().company(*args, **kwargs)

def companies(*args, **kwargs):
    return Server().companies(*args, **kwargs)
