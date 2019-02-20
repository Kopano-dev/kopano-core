/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECARCHIVEAWAREMESSAGE_H
#define ECARCHIVEAWAREMESSAGE_H

#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include "ECAttach.h"
#include "ECMessage.h"
#include "ECMsgStore.h"
#include <kopano/CommonUtil.h>
#include <kopano/ECGuid.h>
#include <kopano/Util.h>
#include <list>
#include <map>
#include <string>
#include <vector>

class KC_EXPORT_DYCAST ECArchiveAwareMsgStore KC_FINAL_OPG :
    public ECMsgStore {
	public:
	_kc_hidden ECArchiveAwareMsgStore(const char *profname, IMAPISupport *, WSTransport *, BOOL modify, ULONG profflags, BOOL is_spooler, BOOL is_dfl_store, BOOL offline_store);
	_kc_hidden static HRESULT Create(const char *profname, IMAPISupport *, WSTransport *, BOOL modify, ULONG profflags, BOOL is_spooler, BOOL is_dfl_store, BOOL offline_store, ECMsgStore **ret);
	_kc_hidden virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	_kc_hidden virtual HRESULT OpenItemFromArchive(LPSPropValue propstore_eids, LPSPropValue propitem_eids, ECMessage **ret);

	private:
	typedef std::list<SBinary *> BinaryList;
	typedef BinaryList::iterator BinaryListIterator;
	typedef KC::object_ptr<ECMsgStore> ECMsgStorePtr;
	typedef std::vector<BYTE> EntryID;
	typedef std::map<EntryID, ECMsgStorePtr> MsgStoreMap;

	_kc_hidden HRESULT CreateCacheBasedReorderedList(SBinaryArray b_store_eids, SBinaryArray b_item_eids, BinaryList *store_eids, BinaryList *item_eids);
	_kc_hidden HRESULT GetArchiveStore(LPSBinary store_eid, ECMsgStore **ret);

	MsgStoreMap m_mapStores;
	ALLOC_WRAP_FRIEND;
};

class KC_EXPORT_DYCAST ECArchiveAwareMessage KC_FINAL_OPG : public ECMessage {
protected:
	/**
	 * \param lpMsgStore	The store owning this message.
	 * \param fNew			Specifies whether the message is a new message.
	 * \param fModify		Specifies whether the message is writable.
	 * \param ulFlags		Flags.
	 */
	_kc_hidden ECArchiveAwareMessage(ECArchiveAwareMsgStore *, BOOL fNew, BOOL modify, ULONG flags);
	_kc_hidden virtual ~ECArchiveAwareMessage(void) = default;

public:
	/**
	 * \brief Creates a new ECMessage object.
	 *
	 * Use this static method to create a new ECMessage object.
	 *
	 * \param lpMsgStore	The store owning this message.
	 * \param fNew			Specifies whether the message is a new message.
	 * \param fModify		Specifies whether the message is writable.
	 * \param ulFlags		Flags.
	 * \param bEmbedded		Specifies whether the message is embedded.
	 *
	 * \return hrSuccess on success.
	 */
	_kc_hidden static HRESULT Create(ECArchiveAwareMsgStore *store, BOOL fNew, BOOL modify, ULONG flags, ECMessage **);
	_kc_hidden virtual HRESULT HrLoadProps() override;
	_kc_hidden virtual HRESULT HrSetRealProp(const SPropValue *) override;
	_kc_hidden virtual HRESULT OpenProperty(ULONG proptag, const IID *intf, ULONG iface_opts, ULONG flags, IUnknown **) override;
	_kc_hidden virtual HRESULT OpenAttach(ULONG atnum, const IID *iface, ULONG flags, IAttach **) override;
	_kc_hidden virtual HRESULT CreateAttach(const IID *intf, ULONG flags, ULONG *atnum, IAttach **) override;
	_kc_hidden virtual HRESULT DeleteAttach(ULONG atnum, ULONG ui_param, IMAPIProgress *, ULONG flags) override;
	_kc_hidden virtual HRESULT ModifyRecipients(ULONG flags, const ADRLIST *mods) override;
	_kc_hidden virtual HRESULT SaveChanges(ULONG flags) override;
	_kc_hidden static HRESULT SetPropHandler(unsigned int tag, void *prov, const SPropValue *, ECGenericProp *);
	_kc_hidden bool IsLoading(void) const { return m_bLoading; }

protected:
	_kc_hidden virtual HRESULT HrDeleteRealProp(ULONG proptag, BOOL overwrite_ro) override;

private:
	_kc_hidden HRESULT MapNamedProps(void);
	_kc_hidden HRESULT CreateInfoMessage(const SPropTagArray *deleteprop, const std::string &bodyhtml);
	_kc_hidden std::string CreateErrorBodyUtf8(HRESULT);

	bool	m_bLoading, m_bNamedPropsMapped;
	PROPMAP_DECL()
	PROPMAP_DEF_NAMED_ID(ARCHIVE_STORE_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(ARCHIVE_ITEM_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(STUBBED)
	PROPMAP_DEF_NAMED_ID(DIRTY)
	PROPMAP_DEF_NAMED_ID(ORIGINAL_SOURCE_KEY)

	typedef KC::memory_ptr<SPropValue> SPropValuePtr;
	SPropValuePtr m_ptrStoreEntryIDs, m_ptrItemEntryIDs;

	enum eMode {
		MODE_UNARCHIVED,	// Not archived
		MODE_ARCHIVED,		// Archived and not stubbed
		MODE_STUBBED,		// Archived and stubbed
		MODE_DIRTY			// Archived and modified saved message
	};
	eMode	m_mode;
	bool	m_bChanged;
	KC::object_ptr<ECMessage> m_ptrArchiveMsg;
	ALLOC_WRAP_FRIEND;
};

class ECArchiveAwareMessageFactory final : public IMessageFactory {
public:
	HRESULT Create(ECMsgStore *, BOOL fnew, BOOL modify, ULONG flags, BOOL embedded, const ECMAPIProp *root, ECMessage **) const;
};

class ECArchiveAwareAttach KC_FINAL_OPG : public ECAttach {
	protected:
	ECArchiveAwareAttach(ECMsgStore *, ULONG obj_type, BOOL modify, ULONG attach_num, const ECMAPIProp *root);

	public:
	static HRESULT Create(ECMsgStore *, ULONG obj_type, BOOL modify, ULONG attach_num, const ECMAPIProp *root, ECAttach **);
	static HRESULT SetPropHandler(unsigned int tag, void *prov, const SPropValue *, ECGenericProp *);

	private:
	const ECArchiveAwareMessage *m_lpRoot;
	ALLOC_WRAP_FRIEND;
};

class ECArchiveAwareAttachFactory final : public IAttachFactory {
	public:
	HRESULT Create(ECMsgStore *, ULONG obj_type, BOOL modify, ULONG attach_num, const ECMAPIProp *root, ECAttach **) const;
};

#endif // ndef ECARCHIVEAWAREMESSAGE_H
