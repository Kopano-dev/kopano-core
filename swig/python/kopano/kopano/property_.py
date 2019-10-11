# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import calendar
import datetime
import sys

from MAPI import (
    PT_ERROR, PT_BINARY, PT_MV_BINARY, PT_UNICODE, PT_LONG, PT_OBJECT,
    PT_BOOLEAN, MV_FLAG, PT_SHORT, PT_MV_SHORT, PT_LONGLONG,
    PT_MV_LONG, PT_FLOAT, PT_MV_FLOAT, PT_DOUBLE, PT_MV_DOUBLE, PT_STRING8,
    PT_CURRENCY, PT_MV_CURRENCY, PT_APPTIME, PT_MV_APPTIME, PT_MV_LONGLONG,
    PT_SYSTIME, MAPI_E_NOT_ENOUGH_MEMORY, PT_MV_STRING8,
    PT_MV_UNICODE, PT_MV_SYSTIME, PT_CLSID, PT_MV_CLSID, MNID_STRING,
    MAPI_E_NOT_FOUND, MNID_ID, MAPI_UNICODE,
)
from MAPI.Defs import (
    PROP_ID, PROP_TYPE, HrGetOneProp,
    CHANGE_PROP_TYPE,
)
from MAPI.Struct import (
    SPropValue, MAPIErrorNotFound, MAPIErrorNoSupport,
    MAPIErrorNotEnoughMemory
)
import MAPI.Tags
from MAPI.Tags import (
    PR_BODY_W, PR_HTML, PR_RTF_COMPRESSED, PR_RTF_IN_SYNC, PR_NULL
)
from MAPI.Time import unixtime

from .defs import (
    REV_TAG, REV_TYPE, GUID_NAMESPACE, MAPINAMEID, NAMESPACE_GUID, STR_GUID,
)
from .compat import (
    benc as _benc, fake_unicode as _unicode,
)
from .errors import Error, NotFoundError

try:
    from . import utils as _utils
except ImportError: # pragma: no cover
    _utils = sys.modules[__package__ + '.utils']
from . import timezone as _timezone

TYPEMAP = {
    'PT_SHORT': PT_SHORT,
    'PT_MV_SHORT': PT_MV_SHORT,
    'PT_LONG': PT_LONG,
    'PT_MV_LONG': PT_MV_LONG,
    'PT_FLOAT': PT_FLOAT,
    'PT_MV_FLOAT': PT_MV_FLOAT,
    'PT_DOUBLE': PT_DOUBLE,
    'PT_MV_DOUBLE': PT_MV_DOUBLE,
    'PT_CURRENCY': PT_CURRENCY,
    'PT_MV_CURRENCY': PT_MV_CURRENCY,
    'PT_APPTIME': PT_APPTIME,
    'PT_MV_APPTIME': PT_MV_APPTIME,
    'PT_BOOLEAN': PT_BOOLEAN,
    'PT_OBJECT': PT_OBJECT,
    'PT_LONGLONG': PT_LONGLONG,
    'PT_MV_LONGLONG': PT_MV_LONGLONG,
    'PT_STRING8': PT_STRING8,
    'PT_MV_STRING8': PT_MV_STRING8,
    'PT_UNICODE': PT_UNICODE,
    'PT_MV_UNICODE': PT_MV_UNICODE,
    'PT_SYSTIME': PT_SYSTIME,
    'PT_MV_SYSTIME': PT_MV_SYSTIME,
    'PT_CLSID': PT_CLSID,
    'PT_MV_CLSID': PT_MV_CLSID,
    'PT_BINARY': PT_BINARY,
    'PT_MV_BINARY': PT_MV_BINARY,
}

 # TODO we may want to use the swigged version in libcommon, once available?
