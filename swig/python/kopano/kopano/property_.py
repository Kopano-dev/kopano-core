"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import datetime
import sys
import time

from MAPI import (
    PT_ERROR, PT_BINARY, PT_MV_BINARY, PT_UNICODE, PT_LONG, PT_OBJECT,
    PT_BOOLEAN, PT_STRING8, MV_FLAG, PT_SHORT, PT_MV_SHORT, PT_LONGLONG,
    PT_MV_LONG, PT_FLOAT, PT_MV_FLOAT, PT_DOUBLE, PT_MV_DOUBLE, PT_STRING8,
    PT_CURRENCY, PT_MV_CURRENCY, PT_APPTIME, PT_MV_APPTIME, PT_MV_LONGLONG,
    PT_SYSTIME, MAPI_E_NOT_ENOUGH_MEMORY, KEEP_OPEN_READWRITE, PT_MV_STRING8,
    PT_MV_UNICODE, PT_MV_SYSTIME, PT_CLSID, PT_MV_CLSID, MNID_STRING,
    MAPI_E_NOT_FOUND, MNID_ID, KEEP_OPEN_READWRITE, MAPI_UNICODE,
)
from MAPI.Defs import (
    PROP_ID, PROP_TAG, PROP_TYPE, HrGetOneProp,
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
    benc as _benc, repr as _repr, fake_unicode as _unicode, is_int as _is_int,
    is_str as _is_str,
)
from .errors import Error, NotFoundError

if sys.hexversion >= 0x03000000:
    try:
        from . import utils as _utils
    except ImportError:
        _utils = sys.modules[__package__+'.utils']
else:
    import utils as _utils

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

def bestbody(mapiobj): # XXX we may want to use the swigged version in libcommon, once available
    # apparently standardized method for determining original message type!
    tag = PR_NULL
    props = mapiobj.GetProps([PR_BODY_W, PR_HTML, PR_RTF_COMPRESSED, PR_RTF_IN_SYNC], 0)

    if (props[3].ulPropTag != PR_RTF_IN_SYNC): # XXX why..
        return tag

    # MAPI_E_NOT_ENOUGH_MEMORY indicates the property exists, but has to be streamed
    if((props[0].ulPropTag == PR_BODY_W or (PROP_TYPE(props[0].ulPropTag) == PT_ERROR and props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY)) and
       (PROP_TYPE(props[1].ulPropTag) == PT_ERROR and props[1].Value == MAPI_E_NOT_FOUND) and
       (PROP_TYPE(props[2].ulPropTag) == PT_ERROR and props[2].Value == MAPI_E_NOT_FOUND)):
        tag = PR_BODY_W

    # XXX why not just check MAPI_E_NOT_FOUND..?
    elif((props[1].ulPropTag == PR_HTML or (PROP_TYPE(props[1].ulPropTag) == PT_ERROR and props[1].Value == MAPI_E_NOT_ENOUGH_MEMORY)) and
         (PROP_TYPE(props[0].ulPropTag) == PT_ERROR and props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY) and
         (PROP_TYPE(props[2].ulPropTag) == PT_ERROR and props[2].Value == MAPI_E_NOT_ENOUGH_MEMORY) and
         not props[3].Value):
        tag = PR_HTML

    elif((props[2].ulPropTag == PR_RTF_COMPRESSED or (PROP_TYPE(props[2].ulPropTag) == PT_ERROR and props[2].Value == MAPI_E_NOT_ENOUGH_MEMORY)) and
         (PROP_TYPE(props[0].ulPropTag) == PT_ERROR and props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY) and
         (PROP_TYPE(props[1].ulPropTag) == PT_ERROR and props[1].Value == MAPI_E_NOT_FOUND) and
         props[3].Value):
        tag = PR_RTF_COMPRESSED

    return tag

def _split_proptag(proptag): # XXX syntax error exception
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

    if name.isdigit(): # XXX
        name = int(name)
    elif name.startswith('0x'):
        name = int(name, 16)

    if namespace in NAMESPACE_GUID:
        guid = NAMESPACE_GUID[namespace]
    elif namespace in STR_GUID:
        guid = STR_GUID[namespace]

    if proptype is None:
        return None, proptype, namespace, name

    nameid = MAPINAMEID(guid, MNID_ID if isinstance(name, int) else MNID_STRING, name)
    lpname = mapiobj.GetIDsFromNames([nameid], 0)
    proptag = CHANGE_PROP_TYPE(lpname[0], proptype)

    return proptag, proptype, namespace, name

