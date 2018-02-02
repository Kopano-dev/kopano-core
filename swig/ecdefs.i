%module ecdefs
%include "typemaps.i"

%{
#include <kopano/IECInterfaces.hpp>
%}

class IECChangeAdvisor : public virtual IUnknown {
public:
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
	virtual HRESULT Config(IStream * lpStream, GUID * lpGUID, IECChangeAdviseSink* lpAdviseSink, ULONG ulFlags) = 0;
	virtual HRESULT UpdateState(IStream * lpStream) = 0;
	virtual HRESULT AddKeys(LPENTRYLIST lpEntryList) = 0;
	virtual HRESULT RemoveKeys(LPENTRYLIST lpEntryList) = 0;
    virtual HRESULT IsMonitoringSyncId(ULONG ulSyncId) = 0;
	virtual HRESULT UpdateSyncState(ULONG ulSyncId, ULONG ulChangeId) = 0;
	%extend {
		virtual ~IECChangeAdvisor() { self->Release(); }
	}
};

class IECChangeAdviseSink : public virtual IUnknown {
public:
	virtual ULONG OnNotify(ULONG ulFlags, LPENTRYLIST lpEntryList) = 0;
	%extend {
		virtual ~IECChangeAdviseSink() { self->Release(); }
	}
};

class IECImportContentsChanges : public IExchangeImportContentsChanges {
public:
	virtual HRESULT ConfigForConversionStream(IStream * lpStream, ULONG ulFlags, ULONG cValuesConversion, LPSPropValue lpPropArrayConversion) = 0;
	virtual HRESULT ImportMessageChangeAsAStream(ULONG cValues, LPSPropValue lpProps, ULONG ulFlags, IStream ** lppStream) = 0;
	%extend {
		virtual ~IECImportContentsChanges() { self->Release(); }
	}
};

class IECImportHierarchyChanges : public IExchangeImportHierarchyChanges {
public:
	virtual HRESULT ImportFolderChangeEx(ULONG cValues, LPSPropValue lpProps, BOOL fNew) = 0;
	%extend {
		virtual ~IECImportHierarchyChanges() { self->Release(); }
	}
};

class IECSingleInstance : public virtual IUnknown {
public:
	virtual HRESULT GetSingleInstanceId(ULONG *OUTPUT /*lpcbInstanceID*/, LPENTRYID *OUTPUT /*lppInstanceID*/) = 0;
	virtual HRESULT SetSingleInstanceId(ULONG eid_size, const ENTRYID *eid) = 0;
	%extend {
		virtual ~IECSingleInstance() { self->Release(); }
	}
};

#if SWIGPYTHON

%{
#include <kopano/swig_iunknown.h>
typedef IUnknownImplementor<IECChangeAdviseSink> ECChangeAdviseSink;
typedef IUnknownImplementor<IECImportContentsChanges> ECImportContentsChanges;
typedef IUnknownImplementor<IECImportHierarchyChanges> ECImportHierarchyChanges;
%}

%feature("director") ECChangeAdviseSink;
%feature("nodirector") ECChangeAdviseSink::QueryInterface;
class ECChangeAdviseSink : public IECChangeAdviseSink {
public:
	ECChangeAdviseSink(ULONG cInterfaces, LPCIID lpInterfaces);
	%extend {
		virtual ~ECChangeAdviseSink() { delete self; }
	}
};

%feature("director") ECImportContentsChanges;
%feature("nodirector") ECImportContentsChanges::QueryInterface;
class ECImportContentsChanges : public IECImportContentsChanges {
public:
	ECImportContentsChanges(ULONG cInterfaces, LPCIID lpInterfaces);
	%extend {
		virtual ~ECImportContentsChanges() { delete self; }
	}
};

%feature("director") ECImportHierarchyChanges;
%feature("nodirector") ECImportHierarchyChanges::QueryInterface;
class ECImportHierarchyChanges : public IECImportHierarchyChanges {
public:
	ECImportHierarchyChanges(ULONG cInterfaces, LPCIID lpInterfaces);
	%extend {
		virtual ~ECImportHierarchyChanges() { delete self; }
	}
};

#endif // SWIGPYTHON
