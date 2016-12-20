%module libfreebusy

%{
#include <mapix.h>
#include <mapidefs.h>
#include <kopano/ECLogger.h>
#include "freebusy.h"
#include "freebusyguid.h"
#include "ECFreeBusySupport.h"
#include "PublishFreeBusy.h"

#include "libfreebusy_conv.h"

%}

%include "std_string.i"
%include "cstring.i"
%include <kopano/typemap.i>



// ICLASS (Class instances of MAPI objects)

// Output
%typemap(in,numinputs=0)    ICLASS *($basetype *temp)
    "temp = NULL; $1 = &temp;";
%typemap(argout)    ICLASS *
{
  %append_output(SWIG_NewPointerObj((void*)*($1), $*1_descriptor, SWIG_SHADOW | SWIG_OWNER));
}

// Classes
%apply ICLASS *{IFreeBusySupport**, IFreeBusyData**, IFreeBusyUpdate**, IEnumFBBlock**}
%apply long {time_t}

enum FBStatus {
        fbFree  = 0,                                    /**< Free */
        fbTentative = fbFree + 1,               /**< Tentative */
        fbBusy  = fbTentative + 1,              /**< Busy */
        fbOutOfOffice   = fbBusy + 1,   /**< Out Of Office */
        fbKopanoAllBusy = 1000                  /**< Internal used */
};

%apply (ULONG, MAPIARRAY) { (ULONG cMax, FBUser *rgfbuser), (ULONG cUsers, FBUser *lpUsers) };
%apply (MAPIARRAY, ULONG) { (FBBlock_1 *lpBlocks, ULONG nBlocks), (FBBlock_1 *lpBlocks, ULONG nBlocks) }
%apply (ULONG, MAPIARRAY) { (LONG celt, FBBlock_1 *pblk), (LONG clt, FBBlock_1 *pblk) }

class IUnknown {
public:
virtual HRESULT QueryInterface(const IID& USE_IID_FOR_OUTPUT, void **OUTPUT_USE_IID) = 0;
};

%extend IUnknown {
~IUnknown() { self->Release(); }
};


%{
swig_type_info *TypeFromIID(REFIID iid)
{
#define TYPECASE(x) if(iid == IID_##x) return SWIGTYPE_p_##x;
    TYPECASE(IFreeBusyUpdate)
    TYPECASE(IEnumFBBlock)
    TYPECASE(IFreeBusySupport)
    TYPECASE(IFreeBusyData)
    return NULL;
}

LPCIID IIDFromType(const char *type)
{
#define IIDCASE(x) if(strstr(type, #x) != NULL) return &IID_##x;
    IIDCASE(IFreeBusyUpdate)
    IIDCASE(IEnumFBBlock)
    IIDCASE(IFreeBusySupport)
    IIDCASE(IFreeBusyData)
    return &IID_IUnknown;
}
%}
  
class IFreeBusyUpdate : public IUnknown {
public:
        virtual HRESULT Reload() = 0;
        virtual HRESULT PublishFreeBusy(FBBlock_1 *lpBlocks, ULONG nBlocks) = 0;
        virtual HRESULT RemoveAppt() = 0;
        virtual HRESULT ResetPublishedFreeBusy() = 0;
        virtual HRESULT ChangeAppt() = 0;
        virtual HRESULT SaveChanges(FILETIME ftBegin, FILETIME ftEnd) = 0;
        virtual HRESULT GetFBTimes() = 0;
        virtual HRESULT Intersect() = 0;
        %extend {
                ~IFreeBusyUpdate() { self->Release(); }
        }
};

class IEnumFBBlock : public IUnknown {

public:

        virtual HRESULT Next(LONG celt, FBBlock_1 *pblk, LONG *pcfetch) = 0;
        virtual HRESULT Skip(LONG celt) = 0;
        virtual HRESULT Reset() = 0;
        virtual HRESULT Clone(IEnumFBBlock **ppclone) = 0;
        virtual HRESULT Restrict(FILETIME ftmStart, FILETIME ftmEnd) = 0;
        %extend {
                ~IEnumFBBlock() { self->Release(); }
        }
};

