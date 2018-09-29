# -*- indent-tabs-mode: nil -*-
# SPDX-License-Identifier: AGPL-3.0-only

import inspect
import sys

import MAPICore
from MAPI.Defs import *

def _convert(s):
    if sys.hexversion >= 0x03000000 and isinstance(s, bytes):
        return s.decode('ascii')
    else:
        return s

class MAPIStruct:
    def __init__(self): pass
    def __eq__(self, other):
        if other is None:
            return False
        return self.__dict__ == other.__dict__
    def __repr__(self):
        return repr(self.__dict__)
    def __setstate__(self, d):
        # XXX pickle with python2, unpickle with python3 (encoding='bytes')
        for k, v in d.items():
            setattr(self, _convert(k), v)

class SPropValue(MAPIStruct):
    def __init__(self, ulPropTag, Value):
        self.ulPropTag = ulPropTag
        self.Value = Value
    def __repr__(self):
        return "SPropValue(0x%08X, %s)" % (self.ulPropTag, repr(self.Value))
    def __cmp__(self, other):
        if other is None:
            return 1
        if self.ulPropTag != other.ulPropTag:
            return cmp(self.ulPropTag,other.ulPropTag)
        return cmp(self.Value,other.Value)
    def __hash__(self):
        return hash(self.ulPropTag) + hash(self.Value)

class SSort(MAPIStruct):
    def __init__(self, ulPropTag, ulOrder):
        self.ulPropTag = ulPropTag
        self.ulOrder = ulOrder
        
    def __repr__(self):
        sorts = [ 'TABLE_SORT_ASCEND', 'TABLE_SORT_DESCEND' ]
        return "SSort(%#x, %s)" % (self.ulPropTag, sorts[self.ulOrder])

class SSortOrderSet(MAPIStruct):
    def __init__(self, sorts, cCategories, cExpanded):
        self.aSort = sorts
        self.cCategories = cCategories
        self.cExpanded = cExpanded
      
    def __repr__(self):
        return "SSortOrderSet(%r, %r, %r)" % (self.aSort, self.cCategories, self.cExpanded)

class MAPINAMEID(MAPIStruct):
    def __init__(self, guid, kind, id):
        self.guid = guid
        self.kind = kind
        self.id = id

    def __hash__(self):
        return (self.guid, self.kind, self.id).__hash__()
        
    def __repr__(self):
        if (self.kind == MAPICore.MNID_ID):
            return "MAPINAMEID(%r, MNID_ID, %r)" % (self.guid, self.id)
        else:
            return "MAPINAMEID(%r, MNID_STRING, %r)" % (self.guid, self.id)

class SPropProblem(MAPIStruct):
    def __init__(self, index, tag, scode):
        self.ulIndex = index
        self.ulPropTag = tag
        self.scode = scode

class SAndRestriction(MAPIStruct):
    def __init__(self, sub):
        self.rt = MAPICore.RES_AND
        self.lpRes = sub
    def __repr__(self):
        return 'SAndRestriction(%r)' % (self.lpRes)
            
class SOrRestriction(MAPIStruct):
    def __init__(self, sub):
        self.rt = MAPICore.RES_OR
        self.lpRes = sub
    def __repr__(self):
        return 'SOrRestriction(%r)' % (self.lpRes)
        
class SNotRestriction(MAPIStruct):
    def __init__(self, sub):
        self.rt = MAPICore.RES_NOT
        self.lpRes = sub
    def __repr__(self):
        return 'SNotRestriction(%r)' % (self.lpRes)
        
class SContentRestriction(MAPIStruct):
    def __init__(self, fuzzy, proptag, prop):
        self.rt = MAPICore.RES_CONTENT
        self.ulFuzzyLevel = fuzzy
        self.ulPropTag = proptag
        self.lpProp = prop
    def __repr__(self):
        return 'SContentRestriction(%r,%#x,%r)' % (self.ulFuzzyLevel, self.ulPropTag, self.lpProp)
        
