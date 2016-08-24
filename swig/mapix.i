%module mapix
%include "typemaps.i"

%{
#include <mapix.h>
%}

class IMAPISession : public IUnknown {
public:
    //    virtual ~IMAPISession() = 0;

    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, MAPIERROR** OUTPUT /*lppMAPIError*/) = 0;
    virtual HRESULT GetMsgStoresTable(ULONG ulFlags, IMAPITable ** OUTPUT /*lppTable*/) = 0;
    virtual HRESULT OpenMsgStore(ULONG ulUIParam, ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, IMsgStore ** OUTPUT /*lppMDB*/) = 0;
    virtual HRESULT OpenAddressBook(ULONG ulUIParam, LPCIID lpInterface, ULONG ulFlags, IAddrBook ** OUTPUT /*lppAdrBook*/) = 0;
    virtual HRESULT OpenProfileSection(LPMAPIUID lpUID, LPCIID lpInterface, ULONG ulFlags, IProfSect ** OUTPUT /*lppProfSect*/) = 0;
    virtual HRESULT GetStatusTable(ULONG ulFlags, IMAPITable ** OUTPUT /*lppTable*/) = 0;
    virtual HRESULT OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG* OUTPUT /*lpulObjType*/,
		      IUnknown ** OUTPUT /*lppUnk*/) = 0;
    virtual HRESULT CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags,
			    ULONG* OUTPUT /*lpulResult*/) = 0;
    virtual HRESULT Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG* OUTPUT /*lpulConnection*/) = 0;
    virtual HRESULT Unadvise(ULONG ulConnection) = 0;
    virtual HRESULT MessageOptions(ULONG ulUIParam, ULONG ulFlags, LPTSTR lpszAdrType, LPMESSAGE lpMessage) = 0;
    virtual HRESULT QueryDefaultMessageOpt(LPTSTR lpszAdrType, ULONG ulFlags, ULONG* lpcValues, LPSPropValue* lppOptions) = 0;
    virtual HRESULT EnumAdrTypes(ULONG ulFlags, ULONG* OUTPUT /*lpcAdrTypes*/, LPTSTR** OUTPUT /*lpppszAdrTypes*/) = 0;
    virtual HRESULT QueryIdentity(ULONG* OUTPUT /*lpcbEntryID*/, LPENTRYID* OUTPUT /*lppEntryID*/) = 0;
    virtual HRESULT Logoff(ULONG ulUIParam, ULONG ulFlags, ULONG ulReserved) = 0;
    virtual HRESULT SetDefaultStore(ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID) = 0;
    virtual HRESULT AdminServices(ULONG ulFlags, IMsgServiceAdmin ** OUTPUT /*lppServiceAdmin*/) = 0;
    virtual HRESULT ShowForm(ULONG ulUIParam, LPMDB lpMsgStore, LPMAPIFOLDER lpParentFolder, LPCIID lpInterface, ULONG ulMessageToken, LPMESSAGE lpMessageSent, ULONG ulFlags, ULONG ulMessageStatus, ULONG ulMessageFlags, ULONG ulAccess,  LPSTR lpszMessageClass) = 0;
    virtual HRESULT PrepareForm(LPCIID lpInterface, LPMESSAGE lpMessage, ULONG* OUTPUT /*lpulMessageToken*/) = 0;
	%extend {
		~IMAPISession() { self->Release(); }
	}
};

/*
 * IAddrBook Interface
*/

class IAddrBook : public IMAPIProp {
public:
    //    virtual ~IAddrBook() = 0;

    virtual HRESULT OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG* OUTPUT /*lpulObjType*/,
		      IUnknown** OUTPUT /*lppUnk*/) = 0;
    virtual HRESULT CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags,
			    ULONG* OUTPUT /*lpulResult*/) = 0;
    virtual HRESULT Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG* OUTPUT /*lpulConnection*/) = 0;
    virtual HRESULT Unadvise(ULONG ulConnection) = 0;
    virtual HRESULT CreateOneOff(LPTSTR lpszName, LPTSTR lpszAdrType, LPTSTR lpszAddress, ULONG ulFlags, ULONG* OUTPUT /*lpcbEntryID*/,
			 LPENTRYID* OUTPUT /*lppEntryID*/) = 0;
    virtual HRESULT NewEntry(ULONG ulUIParam, ULONG ulFlags, ULONG cbEIDContainer, LPENTRYID lpEIDContainer, ULONG cbEIDNewEntryTpl,
		     LPENTRYID lpEIDNewEntryTpl, ULONG* OUTPUT /*lpcbEIDNewEntry*/, LPENTRYID* OUTPUT /*lppEIDNewEntry*/) = 0;
    virtual HRESULT ResolveName(ULONG ulUIParam, ULONG ulFlags, LPTSTR lpszNewEntryTitle, LPADRLIST INOUT /*lpAdrList*/) = 0;
    virtual HRESULT Address(ULONG* lpulUIParam, LPADRPARM lpAdrParms, LPADRLIST* OUTPUT /*lppAdrList*/) = 0;
    virtual HRESULT Details(ULONG* lpulUIParam, LPFNDISMISS lpfnDismiss, LPVOID lpvDismissContext, ULONG cbEntryID, LPENTRYID lpEntryID, LPFNBUTTON lpfButtonCallback, LPVOID lpvButtonContext, LPTSTR lpszButtonText, ULONG ulFlags) = 0;
    virtual HRESULT RecipOptions(ULONG ulUIParam, ULONG ulFlags, LPADRENTRY lpRecip) = 0;
    virtual HRESULT QueryDefaultRecipOpt(LPTSTR lpszAdrType, ULONG ulFlags, ULONG* OUTPUT /*lpcValues*/, LPSPropValue* OUTPUT /*lppOptions*/) = 0;
    virtual HRESULT GetPAB(ULONG* OUTPUT /*lpcbEntryID*/, LPENTRYID* OUTPUT /*lppEntryID*/) = 0;
    virtual HRESULT SetPAB(ULONG cbEntryID, LPENTRYID lpEntryID) = 0;
    virtual HRESULT GetDefaultDir(ULONG* OUTPUT /*lpcbEntryID*/, LPENTRYID* OUTPUT /*lppEntryID*/) = 0;
    virtual HRESULT SetDefaultDir(ULONG cbEntryID, LPENTRYID lpEntryID) = 0;
    virtual HRESULT GetSearchPath(ULONG ulFlags, LPSRowSet* OUTPUT /*lppSearchPath*/) = 0;
    virtual HRESULT SetSearchPath(ULONG ulFlags, LPSRowSet INPUT /*lpSearchPath*/) = 0;
    virtual HRESULT PrepareRecips(ULONG ulFlags, LPSPropTagArray lpPropTagArray, LPADRLIST INOUT /*lpRecipList*/) = 0;
	%extend {
		~IAddrBook() { self->Release(); }
	}
};