def bestbody(mapiobj):
    # apparently standardized method for determining original message type!
    tag = PR_NULL
    props = mapiobj.GetProps(
        [PR_BODY_W, PR_HTML, PR_RTF_COMPRESSED, PR_RTF_IN_SYNC], 0)

    if (props[3].ulPropTag != PR_RTF_IN_SYNC): # TODO why..
        return tag

    # MAPI_E_NOT_ENOUGH_MEMORY indicates the property exists,
    # but has to be streamed
    if((props[0].ulPropTag == PR_BODY_W or \
        (PROP_TYPE(props[0].ulPropTag) == PT_ERROR and \
         props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY)) and \
       (PROP_TYPE(props[1].ulPropTag) == PT_ERROR and \
        props[1].Value == MAPI_E_NOT_FOUND) and \
       (PROP_TYPE(props[2].ulPropTag) == PT_ERROR and \
        props[2].Value == MAPI_E_NOT_FOUND)):
        tag = PR_BODY_W

    # TODO why not just check MAPI_E_NOT_FOUND..?
    elif((props[1].ulPropTag == PR_HTML or \
          (PROP_TYPE(props[1].ulPropTag) == PT_ERROR and \
           props[1].Value == MAPI_E_NOT_ENOUGH_MEMORY)) and \
         (PROP_TYPE(props[0].ulPropTag) == PT_ERROR and \
          props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY) and \
         (PROP_TYPE(props[2].ulPropTag) == PT_ERROR and \
          props[2].Value == MAPI_E_NOT_ENOUGH_MEMORY) and \
         not props[3].Value):
        tag = PR_HTML

    elif((props[2].ulPropTag == PR_RTF_COMPRESSED or \
          (PROP_TYPE(props[2].ulPropTag) == PT_ERROR and \
           props[2].Value == MAPI_E_NOT_ENOUGH_MEMORY)) and
         (PROP_TYPE(props[0].ulPropTag) == PT_ERROR and \
          props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY) and
         (PROP_TYPE(props[1].ulPropTag) == PT_ERROR and \
          props[1].Value == MAPI_E_NOT_FOUND) and \
         props[3].Value):
        tag = PR_RTF_COMPRESSED

    return tag

def _split_proptag(proptag): # TODO syntax error exception
    parts = proptag.split(':')
    if len(parts) == 2:
        namespace, name = parts
        return None, namespace, name
    else:
        proptype, namespace, name = parts
        return TYPEMAP[proptype], namespace, name

def _name_to_proptag(proptag, mapiobj, proptype=None):
    ptype, namespace, name = _split_proptag(proptag)
    proptype = proptype or ptype

    if name.isdigit(): # TODO
        name = int(name)
    elif name.startswith('0x'):
        name = int(name, 16)

    if namespace in NAMESPACE_GUID:
        guid = NAMESPACE_GUID[namespace]
    elif namespace in STR_GUID:
        guid = STR_GUID[namespace]

    if proptype is None:
        return None, proptype, namespace, name

    nameid = MAPINAMEID(guid, MNID_ID if isinstance(name, int) else \
        MNID_STRING, name)
    # TODO MAPI_CREATE, or too dangerous because of potential db overflow?
    lpname = mapiobj.GetIDsFromNames([nameid], 0)
    proptag = CHANGE_PROP_TYPE(lpname[0], proptype)

    return proptag, proptype, namespace, name

# TODO self
def create_prop(self, mapiobj, proptag, value=None, proptype=None):
    if isinstance(proptag, int) or \
       (isinstance(proptag, str) and ':' not in proptag):
        if isinstance(proptag, str):
            proptag2 = getattr(MAPI.Tags, proptag)
        else:
            proptag2 = proptag
        proptype2 = proptype or PROP_TYPE(proptag)

    else: # named property
        proptag2, proptype2, _, _ = \
            _name_to_proptag(proptag, mapiobj, proptype)

        if proptype2 is None:
            # TODO exception too general?
            raise Error('Missing type to create named property')

    if value is None:
        if proptype2 in (PT_STRING8, PT_UNICODE):
            value = ''
        elif proptype2 == PT_BINARY:
            value = b''
        elif proptype2 == PT_SYSTIME:
            value = unixtime(0)
        elif proptype2 & MV_FLAG:
            value = []
        else:
            value = 0
    else:
        if proptype2 == PT_SYSTIME:
            if value.tzinfo is None:
                value = _timezone._to_utc(value, _timezone.LOCAL)
            else:
                value = value.astimezone(_timezone.UTC)
            value = unixtime(calendar.timegm(value.utctimetuple()))

    # handle invalid type versus value.
    # For example proptype=PT_UNICODE and value=True
    try:
        mapiobj.SetProps([SPropValue(proptag2, value)])
        _utils._save(mapiobj)
    except TypeError:
        raise Error(
            'Could not create property, type and value did not match')

    return prop(self, mapiobj, proptag, proptype=proptype2)