class SBitMaskRestriction(MAPIStruct):
    def __init__(self, relBMR, ulPropTag, ulMask):
        self.rt = MAPICore.RES_BITMASK
        self.relBMR = relBMR
        self.ulPropTag = ulPropTag
        self.ulMask = ulMask
    def __repr__(self):
        return 'SBitMaskRestriction(%r,%#x,%r)' % (self.relBMR, self.ulPropTag, self.ulMask)

class SPropertyRestriction(MAPIStruct):
    def __init__(self, relop, ulPropTag, prop):
        self.rt = MAPICore.RES_PROPERTY
        self.ulPropTag = ulPropTag
        self.relop = relop
        self.lpProp = prop
    def __repr__(self):
        return 'SPropertyRestriction(%r,%#x,%r)' %(self.relop, self.ulPropTag, self.lpProp)

class SComparePropsRestriction(MAPIStruct):
    def __init__(self, relop, ulPropTag1, ulPropTag2):
        self.rt = MAPICore.RES_COMPAREPROPS
        self.relop = relop
        self.ulPropTag1 = ulPropTag1
        self.ulPropTag2 = ulPropTag2
    def __repr__(self):
        return 'SComparePropsRestriction(%r,%#x,%#x)' % (self.relop, self.ulPropTag1, self.ulPropTag2)
        
class SSizeRestriction(MAPIStruct):
    def __init__(self, relop, ulPropTag, cb):
        self.rt = MAPICore.RES_SIZE
        self.relop = relop
        self.ulPropTag = ulPropTag
        self.cb = cb
    def __repr__(self):
        return 'SSizeRestriction(%r,%#x,%r)' % (self.relop, self.ulPropTag, self.cb)
        
class SExistRestriction(MAPIStruct):
    def __init__(self, proptag):
        self.rt = MAPICore.RES_EXIST
        self.ulPropTag = proptag
    def __repr__(self):
        return 'SExistRestriction(%#x)' % (self.ulPropTag)
        
class SSubRestriction(MAPIStruct):
    def __init__(self, ulSubObject, res):
        self.rt = MAPICore.RES_SUBRESTRICTION
        self.ulSubObject = ulSubObject
        self.lpRes = res
    def __repr__(self):
        return 'SSubRestriction(%#x,%r)' % (self.ulSubObject, self.lpRes)
        
class SCommentRestriction(MAPIStruct):
    def __init__(self, res, prop):
        self.rt = MAPICore.RES_COMMENT
        self.lpRes = res
        self.lpProp = prop
    def __repr__(self):
        return 'SCommentRestriction(%r,%r)' %(self.lpRes, self.lpProp)

class actMoveCopy(MAPIStruct):
    def __init__(self, store, folder):
        self.StoreEntryId = store
        self.FldEntryId = folder
    def __repr__(self):
        return 'actMoveCopy(%r,%r)' % (self.StoreEntryId, self.FldEntryId)

class actReply(MAPIStruct):
    def __init__(self, entryid, guid):
        self.EntryId = entryid
        self.guidReplyTemplate = guid
    def __repr__(self):
        return 'actReply(%r,%r)' % (self.EntryId, self.guidReplyTemplate)

class actDeferAction(MAPIStruct):
    def __init__(self, deferMsg):
        self.data = deferMsg
    def __repr__(self):
        return 'actDeferMsg(%r)' % (self.data)

class actBounce(MAPIStruct):
    def __init__(self, code):
        self.scBounceCode = code
    def __repr__(self):
        return 'actBounce(%r)' % (self.scBounceCode)

class actFwdDelegate(MAPIStruct):
    def __init__(self, adrlist):
        self.lpadrlist = adrlist
    def __repr__(self):
        return 'actFwdDelegate(%r)' % (self.lpadrlist)

class actTag(MAPIStruct):
    def __init__(self, tag):
        self.propTag = tag
    def __repr__(self):
        return 'actTag(%r)' % (self.propTag)

