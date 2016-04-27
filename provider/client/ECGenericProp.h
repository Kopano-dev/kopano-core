/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef ECGENERICPROP_H
#define ECGENERICPROP_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include "IECPropStorage.h"
#include "ECPropertyEntry.h"
#include <kopano/IECSingleInstance.h>

#include <list>
#include <map>
#include <set>

// These are the callback functions called when a software-handled property is requested
typedef HRESULT (* SetPropCallBack)(ULONG ulPropTag, void* lpProvider, LPSPropValue lpsPropValue, void *lpParam);
typedef HRESULT (* GetPropCallBack)(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);

typedef struct PROPCALLBACK {
	ULONG ulPropTag;
	SetPropCallBack lpfnSetProp;
	GetPropCallBack lpfnGetProp;
	void *			lpParam;
	BOOL			fRemovable;
	BOOL			fHidden; // hidden from GetPropList

	bool operator==(const PROPCALLBACK &callback) const
	{
		return callback.ulPropTag == this->ulPropTag;
	}
} PROPCALLBACK;

typedef std::map<short, PROPCALLBACK>			ECPropCallBackMap;
typedef ECPropCallBackMap::iterator				ECPropCallBackIterator;
typedef std::map<short, ECPropertyEntry>		ECPropertyEntryMap;
typedef ECPropertyEntryMap::iterator			ECPropertyEntryIterator;

class ECGenericProp : public ECUnknown
{
protected:
	ECGenericProp(void *lpProvider, ULONG ulObjType, BOOL fModify, const char *szClassName = NULL);
	virtual ~ECGenericProp();

public:
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);

	HRESULT SetProvider(void* lpProvider);
	HRESULT SetEntryId(ULONG cbEntryId, LPENTRYID lpEntryId);

	static HRESULT		DefaultGetPropGetReal(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);	
	static HRESULT		DefaultGetPropNotFound(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	static HRESULT		DefaultSetPropComputed(ULONG ulPropTag, void* lpProvider, LPSPropValue lpsPropValue, void *lpParam);	
	static HRESULT		DefaultSetPropIgnore(ULONG ulPropTag, void* lpProvider, LPSPropValue lpsPropValue, void *lpParam);	
	static HRESULT		DefaultSetPropSetReal(ULONG ulPropTag, void* lpProvider, LPSPropValue lpsPropValue, void *lpParam);
	static HRESULT		DefaultGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);

	// Our table-row getprop handler (handles client-side generation of table columns)
	static HRESULT		TableRowGetProp(void* lpProvider, struct propVal *lpsPropValSrc, LPSPropValue lpsPropValDst, void **lpBase, ULONG ulType);

	virtual HRESULT HrSetPropStorage(IECPropStorage *lpStorage, BOOL fLoadProps);
	
	virtual HRESULT		HrSetRealProp(SPropValue *lpsPropValue);
	virtual HRESULT		HrGetRealProp(ULONG ulPropTag, ULONG ulFlags, void *lpBase, LPSPropValue lpsPropValue, ULONG ulMaxSize = 0);
	virtual HRESULT		HrAddPropHandlers(ULONG ulPropTag, GetPropCallBack lpfnGetProp, SetPropCallBack lpfnSetProp, void *lpParam, BOOL fRemovable = FALSE, BOOL fHidden = FALSE);
	virtual HRESULT 	HrLoadEmptyProps();

	bool IsReadOnly() const;

protected: ///?
	virtual HRESULT		HrLoadProps();
	HRESULT				HrLoadProp(ULONG ulPropTag);
	virtual HRESULT		HrDeleteRealProp(ULONG ulPropTag, BOOL fOverwriteRO);
	HRESULT				HrGetHandler(ULONG ulPropTag, SetPropCallBack *lpfnSetProp, GetPropCallBack *lpfnGetProp, void **lpParam);
	HRESULT				IsPropDirty(ULONG ulPropTag, BOOL *lpbDirty);
	HRESULT				HrSetClean();
	HRESULT				HrSetCleanProperty(ULONG ulPropTag);

	/* used by child to save here in it's parent */
	friend class ECParentStorage;
	virtual HRESULT HrSaveChild(ULONG ulFlags, MAPIOBJECT *lpsMapiObject);
	virtual HRESULT HrRemoveModifications(MAPIOBJECT *lpsMapiObject, ULONG ulPropTag);

	// For IECSingleInstance
	virtual HRESULT GetSingleInstanceId(ULONG *lpcbInstanceID, LPSIEID *lppInstanceID);
	virtual HRESULT SetSingleInstanceId(ULONG cbInstanceID, LPSIEID lpInstanceID);