# TODO self
def prop(self, mapiobj, proptag, create=False, value=None, proptype=None):
    if isinstance(proptag, int) or \
       (isinstance(proptag, str) and ':' not in proptag):
        # search for property
        if isinstance(proptag, str):
            proptag = getattr(MAPI.Tags, proptag)
        try:
            sprop = HrGetOneProp(mapiobj, proptag)
        except MAPIErrorNotEnoughMemory:
            data = _utils.stream(mapiobj, proptag)
            sprop = SPropValue(proptag, data)
        except MAPIErrorNotFound:
            # not found, create it?
            if create:
                return create_prop(
                    self, mapiobj, proptag, value=value, proptype=proptype)
            else:
                raise NotFoundError('no such property: %s' %
                    REV_TAG.get(proptag, hex(proptag)))
        return Property(mapiobj, sprop)

    else: # named property
        proptag2, proptype2, namespace, name = \
            _name_to_proptag(proptag, mapiobj, proptype)

        # search for property
        if proptype2:
            try:
                # TODO merge two main branches?
                sprop = HrGetOneProp(mapiobj, proptag2)
                return Property(mapiobj, sprop)
            except MAPIErrorNotEnoughMemory:
                data = _utils.stream(mapiobj, proptag2)
                sprop = SPropValue(proptag2, data)
                return Property(mapiobj, sprop)
            except MAPIErrorNotFound:
                pass
        else:
            # TODO sloow, streaming? default pidlid type-db?
            for prop in self.props(namespace=namespace):
                if prop.name == name:
                    return prop

        # not found, create it?
        if create:
            return create_prop(
                self, mapiobj, proptag, value=value, proptype=proptype)
        else:
            raise NotFoundError('no such property: %s' % proptag)

def props(mapiobj, namespace=None):
    proptags = mapiobj.GetPropList(MAPI_UNICODE)
    sprops = mapiobj.GetProps(proptags, MAPI_UNICODE)
    sprops = [s for s in sprops if not \
        (PROP_TYPE(s.ulPropTag) == PT_ERROR and s.Value == MAPI_E_NOT_FOUND)]
    props = [Property(mapiobj, sprop) for sprop in sprops]

    def prop_key(prop): # sort identically across servers
        if prop.named:
            return (prop.guid, prop.kind, prop.name)
        else:
            return ('', prop.proptag)

    for p in sorted(props, key=prop_key):
        if not namespace or p.namespace == namespace:
            yield p

# do not get full property contents, only when explicitly requested
class SPropDelayedValue(SPropValue):
    def __init__(self, mapiobj, proptag):
        self.mapiobj = mapiobj
        self.ulPropTag = proptag
        self._Value = None

    @property
    def Value(self):
        if self._Value is None:
            try:
                self._Value = _utils.stream(self.mapiobj, self.ulPropTag)
            # TODO eg normalized subject streaming broken..?
            except MAPIErrorNotFound:
                self._Value = None
        return self._Value