/*
 * IProfAdmin Interface
 */

class IProfAdmin : public IUnknown {
public:
    //    virtual ~IProfAdmin() = 0;

    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* OUTPUT /*lppMAPIError*/) = 0;
    virtual HRESULT GetProfileTable(ULONG ulFlags, IMAPITable ** OUTPUT /*lppTable*/) = 0;
    virtual HRESULT CreateProfile(LPTSTR lpszProfileName, LPTSTR lpszPassword, ULONG ulUIParam, ULONG ulFlags) = 0;
    virtual HRESULT DeleteProfile(LPTSTR lpszProfileName, ULONG ulFlags) = 0;
    virtual HRESULT ChangeProfilePassword(LPTSTR lpszProfileName, LPTSTR lpszOldPassword, LPTSTR lpszNewPassword, ULONG ulFlags) = 0;
    virtual HRESULT CopyProfile(LPTSTR lpszOldProfileName, LPTSTR lpszOldPassword, LPTSTR lpszNewProfileName, ULONG ulUIParam,
				ULONG ulFlags) = 0;
    virtual HRESULT RenameProfile(LPTSTR lpszOldProfileName, LPTSTR lpszOldPassword, LPTSTR lpszNewProfileName, ULONG ulUIParam,
				  ULONG ulFlags) = 0;
    virtual HRESULT SetDefaultProfile(LPTSTR lpszProfileName, ULONG ulFlags) = 0;
    virtual HRESULT AdminServices(LPTSTR lpszProfileName, LPTSTR lpszPassword, ULONG ulUIParam, ULONG ulFlags,
				  IMsgServiceAdmin ** OUTPUT /*lppServiceAdmin*/) = 0;
	%extend {
		~IProfAdmin() { self->Release(); }
	}
};

/*
 * IMsgServiceAdmin Interface
 */
class IMsgServiceAdmin : public IUnknown {
public:
    //    virtual ~IMsgServiceAdmin() = 0;

    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* OUTPUT /*lppMAPIError*/) = 0;
    virtual HRESULT GetMsgServiceTable(ULONG ulFlags, IMAPITable ** OUTPUT /*lppTable*/) = 0;
    virtual HRESULT CreateMsgService(LPTSTR lpszService, LPTSTR lpszDisplayName, ULONG ulUIParam, ULONG ulFlags) = 0;
    virtual HRESULT DeleteMsgService(LPMAPIUID lpUID) = 0;
    virtual HRESULT CopyMsgService(LPMAPIUID lpUID, LPTSTR lpszDisplayName, LPCIID lpInterfaceToCopy, LPCIID lpInterfaceDst, LPVOID lpObjectDst, ULONG ulUIParam, ULONG ulFlags) = 0;
    virtual HRESULT RenameMsgService(LPMAPIUID lpUID, ULONG ulFlags, LPTSTR lpszDisplayName) = 0;
    virtual HRESULT ConfigureMsgService(LPMAPIUID lpUID, ULONG ulUIParam, ULONG ulFlags, ULONG cValues, LPSPropValue lpProps) = 0;
    virtual HRESULT OpenProfileSection(LPMAPIUID lpUID, LPCIID lpInterface, ULONG ulFlags, IProfSect ** OUTPUT /*lppProfSect*/) = 0;
    virtual HRESULT MsgServiceTransportOrder(ULONG cUID, LPMAPIUID lpUIDList, ULONG ulFlags) = 0;
    virtual HRESULT AdminProviders(LPMAPIUID lpUID, ULONG ulFlags, IProviderAdmin ** OUTPUT /*lppProviderAdmin*/) = 0;
    virtual HRESULT SetPrimaryIdentity(LPMAPIUID lpUID, ULONG ulFlags) = 0;
    virtual HRESULT GetProviderTable(ULONG ulFlags, IMAPITable ** OUTPUT /*lppTable*/) = 0;
	%extend {
		~IMsgServiceAdmin() { self->Release(); }
	}
};