class IFreeBusyData : public IUnknown {
public:
        virtual HRESULT Reload(void*) = 0;
        virtual HRESULT EnumBlocks(IEnumFBBlock **ppenumfb, FILETIME ftmStart, FILETIME ftmEnd) = 0;
        virtual HRESULT Merge(void *) = 0;
        virtual HRESULT GetDelegateInfo(void *) = 0;
        virtual HRESULT FindFreeBlock(LONG, LONG, LONG, BOOL, LONG, LONG, LONG, FBBlock_1 *) = 0;
        virtual HRESULT InterSect(void *, LONG, void *) = 0;
        virtual HRESULT SetFBRange(LONG rtmStart, LONG rtmEnd) = 0;
        virtual HRESULT NextFBAppt(void *, ULONG, void *, ULONG, void *, void *) = 0;
        virtual HRESULT GetFBPublishRange(LONG *prtmStart, LONG *prtmEnd) = 0;
        %extend {
                ~IFreeBusyData() { self->Release(); }
        }
};

%feature("notabstract") IFreeBusySupport;

class IFreeBusySupport : public IUnknown {
public:
        virtual HRESULT Open(IMAPISession* lpMAPISession, IMsgStore* lpMsgStore, BOOL bStore) = 0;
        virtual HRESULT Close() = 0;
        virtual HRESULT LoadFreeBusyData(ULONG cMax, FBUser *rgfbuser, IFreeBusyData **prgfbdata, HRESULT *phrStatus, ULONG *pcRead) = 0;
        virtual HRESULT LoadFreeBusyUpdate(ULONG cUsers, FBUser *lpUsers, IFreeBusyUpdate **lppFBUpdate, ULONG *lpcFBUpdate, void *lpData4) = 0;
        virtual HRESULT CommitChanges() = 0;
        virtual HRESULT GetDelegateInfo(FBUser, void *) = 0;
        virtual HRESULT SetDelegateInfo(void *) = 0;
        virtual HRESULT AdviseFreeBusy(void *) = 0;
        virtual HRESULT Reload(void *) = 0;
        virtual HRESULT GetFBDetailSupport(void **, BOOL ) = 0;
        virtual HRESULT HrHandleServerSched(void *) = 0;
        virtual HRESULT HrHandleServerSchedAccess() = 0;
        virtual BOOL FShowServerSched(BOOL ) = 0;
        virtual HRESULT HrDeleteServerSched() = 0;
        virtual HRESULT GetFReadOnly(void *) = 0;
        virtual HRESULT SetLocalFB(void *) = 0;
        virtual HRESULT PrepareForSync() = 0;
        virtual HRESULT GetFBPublishMonthRange(void *) = 0;
        virtual HRESULT PublishRangeChanged() = 0;
        virtual HRESULT CleanTombstone() = 0;
        virtual HRESULT GetDelegateInfoEx(FBUser sFBUser, unsigned int *lpulStatus, unsigned int *prtmStart, unsigned int *prtmEnd) = 0;
        virtual HRESULT PushDelegateInfoToWorkspace() = 0;
        %extend {
                IFreeBusySupport() {
                    HRESULT hr = hrSuccess;
                    ECFreeBusySupport*  lpFreeBusySup = NULL;
                       IFreeBusySupport *lpFreeBusySupport = NULL;

                    hr = ECFreeBusySupport::Create(&lpFreeBusySup);
                    if(hr != hrSuccess)
                        goto exit;

                    hr = lpFreeBusySup->QueryInterface(IID_IFreeBusySupport, (void**)&lpFreeBusySupport);
                    if(hr != hrSuccess)
                        goto exit;

                    exit:
                        if(lpFreeBusySup)
                            lpFreeBusySup->Release();

                    return lpFreeBusySupport;
                }

                ~IFreeBusySupport() { self->Release(); }
        }        

};
