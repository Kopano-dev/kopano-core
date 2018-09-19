/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECARCHIVEAWAREMESSAGE_H
#define ECARCHIVEAWAREMESSAGE_H

#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include "ECMessage.h"
#include <kopano/CommonUtil.h>
#include <string>

class ECArchiveAwareMsgStore;

class _kc_export_dycast ECArchiveAwareMessage _kc_final : public ECMessage {
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
	_kc_hidden static HRESULT SetPropHandler(ULONG proptag, void *prov, const SPropValue *, void *param);
	_kc_hidden bool IsLoading(void) const { return m_bLoading; }

protected:
	_kc_hidden virtual HRESULT HrDeleteRealProp(ULONG proptag, BOOL overwrite_ro) override;

private:
	_kc_hidden HRESULT MapNamedProps(void);
	_kc_hidden HRESULT CreateInfoMessage(const SPropTagArray *deleteprop, const std::string &bodyhtml);
	_kc_hidden std::string CreateErrorBodyUtf8(HRESULT);
	_kc_hidden std::string CreateOfflineWarnBodyUtf8(void);

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

class ECArchiveAwareMessageFactory _kc_final : public IMessageFactory {
public:
	HRESULT Create(ECMsgStore *, BOOL fnew, BOOL modify, ULONG flags, BOOL embedded, const ECMAPIProp *root, ECMessage **) const;
};

#endif // ndef ECARCHIVEAWAREMESSAGE_H
