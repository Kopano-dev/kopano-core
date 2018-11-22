/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECMESSAGE_H
#define ECMESSAGE_H

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
	virtual HRESULT	QueryInterface(const IID &, void **) override;
	virtual HRESULT OpenProperty(ULONG proptag, const IID *intf, ULONG iface_opts, ULONG flags, IUnknown **) override;
	virtual HRESULT GetAttachmentTable(ULONG flags, IMAPITable **) override;
	virtual HRESULT OpenAttach(ULONG atnum, const IID *intf, ULONG flags, IAttach **) override;
	virtual HRESULT CreateAttach(const IID *intf, ULONG flags, ULONG *atnum, IAttach **) override;
	virtual HRESULT DeleteAttach(ULONG atnum, ULONG ui_param, IMAPIProgress *, ULONG flags) override;
	virtual HRESULT GetRecipientTable(ULONG flags, IMAPITable **) override;
	virtual HRESULT ModifyRecipients(ULONG flags, const ADRLIST *mods) override;
	virtual HRESULT SubmitMessage(ULONG flags) override;
	virtual HRESULT SetReadFlag(ULONG flags) override;

	// override for IMAPIProp::SaveChanges
	virtual HRESULT SaveChanges(ULONG flags) override;
	virtual HRESULT HrSaveChild(ULONG flags, MAPIOBJECT *) override;
	// override for IMAPIProp::CopyTo
	virtual HRESULT CopyTo(ULONG nexcl, const IID *excl, const SPropTagArray *exclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;
	virtual HRESULT CopyProps(const SPropTagArray *inclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;

	// RTF/Subject overrides
	virtual HRESULT SetProps(ULONG nvals, const SPropValue *, SPropProblemArray **) override;
	virtual HRESULT DeleteProps(const SPropTagArray *, SPropProblemArray **) override;
	virtual HRESULT HrLoadProps() override;

	// Our table-row getprop handler (handles client-side generation of table columns)
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);

	// RTF overrides
	virtual HRESULT HrSetRealProp(const SPropValue *) override;

protected:
	void RecursiveMarkDelete(MAPIOBJECT *lpObj);
	HRESULT CreateAttach(LPCIID lpInterface, ULONG ulFlags, const IAttachFactory &refFactory, ULONG *lpulAttachmentNum, LPATTACH *lppAttach);
	HRESULT GetRtfData(std::string *lpstrRtfData);
	HRESULT GetCodePage(unsigned int *lpulCodePage);

private:
	enum eSyncChange {syncChangeNone, syncChangeBody, syncChangeRTF, syncChangeHTML};
	enum eBodyType { bodyTypeUnknown, bodyTypePlain, bodyTypeRTF, bodyTypeHTML };
	HRESULT UpdateTable(KC::ECMemTable *table, ULONG obj_type, ULONG obj_keyprop);
	HRESULT SaveRecips();
	HRESULT SyncAttachments();
	BOOL HasAttachment();
	HRESULT SyncRecips();
	HRESULT SyncSubject();
	HRESULT GetBodyType(const std::string &rtf, eBodyType *out);

	// Override GetProps/GetPropList so we can sync RTF before calling GetProps
	virtual HRESULT GetProps(const SPropTagArray *, ULONG flags, ULONG *nvals, SPropValue **) override;
	virtual HRESULT GetPropList(ULONG flags, SPropTagArray **) override;

	HRESULT GetSyncedBodyProp(ULONG ulPropTag, ULONG ulFlags, void *lpBase, LPSPropValue lpsPropValue);
	HRESULT SyncBody(ULONG ulPropTag);
	HRESULT SyncPlainToRtf();
	HRESULT SyncPlainToHtml();
	HRESULT SyncRtf(const std::string &rtf);
	HRESULT SyncHtmlToPlain();
	HRESULT SyncHtmlToRtf();
	HRESULT SetReadFlag2(unsigned int flags);
	
	BOOL fNew, m_bEmbedded, m_bExplicitSubjectPrefix = false;
	BOOL m_bRecipsDirty = false, m_bInhibitSync = false;
	eBodyType m_ulBodyType = bodyTypeUnknown;

public:
	ULONG m_cbParentID = 0;
	KC::object_ptr<KC::ECMemTable> lpAttachments, lpRecips;
	KC::memory_ptr<ENTRYID> m_lpParentID;
	ULONG ulNextRecipUniqueId = 0, ulNextAttUniqueId = 0;
	ALLOC_WRAP_FRIEND;
};

class ECMessageFactory final : public IMessageFactory {
public:
	HRESULT Create(ECMsgStore *, BOOL fnew, BOOL modify, ULONG flags, BOOL embedded, const ECMAPIProp *root, ECMessage **) const;
};

namespace KC {

static inline constexpr const IID &iid_of(const ECMessage *) { return IID_ECMessage; }

}

#endif // ECMESSAGE_H