public:
	// From IMAPIProp
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR FAR * lppMAPIError);
	virtual HRESULT SaveChanges(ULONG ulFlags);
	virtual HRESULT GetProps(LPSPropTagArray lpPropTagArray, ULONG ulFlags, ULONG FAR * lpcValues, LPSPropValue FAR * lppPropArray);
	virtual HRESULT GetPropList(ULONG ulFlags, LPSPropTagArray FAR * lppPropTagArray);
	
	/**
	 * \brief Opens a property.
	 *
	 * \param ulPropTag				The proptag of the property to open.
	 * \param lpiid					Pointer to the requested interface for the object to be opened.
	 * \param ulInterfaceOptions	Interface options.
	 * \param ulFlags				Flags.
	 * \param lppUnk				Pointer to an IUnknown pointer where the opened object will be stored.
	 *
	 * \return hrSuccess on success.
	 */
	virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN FAR * lppUnk);
	virtual HRESULT SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray FAR * lppProblems);
	virtual HRESULT DeleteProps(LPSPropTagArray lpPropTagArray, LPSPropProblemArray FAR * lppProblems);
	virtual HRESULT CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems);
	virtual HRESULT CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems);
	virtual HRESULT GetNamesFromIDs(LPSPropTagArray FAR * lppPropTags, LPGUID lpPropSetGuid, ULONG ulFlags, ULONG FAR * lpcPropNames, LPMAPINAMEID FAR * FAR * lpppPropNames);
	virtual HRESULT GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID FAR * lppPropNames, ULONG ulFlags, LPSPropTagArray FAR * lppPropTags);

public:
	class xMAPIProp _zcp_final : public IMAPIProp {
	public:
		// From IUnknown
		virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;
		virtual ULONG __stdcall AddRef(void) _zcp_override;
		virtual ULONG __stdcall Release(void) _zcp_override;
		
		// From IMAPIProp
		virtual HRESULT __stdcall GetLastError(HRESULT hError, ULONG ulFlags, LPMAPIERROR * lppMapiError);
		virtual HRESULT __stdcall SaveChanges(ULONG ulFlags);
		virtual HRESULT __stdcall GetProps(LPSPropTagArray lpPropTagArray, ULONG ulFlags, ULONG FAR * lpcValues, LPSPropValue FAR * lppPropArray);
		virtual HRESULT __stdcall GetPropList(ULONG ulFlags, LPSPropTagArray FAR * lppPropTagArray);
		virtual HRESULT __stdcall OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN FAR * lppUnk);
		virtual HRESULT __stdcall SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray FAR * lppProblems);
		virtual HRESULT __stdcall DeleteProps(LPSPropTagArray lpPropTagArray, LPSPropProblemArray FAR * lppProblems);
		virtual HRESULT __stdcall CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems);
		virtual HRESULT __stdcall CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems);
		virtual HRESULT __stdcall GetNamesFromIDs(LPSPropTagArray * pptaga, LPGUID lpguid, ULONG ulFlags, ULONG * pcNames, LPMAPINAMEID ** pppNames);
		virtual HRESULT __stdcall GetIDsFromNames(ULONG cNames, LPMAPINAMEID * ppNames, ULONG ulFlags, LPSPropTagArray * pptaga);
	} m_xMAPIProp;

	class xECSingleInstance _zcp_final : public IECSingleInstance {
	public:
		// IUnknown
		virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;
		virtual ULONG __stdcall AddRef(void) _zcp_override;
		virtual ULONG __stdcall Release(void) _zcp_override;

		// IECSingleInstance
		virtual HRESULT __stdcall GetSingleInstanceId(ULONG *lpcbInstanceID, LPENTRYID *lppInstanceID) _zcp_override;
		virtual HRESULT __stdcall SetSingleInstanceId(ULONG cbInstanceID, LPENTRYID lpInstanceID) _zcp_override;
	} m_xECSingleInstance;

protected:
	ECPropertyEntryMap*		lstProps;
	std::set<ULONG>			m_setDeletedProps;
	ECPropCallBackMap		lstCallBack;
	DWORD					dwLastError;
	BOOL					fSaved;			// only 0 if just created
	ULONG					ulObjType;
	ULONG					ulObjFlags;		// message: MAPI_ASSOCIATED, folder: FOLDER_SEARCH (last?)

	BOOL					fModify;
	void*					lpProvider;
	BOOL					isTransactedObject;
	ULONG					m_ulMaxPropSize;

public:
	// Current entryid of object
	ULONG					m_cbEntryId;
	LPENTRYID				m_lpEntryId;

	MAPIOBJECT				*m_sMapiObject;
	pthread_mutex_t			m_hMutexMAPIObject;	///< Mutex for locking the MAPIObject
	BOOL					m_bReload;
	BOOL					m_bLoading;

	IECPropStorage*			lpStorage;
};


// Inlines
inline bool ECGenericProp::IsReadOnly() const {
	return !fModify;
}

#endif
