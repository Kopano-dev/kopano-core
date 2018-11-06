# SPDX-License-Identifier: AGPL-3.0-only
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

import sys

from .version import __version__
from .config import Config, CONFIG
from .errors import (
    Error, ConfigError, DuplicateError, NotFoundError, LogonError,
    NotSupportedError, ArgumentError,
)
from .server import Server
from .address import Address
from .attachment import Attachment
from .autoaccept import AutoAccept
from .company import Company
from .compat import set_bin_encoding, set_missing_none, hex, unhex, benc, bdec
from .delegation import Delegation
from .distlist import DistList
from .folder import Folder
from .freebusy import FreeBusyBlock, FreeBusy
from .group import Group
from .item import Item
from .log import log_exc, QueueListener, logger
from .meetingrequest import MeetingRequest
from .outofoffice import OutOfOffice
from .property_ import Property
from .permission import Permission
from .picture import Picture
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

# TODO add kopano.servers?

class Module(object):
    def __init__(self, module):
        self.__module = module
        self.__server = None

    def __getattr__(self, name):
        return getattr(self.__module, name)

    @property
    def _server(self):
        if not self.__server:
            self.__server = Server()
        return self.__server

    @property # this is the reason we need a class
    def public_store(self):
        return self._server.public_store

    def user(self, *args, **kwargs):
        return self._server.user(*args, **kwargs)

    def users(self, *args, **kwargs):
        return self._server.users(*args, **kwargs)

    def group(self, *args, **kwargs):
        return self._server.group(*args, **kwargs)

    def groups(self, *args, **kwargs):
        return self._server.groups(*args, **kwargs)

    def store(self, *args, **kwargs):
        return self._server.store(*args, **kwargs)

    def stores(self, *args, **kwargs):
        return self._server.stores(*args, **kwargs)

    def company(self, *args, **kwargs):
        return self._server.company(*args, **kwargs)

    def companies(self, *args, **kwargs):
        return self._server.companies(*args, **kwargs)

sys.modules[__name__] = Module(sys.modules[__name__])

