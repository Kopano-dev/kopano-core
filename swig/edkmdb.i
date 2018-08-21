/* SPDX-License-Identifier: AGPL-3.0-only */
%{
// Hack to get around OP_DELETE being an enum in perl\lib\core\opnames.h and EdkMdb.h
namespace EdkMdb {
	#include <edkmdb.h>
}
using namespace EdkMdb;
%}

class IExchangeImportContentsChanges : public virtual IUnknown {
public:
    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* OUTPUT /*lppMAPIError*/) = 0;
	virtual HRESULT Config(IStream * lpStream, ULONG ulFlags) = 0;
	virtual HRESULT UpdateState(IStream * lpStream) = 0;
	virtual HRESULT ImportMessageChange(ULONG cValues, LPSPropValue lpProps, ULONG ulFlags, IMessage ** lppMessage) = 0;
	virtual HRESULT ImportMessageDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList) = 0;
	virtual HRESULT ImportPerUserReadStateChange(ULONG cElements, LPREADSTATE lpReadState) = 0;
	virtual HRESULT ImportMessageMove(ULONG cbSourceKeySrcFolder, BYTE * pbSourceKeySrcFolder, ULONG cbSourceKeySrcMessage, BYTE * pbSourceKeySrcMessage, ULONG cbPCLMessage, BYTE * pbPCLMessage, ULONG cbSourceKeyDestMessage, BYTE * pbSourceKeyDestMessage, ULONG cbChangeNumDestMessage, BYTE * pbChangeNumDestMessage) = 0;

	%extend {
		virtual ~IExchangeImportContentsChanges() { self->Release(); }
	}
};

class IExchangeImportHierarchyChanges : public virtual IUnknown {
public:
    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* OUTPUT /*lppMAPIError*/) = 0;
	virtual HRESULT Config(IStream * lpStream, ULONG ulFlags) = 0;
	virtual HRESULT UpdateState(IStream * lpStream) = 0;
	virtual HRESULT ImportFolderChange(ULONG cValues, LPSPropValue lpProps) = 0;
	virtual HRESULT ImportFolderDeletion(ULONG ulFlags, LPENTRYLIST	lpSrcEntryList) = 0;
	%extend {
		virtual ~IExchangeImportHierarchyChanges() { self->Release(); }
	}
};

class IExchangeExportChanges : public virtual IUnknown {
public:
    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* OUTPUT /*lppMAPIError*/) = 0;
	virtual HRESULT Config(IStream * lpStream, ULONG ulFlags, IUnknown *lpUnk, LPSRestriction lpRestriction, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize) = 0;
	virtual HRESULT Synchronize(ULONG* lpulSteps, ULONG *INOUT /*lpulProgress*/) = 0;
	virtual HRESULT UpdateState(IStream * lpStream) = 0;
	%extend {
		virtual ~IExchangeExportChanges() { self->Release(); }
	}
};

class IECExportAddressbookChanges : public IUnknown {
public:
	virtual HRESULT Config(IStream * lpStream, ULONG ulFlags, IECImportAddressbookChanges *lpUnk);
	virtual HRESULT Synchronize(ULONG* lpulSteps, ULONG *INOUT /*lpulProgress*/) = 0;
	virtual HRESULT UpdateState(IStream * lpStream) = 0;

	%extend {
		~IECExportAddressbookChanges() { self->Release(); };
	}
};

class IExchangeManageStore : public virtual IUnknown {
public:
	virtual HRESULT CreateStoreEntryID(LPTSTR lpszMsgStoreDN, LPTSTR lpszMailboxDN, ULONG ulFlags, ULONG *OUTPUT,	LPENTRYID *OUTPUT) = 0;
	virtual HRESULT EntryIDFromSourceKey(ULONG cFolderKeySize, BYTE *lpFolderSourceKey,	ULONG cMessageKeySize, BYTE *lpMessageSourceKey, ULONG *OUTPUT, LPENTRYID *OUTPUT) = 0;
	virtual HRESULT GetRights(ULONG cbUserEntryID, LPENTRYID lpUserEntryID,	ULONG cbEntryID, LPENTRYID lpEntryID, ULONG *lpulRights) = 0;
	virtual HRESULT GetMailboxTable(LPTSTR lpszServerName, IMAPITable **OUTPUT /*lppTable*/, ULONG ulFlags) = 0;
	virtual HRESULT GetPublicFolderTable(LPTSTR lpszServerName, IMAPITable **OUTPUT /*lppTable*/, ULONG	ulFlags) = 0;
	%extend {
		virtual ~IExchangeManageStore() { self->Release(); }
	}
};