def _proptag_to_name(proptag, store, proptype=False):
    lpname = store.mapiobj.GetNamesFromIDs([proptag], None, 0)[0]
    namespace = GUID_NAMESPACE.get(lpname.guid)
    name = lpname.id
    if proptype:
        type_ = REV_TYPE.get(PROP_TYPE(proptag))
        return u'%s:%s:%s' % (namespace, name, type_)
    return u'%s:%s' % (namespace, name)

def create_prop(self, mapiobj, proptag, value=None, proptype=None): # XXX selfie
    if _is_int(proptag) or \
       (_is_str(proptag) and ':' not in proptag):
        if _is_str(proptag):
            proptag2 = getattr(MAPI.Tags, proptag)
        else:
            proptag2 = proptag
        proptype2 = proptype or PROP_TYPE(proptag)

    else: # named property
        proptag2, proptype2, _, _ = _name_to_proptag(proptag, mapiobj, proptype)

        if proptype2 is None:
            raise Error('Missing type to create named property') # XXX exception too general?

    if value is None:
        if proptype2 in (PT_STRING8, PT_UNICODE):
            value = u''
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
            value = unixtime(time.mktime(value.timetuple()))

    # handle invalid type versus value. For example proptype=PT_UNICODE and value=True
    try:
        mapiobj.SetProps([SPropValue(proptag2, value)])
        mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
    except TypeError:
        raise Error('Could not create property, type and value did not match')

    return prop(self, mapiobj, proptag, proptype=proptype2)

def prop(self, mapiobj, proptag, create=False, value=None, proptype=None): # XXX selfie
    if _is_int(proptag) or \
       (_is_str(proptag) and ':' not in proptag):
        # search for property
        if _is_str(proptag):
            proptag = getattr(MAPI.Tags, proptag)
        try:
            sprop = HrGetOneProp(mapiobj, proptag)
        except MAPIErrorNotEnoughMemory:
            data = _utils.stream(mapiobj, proptag)
            sprop = SPropValue(proptag, data)
        except MAPIErrorNotFound as e:
            # not found, create it?
            if create:
                return create_prop(self, mapiobj, proptag, value=value, proptype=proptype)
            else:
                raise NotFoundError('no such property: %s' % REV_TAG.get(proptag, hex(proptag)))
        return Property(mapiobj, sprop)

    else: # named property
        proptag2, proptype2, namespace, name = _name_to_proptag(proptag, mapiobj, proptype)

        # search for property
        if proptype2:
            try:
                sprop = HrGetOneProp(mapiobj, proptag2) # XXX merge two main branches?
                return Property(mapiobj, sprop)
            except MAPIErrorNotEnoughMemory:
                data = _utils.stream(mapiobj, proptag2)
                sprop = SPropValue(proptag2, data)
                return Property(mapiobj, sprop)
            except MAPIErrorNotFound:
                pass
        else:
            for prop in self.props(namespace=namespace): # XXX sloow, streaming? default pidlid type-db?
                if prop.name == name:
                    return prop

        # not found, create it?
        if create:
            return create_prop(self, mapiobj, proptag, value=value, proptype=proptype)
        else:
            raise NotFoundError('no such property: %s' % proptag)

def props(mapiobj, namespace=None):
    proptags = mapiobj.GetPropList(MAPI_UNICODE)
    sprops = mapiobj.GetProps(proptags, MAPI_UNICODE)
    sprops = [s for s in sprops if not (PROP_TYPE(s.ulPropTag) == PT_ERROR and s.Value == MAPI_E_NOT_FOUND)]
    props = [Property(mapiobj, sprop) for sprop in sprops]

    def prop_key(prop): # sort identically across servers
        if prop.named:
            return (prop.guid, prop.kind, prop.name)
        else:
            return ('', prop.proptag)

    for p in sorted(props, key=prop_key):
        if not namespace or p.namespace == namespace:
            yield p

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
            except MAPIErrorNotFound: # XXX eg normalized subject streaming broken..?
                self._Value = None
        return self._Value

