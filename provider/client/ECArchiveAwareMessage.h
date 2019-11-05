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
	KC_HIDDEN ECArchiveAwareMsgStore(const char *profname, IMAPISupport *, WSTransport *, BOOL modify, unsigned int profflags, BOOL is_spooler, BOOL is_dfl_store, BOOL offline_store);
	KC_HIDDEN static HRESULT Create(const char *profname, IMAPISupport *, WSTransport *, BOOL modify, unsigned int profflags, BOOL is_spooler, BOOL is_dfl_store, BOOL offline_store, ECMsgStore **ret);
	KC_HIDDEN virtual HRESULT OpenEntry(unsigned int eid_size, const ENTRYID *eid, const IID *intf, unsigned int flags, unsigned int *obj_type, IUnknown **);
	KC_HIDDEN virtual HRESULT OpenItemFromArchive(SPropValue *propstore_eids, SPropValue *propitem_eids, ECMessage **ret);

	private:
	typedef std::list<SBinary *> BinaryList;
	typedef BinaryList::iterator BinaryListIterator;
	typedef KC::object_ptr<ECMsgStore> ECMsgStorePtr;
	typedef std::vector<BYTE> EntryID;
	typedef std::map<EntryID, ECMsgStorePtr> MsgStoreMap;

	KC_HIDDEN HRESULT CreateCacheBasedReorderedList(SBinaryArray b_store_eids, SBinaryArray b_item_eids, BinaryList *store_eids, BinaryList *item_eids);
	KC_HIDDEN HRESULT GetArchiveStore(SBinary *store_eid, ECMsgStore **ret);

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
	KC_HIDDEN ECArchiveAwareMessage(ECArchiveAwareMsgStore *, BOOL fNew, BOOL modify, unsigned int flags);
	KC_HIDDEN virtual ~ECArchiveAwareMessage() = default;

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
	KC_HIDDEN static HRESULT Create(ECArchiveAwareMsgStore *store, BOOL fNew, BOOL modify, unsigned int flags, ECMessage **);
	KC_HIDDEN virtual HRESULT HrLoadProps() override;
	KC_HIDDEN virtual HRESULT HrSetRealProp(const SPropValue *) override;
	KC_HIDDEN virtual HRESULT OpenProperty(unsigned int proptag, const IID *intf, unsigned int iface_opts, unsigned int flags, IUnknown **) override;
	KC_HIDDEN virtual HRESULT OpenAttach(unsigned int atnum, const IID *iface, unsigned int flags, IAttach **) override;
	KC_HIDDEN virtual HRESULT CreateAttach(const IID *intf, unsigned int flags, unsigned int *atnum, IAttach **) override;
	KC_HIDDEN virtual HRESULT DeleteAttach(unsigned int atnum, unsigned int ui_param, IMAPIProgress *, unsigned int flags) override;
	KC_HIDDEN virtual HRESULT ModifyRecipients(unsigned int flags, const ADRLIST *mods) override;
	KC_HIDDEN virtual HRESULT SaveChanges(unsigned int flags) override;
	KC_HIDDEN static HRESULT SetPropHandler(unsigned int tag, void *prov, const SPropValue *, ECGenericProp *);
	KC_HIDDEN bool IsLoading() const { return m_bLoading; }

protected:
	KC_HIDDEN virtual HRESULT HrDeleteRealProp(unsigned int proptag, BOOL overwrite_ro) override;

private:
	KC_HIDDEN HRESULT MapNamedProps();
	KC_HIDDEN HRESULT CreateInfoMessage(const SPropTagArray *deleteprop, const std::string &bodyhtml);
	KC_HIDDEN std::string CreateErrorBodyUtf8(HRESULT);

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