class ACTION(MAPIStruct):
    def __init__(self, acttype, flavor, res, proptagarray, flags, actobj):
        self.acttype = acttype
        self.ulActionFlavor = flavor
        self.lpRes = res
        self.lpPropTagArray = proptagarray
        self.ulFlags = flags
        # any of the above action
        self.actobj = actobj
    def __repr__(self):
        return 'ACTION(%r,%r,%r,%r,%#x,%r)' % (self.acttype,self.ulActionFlavor,self.lpRes,self.lpPropTagArray,self.ulFlags,self.actobj)

class ACTIONS(MAPIStruct):
    def __init__(self, version, actions):
        self.ulVersion = version
        self.lpAction = actions
    def __repr__(self):
        return 'ACTIONS(%r,%r)' % (self.ulVersion, self.lpAction)
    
# pylint (let's replace the horror which comes after this with a simple list)
MAPIErrorNotFound = None
MAPIErrorInterfaceNotSupported = None
MAPIErrorNotEnoughMemory = None
MAPIErrorNoSupport = None
MAPIErrorInvalidParameter = None
MAPIErrorNoAccess = None
MAPIErrorInvalidEntryid = None
MAPIErrorUnknownEntryid = None
MAPIErrorNoSupport = None
MAPIErrorNetworkError = None
MAPIErrorDiskError = None
MAPIErrorLogonFailed = None

class MAPIError(Exception):
    _errormap = {}
    
    @staticmethod
    def _initialize_errors():
        error_prefix = 'MAPI_E_'
        for name, value in inspect.getmembers(sys.modules['MAPICore']):
            if name.startswith(error_prefix):
                clsname = 'MAPIError' + ''.join(s.capitalize() for s in name[len(error_prefix):].split('_'))
                def construct_class(hr, descr):
                    if issubclass(MAPIError, object):
                        return type(clsname, (MAPIError,), {'__init__': lambda self: MAPIError.__init__(self, hr, descr)})
                    else:
                        class MAPIErrorDynamic(MAPIError):
                            def __init__(self):
                                MAPIError.__init__(self, hr, descr)
                        return MAPIErrorDynamic
                t = construct_class(value, name)
                setattr(sys.modules[__name__], clsname, t)
                MAPIError._errormap[value] = t
    
    @staticmethod
    def _get_type(hr):
        '''Returns the type of the MAPIError subclass that represents the given error code.'''
        return MAPIError._errormap.get(hr)
    
    @staticmethod
    def from_hresult(hr):
        '''Use MAPIError.from_hresult(hr) to create a MAPIError subclass based on the error
        code. If no subclass is defined for the error code, a MAPIError instance is
        returned.
        '''
        t = MAPIError._get_type(hr)
        if t:
            return t()
        return MAPIError(hr)
    
    def __init__(self, hr, descr=None):
        self.hr = hr
        self.descr = descr
        
    def __repr__(self):
        return "MAPI error %X (%s)" % (self.hr, self.descr)

    def __str__(self):
        return self.__repr__()

class READSTATE(MAPIStruct):
    def __init__(self, SourceKey, ulFlags):
        self.SourceKey = SourceKey
        self.ulFlags = ulFlags

class MVPROPMAP(MAPIStruct):
    def __init__(self, ulPropId, Values):
        self.ulPropId = ulPropId
        self.Values = Values

    def __str__(self):
        return 'MVPROPMAP(%s)' % (self.ulPropId)

class ECUSER(MAPIStruct):
    def __init__(self, Username, Password, Email, FullName, Servername = None, Class = 0x10001, IsAdmin = False, IsHidden = False, Capacity = 0, UserID = None, MVPropMap = None):
        self.Username = Username
        self.Password = Password
        self.Email = Email
        self.FullName = FullName
        self.Servername = Servername
        self.Class = Class
        self.IsAdmin = IsAdmin
        self.IsHidden = IsHidden
        self.Capacity = Capacity
        self.UserID = UserID
        self.MVPropMap = MVPropMap
    def __str__(self):
        return 'ECUSER(%s,%s,%s)' % (self.Username, self.Email, self.FullName)

