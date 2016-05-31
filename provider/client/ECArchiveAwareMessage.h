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

#ifndef ECARCHIVEAWAREMESSAGE_H
#define ECARCHIVEAWAREMESSAGE_H

#include <kopano/zcdefs.h>
#include "ECMessage.h"
#include <kopano/CommonUtil.h>

#include <kopano/mapi_ptr/mapi_memory_ptr.h>
#include <kopano/mapi_ptr/mapi_object_ptr.h>

#include <string>

class ECArchiveAwareMsgStore;

class ECArchiveAwareMessage _kc_final : public ECMessage {
protected:
	/**
	 * \brief Constructor
	 *
	 * \param lpMsgStore	The store owning this message.
	 * \param fNew			Specifies whether the message is a new message.
	 * \param fModify		Specifies whether the message is writable.
	 * \param ulFlags		Flags.
	 */
	ECArchiveAwareMessage(ECArchiveAwareMsgStore *lpMsgStore, BOOL fNew, BOOL fModify, ULONG ulFlags);
	virtual ~ECArchiveAwareMessage();

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
	static HRESULT	Create(ECArchiveAwareMsgStore *lpMsgStore, BOOL fNew, BOOL fModify, ULONG ulFlags, ECMessage **lppMessage);

	virtual HRESULT HrLoadProps();
	virtual HRESULT	HrSetRealProp(SPropValue *lpsPropValue);

	virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN FAR * lppUnk);

	virtual HRESULT OpenAttach(ULONG ulAttachmentNum, LPCIID lpInterface, ULONG ulFlags, LPATTACH *lppAttach);
	virtual HRESULT CreateAttach(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulAttachmentNum, LPATTACH *lppAttach);
	virtual HRESULT DeleteAttach(ULONG ulAttachmentNum, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags);

	virtual HRESULT ModifyRecipients(ULONG ulFlags, LPADRLIST lpMods);

	virtual HRESULT SaveChanges(ULONG ulFlags);

	static HRESULT	SetPropHandler(ULONG ulPropTag, void* lpProvider, LPSPropValue lpsPropValue, void *lpParam);

	bool IsLoading() const { return m_bLoading; }

protected:
	virtual HRESULT	HrDeleteRealProp(ULONG ulPropTag, BOOL fOverwriteRO);

private:
	HRESULT MapNamedProps();
	HRESULT CreateInfoMessage(LPSPropTagArray lpptaDeleteProps, const std::string &strBodyHtml);
	std::string CreateErrorBodyUtf8(HRESULT hResult);
	std::string CreateOfflineWarnBodyUtf8();

private:
	bool	m_bLoading;

	bool	m_bNamedPropsMapped;
	PROPMAP_START
	PROPMAP_DEF_NAMED_ID(ARCHIVE_STORE_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(ARCHIVE_ITEM_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(STUBBED)
	PROPMAP_DEF_NAMED_ID(DIRTY)
	PROPMAP_DEF_NAMED_ID(ORIGINAL_SOURCE_KEY)

	typedef mapi_memory_ptr<SPropValue> SPropValuePtr;
	SPropValuePtr	m_ptrStoreEntryIDs;
	SPropValuePtr	m_ptrItemEntryIDs;

	enum eMode {
		MODE_UNARCHIVED,	// Not archived
		MODE_ARCHIVED,		// Archived and not stubbed
		MODE_STUBBED,		// Archived and stubbed
		MODE_DIRTY			// Archived and modified saved message
	};
	eMode	m_mode;
	bool	m_bChanged;

	typedef mapi_object_ptr<ECMessage, IID_ECMessage>	ECMessagePtr;
	ECMessagePtr	m_ptrArchiveMsg;
};

class ECArchiveAwareMessageFactory : public IMessageFactory {
public:
	HRESULT Create(ECMsgStore *lpMsgStore, BOOL fNew, BOOL fModify, ULONG ulFlags, BOOL bEmbedded, ECMAPIProp *lpRoot, ECMessage **lppMessage) const;
};

#endif // ndef ECARCHIVEAWAREMESSAGE_H