class Property(object):
    """Property class

    Low-level abstraction for MAPI properties.
    """

    def __init__(self, parent_mapiobj, mapiobj):
        self._parent_mapiobj = parent_mapiobj
        #: MAPI proptag, for example 0x37001f for PR_SUBJECT_W
        self.proptag = mapiobj.ulPropTag

        if (PROP_TYPE(mapiobj.ulPropTag) == PT_ERROR and \
            mapiobj.Value == MAPI_E_NOT_ENOUGH_MEMORY):
            # avoid slow guessing
            if PROP_ID(self.proptag) == PROP_ID(PR_BODY_W):
                self.proptag = PR_BODY_W
                mapiobj = SPropDelayedValue(parent_mapiobj, self.proptag)
            elif PROP_ID(self.proptag) in \
                (PROP_ID(PR_RTF_COMPRESSED), PROP_ID(PR_HTML)):
                self.proptag = CHANGE_PROP_TYPE(self.proptag, PT_BINARY)
                mapiobj = SPropDelayedValue(parent_mapiobj, self.proptag)
            else: # TODO possible to use above trick to infer all proptags?
                # TODO slow, incomplete?
                for proptype in (PT_BINARY, PT_UNICODE):
                    proptag = (mapiobj.ulPropTag & 0xffff0000) | proptype
                    try:
                        # TODO: Unicode issue?? calls GetProps([proptag], 0)
                        HrGetOneProp(parent_mapiobj, proptag)
                        # TODO how did we end up here, why is this possible
                        self.proptag = proptag
                    except MAPIErrorNotEnoughMemory:
                        mapiobj = SPropDelayedValue(parent_mapiobj, proptag)
                        self.proptag = proptag
                        break
                    except MAPIErrorNotFound:
                        pass

        self.mapiobj = mapiobj

        self._value = None
        self.__lpname = None

    @property
    def id_(self):
        """Id part of proptag, for example 0x37 for PR_SUBJECT_W."""
        return PROP_ID(self.proptag)

    @property
    def idname(self):
        """MAPI proptag name, for example *PR_SUBJECT_W*."""
        return REV_TAG.get(self.proptag)

    @property
    def type_(self):
        """Proptag type, for example 0x1f for PR_SUBJECT_W."""
        return PROP_TYPE(self.proptag)

    @property
    def typename(self):
        """Proptag type name, for example *PT_UNICODE* for PR_SUBJECT_W."""
        return REV_TYPE.get(self.type_)

    @property
    def _lpname(self):
        if self.__lpname is None and self.id_ >= 0x8000:
            try:
                self.__lpname = self._parent_mapiobj.GetNamesFromIDs(
                    [self.proptag], None, 0)[0]
            except MAPIErrorNoSupport: # TODO user.props()?
                pass
        return self.__lpname

    @property
    def named(self):
        """Is the property a named property."""
        lpname = self._lpname
        return bool(lpname)

    @property
    def guid(self):
        """For a named property, return the GUID."""
        lpname = self._lpname
        if lpname:
            return _benc(lpname.guid)

    @property
    def namespace(self):
        """For a named property, return the readable namespace."""
        lpname = self._lpname
        if lpname:
            return GUID_NAMESPACE.get(lpname.guid)

    @property
    def name(self):
        """For a named property, return the name."""
        lpname = self._lpname
        if lpname:
            return lpname.id

    @property
    def kind(self):
        """For a named property, return the name kind."""
        lpname = self._lpname
        if lpname:
            return lpname.kind

    @property
    def kindname(self):
        """For a named property, return the readable name kind."""
        lpname = self._lpname
        if lpname:
            return 'MNID_STRING' if lpname.kind == MNID_STRING else 'MNID_ID'

    @property
    def value(self):
        """Property value.

        For PT_SYSTIME properties, convert to/from timezone-unaware
        system-local datetimes.
        """
        if self._value is None:
            if self.type_ == PT_SYSTIME: # TODO generalize, property?
                # TODO add global flag to enable tz-aware UTC datetimes
                try:
                    value = datetime.datetime.utcfromtimestamp(self.mapiobj.Value.unixtime)
                    value = _timezone._from_utc(value, _timezone.LOCAL)
                except ValueError: # Y10K: datetime is limited to 4-digit years
                    value = datetime.datetime(9999, 1, 1)
                self._value = value
            else:
                self._value = self.mapiobj.Value
        return self._value

    @value.setter
    def value(self, value):
        self._value = value
        if self.type_ == PT_SYSTIME:
            # Ensure that time is stored as UTC.
            if value.tzinfo is None:
                value = _timezone._to_utc(value, _timezone.LOCAL)
            else:
                value = value.astimezone(_timezone.UTC)
            value = unixtime(calendar.timegm(value.utctimetuple()))
        self._parent_mapiobj.SetProps([SPropValue(self.proptag, value)])
        _utils._save(self._parent_mapiobj)

    @property
    def strid(self):
        """Readable identifier for named/unnamed property."""
        if self.named:
            return '%s:%s' % (self.namespace or self.guid, self.name)
        else:
            return self.idname or hex(self.proptag)

    @property
    def strval(self):
        """String representation of property value."""
        def flatten(v):
            if isinstance(v, list):
                return ','.join(flatten(e) for e in v)
            elif isinstance(v, bool):
                return str(v)
            elif isinstance(v, datetime.datetime):
                return _unicode(v.isoformat(' '))
            elif v is None:
                return ''
            elif (self.type_ in (PT_BINARY, PT_MV_BINARY) or \
                  isinstance(v, bytes)):
                return _benc(v)
            else:
                return _unicode(v)
        return flatten(self.value)

    def __unicode__(self):
        return 'Property(%s)' % self.strid

    # TODO: check if data is binary and convert it to hex
    def __repr__(self):
        return self.__unicode__()