class Property(object):
    """Property class"""

    def __init__(self, parent_mapiobj, mapiobj):
        self._parent_mapiobj = parent_mapiobj
        self.proptag = mapiobj.ulPropTag

        if PROP_TYPE(mapiobj.ulPropTag) == PT_ERROR and mapiobj.Value == MAPI_E_NOT_ENOUGH_MEMORY:
            if PROP_ID(self.proptag) == PROP_ID(PR_BODY_W): # avoid slow guessing
                self.proptag = PR_BODY_W
                mapiobj = SPropDelayedValue(parent_mapiobj, self.proptag)
            elif PROP_ID(self.proptag) in (PROP_ID(PR_RTF_COMPRESSED), PROP_ID(PR_HTML)):
                self.proptag = PROP_TAG(PT_BINARY, PROP_ID(self.proptag))
                mapiobj = SPropDelayedValue(parent_mapiobj, self.proptag)
            else: # XXX possible to use above trick to infer all proptags?
                for proptype in (PT_BINARY, PT_UNICODE): # XXX slow, incomplete?
                    proptag = (mapiobj.ulPropTag & 0xffff0000) | proptype
                    try:
                        HrGetOneProp(parent_mapiobj, proptag) # XXX: Unicode issue?? calls GetProps([proptag], 0)
                        self.proptag = proptag # XXX isn't it strange we can get here
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
        return PROP_ID(self.proptag)

    @property
    def idname(self):
        return REV_TAG.get(self.proptag)

    @property
    def type_(self):
        return PROP_TYPE(self.proptag)

    @property
    def typename(self):
        return REV_TYPE.get(self.type_)

    @property
    def _lpname(self):
        if self.__lpname is None and self.id_ >= 0x8000:
            try:
                self.__lpname = self._parent_mapiobj.GetNamesFromIDs([self.proptag], None, 0)[0]
            except MAPIErrorNoSupport: # XXX user.props()?
                pass
        return self.__lpname

    @property
    def named(self):
        lpname = self._lpname
        return bool(lpname)

    @property
    def guid(self):
        lpname = self._lpname
        if lpname:
            return _benc(lpname.guid)

    @property
    def namespace(self):
        lpname = self._lpname
        if lpname:
            return GUID_NAMESPACE.get(lpname.guid)

    @property
    def name(self):
        lpname = self._lpname
        if lpname:
            return lpname.id

    @property
    def kind(self):
        lpname = self._lpname
        if lpname:
            return lpname.kind

    @property
    def kindname(self):
        lpname = self._lpname
        if lpname:
            return u'MNID_STRING' if lpname.kind == MNID_STRING else u'MNID_ID'

    @property
    def value(self):
        if self._value is None:
            if self.type_ == PT_SYSTIME: # XXX generalize, property?
                #
                # XXX The datetime object is of "naive" type, has local time and
                # no TZ info. :-(
                #
                try:
                    self._value = datetime.datetime.fromtimestamp(self.mapiobj.Value.unixtime)
                except ValueError: # Y10K: datetime is limited to 4-digit years
                    self._value = datetime.datetime(9999, 1, 1)
            else:
                self._value = self.mapiobj.Value
        return self._value

    @value.setter
    def value(self, value):
        self._value = value
        if self.type_ == PT_SYSTIME:
            value = unixtime(time.mktime(value.timetuple()))
        self._parent_mapiobj.SetProps([SPropValue(self.proptag, value)])
        self._parent_mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def strid(self):
        if self.named:
            return u'%s:%s' % (self.namespace, self.name)
        else:
            return self.idname or hex(self.proptag)

    @property
    def strval(self):
        """String representation of value; useful for overviews, debugging."""
        def flatten(v):
            if isinstance(v, list):
                return u','.join(flatten(e) for e in v)
            elif isinstance(v, bool):
                return str(v)
            elif isinstance(v, datetime.datetime):
                return _unicode(v.isoformat(' '))
            elif v is None:
                return u''
            elif self.type_ in (PT_BINARY, PT_MV_BINARY) or isinstance(v, bytes):
                return _benc(v)
            else:
                return _unicode(v)
        return flatten(self.value)

    def __unicode__(self):
        return u'Property(%s)' % self.strid

    # TODO: check if data is binary and convert it to hex
    def __repr__(self):
        return _repr(self)
