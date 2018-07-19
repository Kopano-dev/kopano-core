%module libfreebusy

%{
#include <mapix.h>
#include <mapidefs.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
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
%apply (MAPIARRAY, ULONG) { (FBBlock_1 const *, ULONG), (FBBlock_1 *, ULONG) }

%typemap(in) (LONG, FBBLOCK)
{
	$1 = PyLong_AsLong($input);
	if (MAPIAllocateBuffer($1 * sizeof(FBBlock_1),
	    reinterpret_cast<void **>(&$2)) != hrSuccess)
		SWIG_fail;
}

%apply (LONG, FBBLOCK) { (LONG celt, FBBlock_1 *pblk), (ULONG celt, FBBlock_1 *pblk) }
%apply (MAPIARRAY, LONG) { (FBBlock_1 *pblk, LONG* pcfetch), (FBBlock_1 *pblk, LONG* pcfetch) }

%typemap(argout) LONG *
{
        %append_output(PyLong_FromLong(*$1));
}

%typemap(argout) ULONG *
{
        %append_output(PyLong_FromUnsignedLong(*$1));
}

%init %{
	InitFreebusy();
%}

class IFreeBusyUpdate {
public:
        virtual HRESULT Reload() = 0;
	virtual HRESULT PublishFreeBusy(const FBBlock_1 *, ULONG nblks) = 0;
        virtual HRESULT RemoveAppt() = 0;
        virtual HRESULT ResetPublishedFreeBusy() = 0;
        virtual HRESULT ChangeAppt() = 0;
        virtual HRESULT SaveChanges(const FILETIME start, const FILETIME end) = 0;
        virtual HRESULT GetFBTimes() = 0;
        virtual HRESULT Intersect() = 0;
        %extend {
                ~IFreeBusyUpdate() { self->Release(); }
        }
};

class IEnumFBBlock {

public:

        virtual HRESULT Next(LONG celt, FBBlock_1 *pblk, LONG *pcfetch) = 0;
        virtual HRESULT Skip(LONG celt) = 0;
        virtual HRESULT Reset() = 0;
        virtual HRESULT Clone(IEnumFBBlock **ppclone) = 0;
        virtual HRESULT Restrict(const FILETIME start, const FILETIME end) = 0;
        %extend {
                ~IEnumFBBlock() { self->Release(); }
        }
};

class IFreeBusyData {
public:
        virtual HRESULT Reload(void*) = 0;
        virtual HRESULT EnumBlocks(IEnumFBBlock **ppenumfb, const FILETIME start, const FILETIME end) = 0;
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

class IFreeBusySupport {
public:
        virtual HRESULT Open(IMAPISession* lpMAPISession, IMsgStore* lpMsgStore, BOOL bStore) = 0;
        virtual HRESULT Close() = 0;
        virtual HRESULT LoadFreeBusyData(ULONG cMax, FBUser *rgfbuser, IFreeBusyData **prgfbdata, HRESULT *phrStatus, ULONG *pcRead) = 0;
        virtual HRESULT LoadFreeBusyUpdate(ULONG cUsers, FBUser *lpUsers, IFreeBusyUpdate **lppFBUpdate, ULONG *lpcFBUpdate, void *lpData4) = 0;
        virtual HRESULT CommitChanges() = 0;
        virtual HRESULT GetDelegateInfo(const FBUser &, void *) = 0;
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
        virtual HRESULT GetDelegateInfoEx(const FBUser &, unsigned int *status, unsigned int *start, unsigned int *end) = 0;
        virtual HRESULT PushDelegateInfoToWorkspace() = 0;
        %extend {
                IFreeBusySupport() {
                    HRESULT hr = hrSuccess;
					KC::object_ptr<ECFreeBusySupport> lpFreeBusySup;
                       IFreeBusySupport *lpFreeBusySupport = NULL;

                    hr = ECFreeBusySupport::Create(&~lpFreeBusySup);
                    if(hr != hrSuccess)
				return NULL;
                    hr = lpFreeBusySup->QueryInterface(IID_IFreeBusySupport, (void**)&lpFreeBusySupport);
                    return lpFreeBusySupport;
                }

                ~IFreeBusySupport() { self->Release(); }
        }

};
