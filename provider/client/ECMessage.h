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

#ifndef ECMESSAGE_H
#define ECMESSAGE_H

#include <kopano/zcdefs.h>
#include <kopano/ECMemTable.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include <mapidefs.h>
#include "WSTransport.h"
#include "ECMsgStore.h"
#include "ECMAPIProp.h"

class ECAttach;
class IAttachFactory {
public:
	virtual HRESULT Create(ECMsgStore *, ULONG obj_type, BOOL modify, ULONG attach_num, const ECMAPIProp *root, ECAttach **) const = 0;
};

/**
 * \brief Represents a MAPI message.
 *
 * This class represents any kind of MAPI message and exposes it through
 * the IMessage interface.
 */
class ECMessage : public ECMAPIProp, public IMessage {
protected:
	/**
	 * \param lpMsgStore	The store owning this message.
	 * \param fNew			Specifies whether the message is a new message.
	 * \param fModify		Specifies whether the message is writable.
	 * \param ulFlags		Flags.
	 * \param bEmbedded		Specifies whether the message is embedded.
	 * \param lpRoot		The parent object when the message is embedded.
	 */
	ECMessage(ECMsgStore *, BOOL fnew, BOOL modify, ULONG flags, BOOL embedded, const ECMAPIProp *root);
	virtual ~ECMessage() = default;

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
	 * \param lpRoot		The parent object when the message is embedded.
	 * \param lpMessage		Pointer to a pointer in which the create object will be returned.
	 *
	 * \return hrSuccess on success.
	 */
	static HRESULT Create(ECMsgStore *, BOOL fnew, BOOL modify, ULONG flags, BOOL embedded, const ECMAPIProp *root, ECMessage **);

	/**
	 * \brief Handles GetProp requests for previously registered properties.
	 *
	 * Properties can be registered through ECGenericProp::HrAddPropHandlers to be obtained through 
	 * this function when special processing is needed. 
	 *
	 * \param[in] ulPropTag		The proptag of the requested property.
	 * \param[in] lpProvider	The provider for the requested property (Probably an ECMsgStore pointer).
	 * \param[out] lpsPropValue	Pointer to an SPropValue structure in which the result will be stored.
	 * \param[in] lpParam		Pointer passed to ECGenericProp::HrAddPropHandlers (usually an ECMessage pointer).
	 * \param[in] lpBase		Base pointer used for allocating more memory
	 *
	 * \return hrSuccess on success.
	 */
	static HRESULT	GetPropHandler(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	
	/**
	 * \brief Handles SetProp requests for previously registered properties.
	 *
	 * Properties can be registered through ECGenericProp::HrAddPropHandlers to be set through
	 * this function when special processing is needed.
	 *
	 * \param ulPropTag		The proptag of the requested property.
	 * \param lpProvider	The provider for the requested property (Probably an ECMsgStore pointer).
	 * \param lpsPropValue	Pointer to an SPropValue structure which holds the data to be set.
	 * \param lpParam		Pointer passed to ECGenericProp::HrAddPropHandlers (usually an ECMessage pointer).
	 *
	 * \return hrSuccess on success.
	 */
	static HRESULT SetPropHandler(ULONG ulPropTag, void *lpProvider, const SPropValue *lpsPropValue, void *lpParam);
	virtual HRESULT	QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk);
	virtual HRESULT GetAttachmentTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT OpenAttach(ULONG ulAttachmentNum, LPCIID lpInterface, ULONG ulFlags, LPATTACH *lppAttach);
	virtual HRESULT CreateAttach(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulAttachmentNum, LPATTACH *lppAttach);
	virtual HRESULT DeleteAttach(ULONG ulAttachmentNum, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags);
	virtual HRESULT GetRecipientTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT ModifyRecipients(ULONG ulFlags, const ADRLIST *lpMods);
	virtual HRESULT SubmitMessage(ULONG ulFlags);
	virtual HRESULT SetReadFlag(ULONG ulFlags);

	// override for IMAPIProp::SaveChanges
	virtual HRESULT SaveChanges(ULONG ulFlags);
	virtual HRESULT HrSaveChild(ULONG ulFlags, MAPIOBJECT *lpsMapiObject);
	// override for IMAPIProp::CopyTo
	virtual HRESULT CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, const SPropTagArray *lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
	virtual HRESULT CopyProps(const SPropTagArray *lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);

	// RTF/Subject overrides
	virtual HRESULT SetProps(ULONG cValues, const SPropValue *lpPropArray, LPSPropProblemArray *lppProblems);
	virtual HRESULT DeleteProps(const SPropTagArray *lpPropTagArray, LPSPropProblemArray *lppProblems);
	virtual HRESULT HrLoadProps();

	// Our table-row getprop handler (handles client-side generation of table columns)
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);

	// RTF overrides
	virtual HRESULT HrSetRealProp(const SPropValue *lpsPropValue);

protected:
	void RecursiveMarkDelete(MAPIOBJECT *lpObj);

	HRESULT CreateAttach(LPCIID lpInterface, ULONG ulFlags, const IAttachFactory &refFactory, ULONG *lpulAttachmentNum, LPATTACH *lppAttach);

	HRESULT GetRtfData(std::string *lpstrRtfData);
	HRESULT GetCodePage(unsigned int *lpulCodePage);

private:
	enum eSyncChange {syncChangeNone, syncChangeBody, syncChangeRTF, syncChangeHTML};
	enum eBodyType { bodyTypeUnknown, bodyTypePlain, bodyTypeRTF, bodyTypeHTML };

	HRESULT UpdateTable(ECMemTable *lpTable, ULONG ulObjType, ULONG ulObjKeyProp);
	HRESULT SaveRecips();
	HRESULT SyncAttachments();
	BOOL HasAttachment();

	HRESULT SyncRecips();
	HRESULT SyncSubject();
	HRESULT GetBodyType(eBodyType *lpulBodyType);
	
	// Override GetProps/GetPropList so we can sync RTF before calling GetProps
	virtual HRESULT GetProps(const SPropTagArray *lpPropTagArray, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppPropArray);
	virtual HRESULT GetPropList(ULONG ulFlags, LPSPropTagArray *lppPropTagArray);

	HRESULT GetSyncedBodyProp(ULONG ulPropTag, ULONG ulFlags, void *lpBase, LPSPropValue lpsPropValue);
	HRESULT SyncBody(ULONG ulPropTag);
	HRESULT SyncPlainToRtf();
	HRESULT SyncPlainToHtml();
	HRESULT SyncRtf();
	HRESULT SyncHtmlToPlain();
	HRESULT SyncHtmlToRtf();
	
	BOOL			fNew;
	BOOL			m_bEmbedded;
	BOOL m_bExplicitSubjectPrefix = false;
	BOOL m_bRecipsDirty = false, m_bInhibitSync = false;
	eBodyType m_ulBodyType = bodyTypeUnknown;

public:
	ULONG m_cbParentID = 0;
	KC::object_ptr<ECMemTable> lpAttachments, lpRecips;
	KC::memory_ptr<ENTRYID> m_lpParentID;
	ULONG ulNextRecipUniqueId = 0, ulNextAttUniqueId = 0;
	ALLOC_WRAP_FRIEND;
};

class ECMessageFactory _kc_final : public IMessageFactory {
public:
	HRESULT Create(ECMsgStore *, BOOL fnew, BOOL modify, ULONG flags, BOOL embedded, const ECMAPIProp *root, ECMessage **) const;
};

namespace KC {

static inline constexpr const IID &iid_of(const ECMessage *) { return IID_ECMessage; }

}

#endif // ECMESSAGE_H