class IExchangeModifyTable : public virtual IUnknown {
public:
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* OUTPUT /*lppMAPIError*/) = 0;
	virtual HRESULT GetTable(ULONG ulFlags, IMAPITable ** OUTPUT /*lppTable*/) = 0;
	virtual HRESULT ModifyTable(ULONG ulFlags, LPROWLIST lpMods) = 0;
	%extend {
		virtual ~IExchangeModifyTable() { self->Release(); }
	}
};

#if SWIGPYTHON

%{
#include <kopano/swig_iunknown.h>
typedef IUnknownImplementor<IExchangeImportContentsChanges> ExchangeImportContentsChanges;
typedef IUnknownImplementor<IExchangeImportHierarchyChanges> ExchangeImportHierarchyChanges;
typedef IUnknownImplementor<IExchangeExportChanges> ExchangeExportChanges;
%}

%feature("director") ExchangeImportContentsChanges;
%feature("nodirector") ExchangeImportContentsChanges::QueryInterface;
class ExchangeImportContentsChanges : public IExchangeImportContentsChanges {
public:
	ExchangeImportContentsChanges(ULONG cInterfaces, LPCIID lpInterfaces);
	%extend {
		virtual ~ExchangeImportContentsChanges() { delete self; }
	}
};

%feature("director") ExchangeImportHierarchyChanges;
%feature("nodirector") ExchangeImportHierarchyChanges::QueryInterface;
class ExchangeImportHierarchyChanges : public IExchangeImportHierarchyChanges {
public:
	ExchangeImportHierarchyChanges(ULONG cInterfaces, LPCIID lpInterfaces);
	%extend {
		virtual ~ExchangeImportHierarchyChanges() { delete self; }
	}
};

%feature("director") ExchangeExportChanges;
%feature("nodirector") ExchangeExportChanges::QueryInterface;
class ExchangeExportChanges : public IExchangeExportChanges {
public:
	ExchangeExportChanges(ULONG cInterfaces, LPCIID lpInterfaces);
	%extend {
		virtual ~ExchangeExportChanges() { delete self; }
	}
};

#endif // SWIGPYTHON


#define SYNC_UNICODE                            0x01
#define SYNC_NO_DELETIONS                       0x02
#define SYNC_NO_SOFT_DELETIONS          0x04
#define SYNC_READ_STATE                         0x08
#define SYNC_ASSOCIATED                         0x10
#define SYNC_NORMAL                                     0x20
#define SYNC_NO_CONFLICTS                       0x40
#define SYNC_ONLY_SPECIFIED_PROPS       0x80
#define SYNC_NO_FOREIGN_KEYS            0x100
#define SYNC_LIMITED_IMESSAGE           0x200
#define SYNC_CATCHUP                            0x400
#define SYNC_NEW_MESSAGE                        0x800
#define SYNC_MSG_SELECTIVE                      0x1000
#define SYNC_BEST_BODY                          0x2000
#define SYNC_IGNORE_SPECIFIED_ON_ASSOCIATED 0x4000
#define SYNC_PROGRESS_MODE                      0x8000
#define SYNC_FXRECOVERMODE                      0x10000
#define SYNC_DEFER_CONFIG                       0x20000
#define SYNC_FORCE_UNICODE                      0x40000

#define DELETE_HARD_DELETE						0x10

#define ROWLIST_REPLACE		1
#define ROW_ADD				1
#define ROW_MODIFY			2
#define ROW_REMOVE			4
#define ROW_EMPTY			5


%constant unsigned int SYNC_E_UNKNOWN_FLAGS			= 0x80040106;
%constant unsigned int SYNC_E_INVALID_PARAMETER		= 0x80070057;
%constant unsigned int SYNC_E_ERROR					= 0x80004005;
%constant unsigned int SYNC_E_OBJECT_DELETED		= 0x80040800;
%constant unsigned int SYNC_E_IGNORE				= 0x80040801;
%constant unsigned int SYNC_E_CONFLICT				= 0x80040802;
%constant unsigned int SYNC_E_NO_PARENT				= 0x80040803;
%constant unsigned int SYNC_E_INCEST				= 0x80040804;
%constant unsigned int SYNC_E_UNSYNCHRONIZED		= 0x80040805;

%constant unsigned int SYNC_W_PROGRESS				= 0x00040820;
%constant unsigned int SYNC_W_CLIENT_CHANGE_NEWER	= 0x00040821;
