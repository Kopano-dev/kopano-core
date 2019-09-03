# SPDX-License-Identifier: AGPL-3.0-only
"""
High-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)

Some goals:

- To be fully object-oriented, pythonic, layer above MAPI
- To be usable for many common system administration tasks
- To provide full access to the underlying MAPI layer if needed
- To return/accept binary identifiers in readable form
- To raise well-described exceptions if something goes wrong

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
from .appointment import Appointment
from .attachment import Attachment
from .attendee import Attendee
from .autoaccept import AutoAccept
from .autoprocess import AutoProcess
from .company import Company
from .compat import (
    set_bin_encoding, set_missing_none, hex, benc, bdec
)
from .contact import Contact
from .delegation import Delegation
from .distlist import DistList
from .folder import Folder
from .freebusy import FreeBusyBlock, FreeBusy
from .group import Group
from .item import Item
from .log import log_exc, QueueListener, logger
from .meetingrequest import MeetingRequest
from .notification import Notification
from .outofoffice import OutOfOffice
from .property_ import Property
from .properties import Properties
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
            self.__server = Server(_skip_check=True, parse_args=False)
        return self.__server

    @property # this is the reason we need a class
    def public_store(self):
        return self._server.public_store

    # TODO add 'name' argument to lookup node?
    def server(self, *args, **kwargs):
        kwargs['_skip_check'] = True # avoid deprecation warning
        kwargs['parse_args'] = kwargs.get('parse_args', False)
        return Server(*args, **kwargs)

    # TODO add servers() for multiserver

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