# @todo propmap?
class ECGROUP(MAPIStruct):
    def __init__(self, Groupname, Fullname, Email, IsHidden = False, GroupID = None, MVPropMap = None):
        self.Groupname = Groupname
        self.Fullname = Fullname
        self.Email = Email
        self.IsHidden = IsHidden
        self.GroupID = GroupID
        self.MVPropMap = MVPropMap

# @todo propmap?
class ECCOMPANY(MAPIStruct):
    def __init__(self, Companyname, Servername, IsHidden = False, CompanyID = None, MVPropMap = None, AdministratorID = None):
        self.Companyname = Companyname
        self.Servername = Servername
        self.IsHidden = IsHidden
        self.CompanyID = CompanyID
        self.MVPropMap = MVPropMap
        self.AdministratorID = AdministratorID

class ECSERVER(MAPIStruct):
    def __init__(self, Name, FilePath, HttpPath, SslPath, PreferedPath, Flags):
        self.Name = Name
        self.FilePath = FilePath
        self.HttpPath = HttpPath
        self.SslPath = SslPath
        self.PreferedPath = PreferedPath
        self.Flags = Flags

class ERROR_NOTIFICATION(MAPIStruct):
    def __init__(self, lpEntryID, scode, ulFlags, lpMAPIError):
        self.lpEntryID = lpEntryID
        self.scode = scode
        self.ulFlags = ulFlags
        self.lpMAPIError = lpMAPIError
        
class NEWMAIL_NOTIFICATION(MAPIStruct):
    def __init__(self, lpEntryID, lpParentID, ulFlags, lpszMessageClass, ulMessageFlags):
        self.lpEntryID = lpEntryID
        self.lpParentID = lpParentID
        self.ulFlags = ulFlags
        self.lpszMessageClass = lpszMessageClass
        self.ulMessageFlags = ulMessageFlags
        
class OBJECT_NOTIFICATION(MAPIStruct):
    def __init__(self, ulEventType, lpEntryID, ulObjType, lpParentID, lpOldID, lpOldParentID, lpPropTagArray):
        self.ulEventType = ulEventType
        self.lpEntryID = lpEntryID
        self.ulObjType = ulObjType
        self.lpParentID = lpParentID
        self.lpOldID = lpOldID
        self.lpOldParentID = lpOldParentID
        self.lpPropTagArray = lpPropTagArray
        
class TABLE_NOTIFICATION(MAPIStruct):
    def __init__(self, ulTableEvent, hResult, propIndex, propPrior, row):
        self.ulTableEvent = ulTableEvent
        self.hResult = hResult
        self.propIndex = propIndex
        self.propPrior = propPrior
        self.row = row
     
class ROWENTRY(MAPIStruct):
    def __init__(self, ulRowFlags, rgPropVals):
        self.ulRowFlags = ulRowFlags
        self.rgPropVals = rgPropVals

class ECQUOTA(MAPIStruct):
    def __init__(self, bUseDefaultQuota, bIsUserDefaultQuota, llWarnSize, llSoftSize, llHardSize):
        self.bUseDefaultQuota = bUseDefaultQuota
        self.bIsUserDefaultQuota = bIsUserDefaultQuota
        self.llWarnSize = llWarnSize
        self.llSoftSize = llSoftSize
        self.llHardSize = llHardSize

class ECQUOTASTATUS(MAPIStruct):
    def __init__(self, StoreSize, QuotaStatus):
        self.StoreSize = StoreSize
        self.QuotaStatus = QuotaStatus

class STATSTG(MAPIStruct):
    def __init__(self, cbSize):
        self.cbSize = cbSize

class SYSTEMTIME(MAPIStruct):
    def __init__(self, wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds):
        self.wYear = wYear
        self.wMonth = wMonth
        self.wDayOfWeek = wDayOfWeek
        self.wDay = wDay
        self.wHour = wHour
        self.wMinute = wMinute
        self.wSecond = wSecond
        self.wMilliseconds = wMilliseconds

class FreeBusyBlock(MAPIStruct):
    def __init__(self, start, end, status):
        self.start = start
        self.end = end
        self.status = status


MAPIError._initialize_errors()
