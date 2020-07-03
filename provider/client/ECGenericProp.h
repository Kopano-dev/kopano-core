/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <memory>
#include <mutex>
#include <kopano/ECUnknown.h>
#include "IECPropStorage.h"
#include "ECPropertyEntry.h"
#include <kopano/IECInterfaces.hpp>
#include <kopano/memory.hpp>
#include <map>
#include <set>

// These are the callback functions called when a software-handled property is requested
class ECGenericProp;
typedef HRESULT (*SetPropCallBack)(unsigned int tag, void *prov, const SPropValue *, ECGenericProp *);
typedef HRESULT (*GetPropCallBack)(unsigned int tag, void *prov, unsigned int flags, SPropValue *, ECGenericProp *, void *base);

struct PROPCALLBACK {
	ULONG ulPropTag;
	SetPropCallBack lpfnSetProp;
	GetPropCallBack lpfnGetProp;
	ECGenericProp *lpParam;
	BOOL			fRemovable;
	BOOL			fHidden; // hidden from GetPropList

	bool operator==(const PROPCALLBACK &callback) const noexcept
	{
		return callback.ulPropTag == ulPropTag;
	}
};

class ECGenericProp :
    public KC::ECUnknown, public virtual IMAPIProp,
    public KC::IECSingleInstance {
protected:
	ECGenericProp(void *prov, unsigned int obj_type, BOOL modify);
	virtual ~ECGenericProp() = default;

public:
	virtual HRESULT QueryInterface(const IID &, void **) override;

	HRESULT SetProvider(void* lpProvider);
	HRESULT SetEntryId(ULONG eid_size, const ENTRYID *eid);
	static HRESULT DefaultGetPropGetReal(unsigned int tag, void *prov, unsigned int flags, SPropValue *, ECGenericProp *lpParam, void *base);
	static HRESULT DefaultSetPropComputed(unsigned int tag, void *prov, const SPropValue *, ECGenericProp *);
	static HRESULT DefaultSetPropIgnore(unsigned int tag, void *prov, const SPropValue *, ECGenericProp *);
	static HRESULT DefaultSetPropSetReal(unsigned int tag, void *prov, const SPropValue *, ECGenericProp *);
	static HRESULT DefaultGetProp(unsigned int tag, void *prov, unsigned int flags, SPropValue *, ECGenericProp *lpParam, void *base);

	// Our table-row getprop handler (handles client-side generation of table columns)
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);
	virtual HRESULT HrSetPropStorage(IECPropStorage *lpStorage, BOOL fLoadProps);
	virtual HRESULT HrSetRealProp(const SPropValue *lpsPropValue);
	virtual HRESULT		HrGetRealProp(ULONG ulPropTag, ULONG ulFlags, void *lpBase, LPSPropValue lpsPropValue, ULONG ulMaxSize = 0);
	virtual HRESULT HrAddPropHandlers(unsigned int tag, GetPropCallBack, SetPropCallBack, ECGenericProp *, BOOL removable = false, BOOL hidden = false);
	virtual HRESULT 	HrLoadEmptyProps();

	bool IsReadOnly() const;

protected: ///?
	virtual HRESULT		HrLoadProps();
	HRESULT				HrLoadProp(ULONG ulPropTag);
	virtual HRESULT		HrDeleteRealProp(ULONG ulPropTag, BOOL fOverwriteRO);
	HRESULT HrGetHandler(unsigned int tag, SetPropCallBack *, GetPropCallBack *, ECGenericProp **);
	HRESULT				IsPropDirty(ULONG ulPropTag, BOOL *lpbDirty);
	HRESULT				HrSetClean();
	HRESULT				HrSetCleanProperty(ULONG ulPropTag);

	/* used by child to save here in it's parent */
	friend class ECParentStorage;
	virtual HRESULT HrSaveChild(ULONG ulFlags, MAPIOBJECT *lpsMapiObject);
	virtual HRESULT HrRemoveModifications(MAPIOBJECT *lpsMapiObject, ULONG ulPropTag);

	// For IECSingleInstance
	virtual HRESULT GetSingleInstanceId(ULONG *id_size, ENTRYID **id) override;
	virtual HRESULT SetSingleInstanceId(ULONG eid_size, const ENTRYID *eid) override;

public:
	// From IMAPIProp
	virtual HRESULT GetLastError(HRESULT, ULONG flags, MAPIERROR **) override;
	virtual HRESULT SaveChanges(ULONG flags) override;
	virtual HRESULT GetProps(const SPropTagArray *, ULONG flags, ULONG *nprops, SPropValue **) override;
	virtual HRESULT GetPropList(ULONG flags, SPropTagArray **) override;

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
	virtual HRESULT SetProps(ULONG nprops, const SPropValue *props, SPropProblemArray **) override;
	virtual HRESULT DeleteProps(const SPropTagArray *, LPSPropProblemArray *) override;

protected:
	std::map<short, ECPropertyEntry> lstProps;
	std::set<ULONG>			m_setDeletedProps;
	std::map<short, PROPCALLBACK> lstCallBack;
	DWORD dwLastError = hrSuccess;
	BOOL fSaved = false; // only 0 if just created, // not saved until we either read or write from/to disk
	ULONG					ulObjType;
	ULONG ulObjFlags = 0; // message: MAPI_ASSOCIATED, folder: FOLDER_SEARCH (last?)
	BOOL					fModify;
	void*					lpProvider;
	BOOL isTransactedObject = true; // only ECMsgStore and ECMAPIFolder are not transacted
	ULONG m_ulMaxPropSize = 8192;
	bool m_props_loaded = false;

public:
	// Current entryid of object
	ULONG m_cbEntryId = 0;
	std::recursive_mutex m_hMutexMAPIObject; /* Mutex for locking the MAPIObject */
	BOOL m_bReload = false, m_bLoading = false;
	KC::memory_ptr<ENTRYID> m_lpEntryId;
	KC::object_ptr<IECPropStorage> lpStorage;
	std::unique_ptr<MAPIOBJECT> m_sMapiObject;
};

inline bool ECGenericProp::IsReadOnly() const {
	return !fModify;
}
