"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import codecs
import datetime
import sys
import time

from MAPI import (
    PT_ERROR, PT_BINARY, PT_MV_BINARY, PT_UNICODE, PT_LONG,
    PT_STRING8, MV_FLAG,
    PT_SYSTIME, MAPI_E_NOT_ENOUGH_MEMORY, KEEP_OPEN_READWRITE,
    MNID_STRING, MAPI_E_NOT_FOUND, MNID_ID, KEEP_OPEN_READWRITE,
    MAPI_UNICODE,
)
from MAPI.Defs import (
    PROP_ID, PROP_TAG, PROP_TYPE, HrGetOneProp, bin2hex,
    CHANGE_PROP_TYPE,
)
from MAPI.Struct import (
    SPropValue, MAPIErrorNotFound, MAPIErrorNoSupport,
    MAPIErrorNotEnoughMemory
)
from MAPI.Tags import (
    PR_BODY_W, PR_HTML, PR_RTF_COMPRESSED, PR_RTF_IN_SYNC, PR_NULL
)
from MAPI.Time import unixtime

from .defs import (
    REV_TAG, REV_TYPE, GUID_NAMESPACE, MAPINAMEID, NAMESPACE_GUID
)
from .compat import (
    repr as _repr, fake_unicode as _unicode, is_int as _is_int, hex as _hex
)
from .errors import Error

if sys.hexversion >= 0x03000000:
    from . import utils as _utils
else:
    import utils as _utils

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

def create_prop(self, mapiobj, proptag, value, proptype=None):
    if _is_int(proptag):
        if PROP_TYPE(proptag) == PT_SYSTIME:
            value = unixtime(time.mktime(value.timetuple()))
        # handle invalid type versus value. For example proptype=PT_UNICODE and value=True
        try:
            mapiobj.SetProps([SPropValue(proptag, value)])
        except TypeError:
            raise Error('Could not create property, type and value did not match')
    else: # named prop
        # XXX: code duplication from prop()
        namespace, name = proptag.split(':') # XXX syntax
        if name.isdigit(): # XXX
            name = int(name)

        if proptype == PT_SYSTIME:
            value = unixtime(time.mktime(value.timetuple()))
        if not proptype:
            raise Error('Missing type to create named Property') # XXX exception too general?

        nameid = MAPINAMEID(NAMESPACE_GUID.get(namespace), MNID_ID if isinstance(name, int) else MNID_STRING, name)
        lpname = mapiobj.GetIDsFromNames([nameid], 0)
        proptag = CHANGE_PROP_TYPE(lpname[0], proptype)
        # handle invalid type versus value. For example proptype=PT_UNICODE and value=True
        try:
            mapiobj.SetProps([SPropValue(proptag, value)])
        except TypeError:
            raise Error('Could not create property, type and value did not match')

    return prop(self, mapiobj, proptag)

def prop(self, mapiobj, proptag, create=False):
    if _is_int(proptag):
        try:
            sprop = HrGetOneProp(mapiobj, proptag)
        except MAPIErrorNotEnoughMemory:
            data = _utils.stream(mapiobj, proptag)
            sprop = SPropValue(proptag, data)
        except MAPIErrorNotFound as e:
            if create:
                if PROP_TYPE(proptag) in (PT_STRING8, PT_UNICODE):
                    mapiobj.SetProps([SPropValue(proptag, u'')])
                elif PROP_TYPE(proptag) == PT_BINARY:
                    mapiobj.SetProps([SPropValue(proptag, b'')])
                elif PROP_TYPE(proptag) & MV_FLAG:
                    mapiobj.SetProps([SPropValue(proptag, [])])
                elif PROP_TYPE(proptag) == PT_SYSTIME:
                    mapiobj.SetProps([SPropValue(proptag, unixtime(0))])
                else:
                    mapiobj.SetProps([SPropValue(proptag, 0)])
                mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
                sprop = HrGetOneProp(mapiobj, proptag)
            else:
                raise e
        return Property(mapiobj, sprop)

    else: # XXX create=True, merge with create_prop
        namespace, name = proptag.split(':') # XXX syntax
        if name.isdigit(): # XXX
            name = int(name)

        for prop in self.props(namespace=namespace): # XXX sloow, streaming
            if prop.name == name:
                return prop
        raise MAPIErrorNotFound()

def props(mapiobj, namespace=None):
    proptags = mapiobj.GetPropList(MAPI_UNICODE)
    sprops = mapiobj.GetProps(proptags, MAPI_UNICODE)
    props = [Property(mapiobj, sprop) for sprop in sprops]
    for p in sorted(props):
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

    def __init__(self, parent_mapiobj, mapiobj): # XXX rethink attributes, names.. add guidname..?
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

        self.id_ = self.proptag >> 16
        self.mapiobj = mapiobj
        self._value = None

        self.idname = REV_TAG.get(self.proptag) # XXX slow, often unused: make into properties?
        self.type_ = PROP_TYPE(self.proptag)
        self.typename = REV_TYPE.get(self.type_)
        self.named = False
        self.kind = None
        self.kindname = None
        self.guid = None
        self.name = None
        self.namespace = None

        if self.id_ >= 0x8000: # possible named prop
            try:
                lpname = self._parent_mapiobj.GetNamesFromIDs([self.proptag], None, 0)[0]
                if lpname:
                    self.guid = bin2hex(lpname.guid)
                    self.namespace = GUID_NAMESPACE.get(lpname.guid)
                    self.name = lpname.id
                    self.kind = lpname.kind
                    self.kindname = 'MNID_STRING' if lpname.kind == MNID_STRING else 'MNID_ID'
                    self.named = True
            except MAPIErrorNoSupport: # XXX user.props()?
                pass

    def get_value(self):
        if self._value is None:
            if self.type_ == PT_SYSTIME: # XXX generalize, property?
                #
                # The datetime object is of "naive" type, has local time and
                # no TZ info. :-(
                #
                try:
                    self._value = datetime.datetime.fromtimestamp(self.mapiobj.Value.unixtime)
                except ValueError: # Y10K: datetime is limited to 4-digit years
                    self._value = datetime.datetime(9999, 1, 1)
            else:
                self._value = self.mapiobj.Value
        return self._value

    def set_value(self, value):
        self._value = value
        if self.type_ == PT_SYSTIME:
            # Timezones are handled.
            value = unixtime(time.mktime(value.timetuple()))
        self._parent_mapiobj.SetProps([SPropValue(self.proptag, value)])
        self._parent_mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
    value = property(get_value, set_value)

    @property
    def strid(self):
        if self.named:
            return u'%s:%s' % (self.namespace, self.name)
        else:
            return self.idname or u'' # FIXME: should never be None

    @property
    def strval(self):
        """ String representation of value; useful for overviews, debugging """

        def flatten(v):
            if isinstance(v, list):
                return u','.join(flatten(e) for e in v)
            elif isinstance(v, bool):
                return u'01'[v]
            elif v is None:
                return ''
            elif self.type_ in (PT_BINARY, PT_MV_BINARY):
                return _hex(v)
            else:
                return _unicode(v)
        return flatten(self.value)

    def __lt__(self, prop):
        return self.proptag < prop.proptag

    def __unicode__(self):
        return u'Property(%s)' % self.strid

    # TODO: check if data is binary and convert it to hex
    def __repr__(self):
        return _repr(self)
