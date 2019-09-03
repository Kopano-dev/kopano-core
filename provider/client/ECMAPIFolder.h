/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECMAPIFOLDER_H
#define ECMAPIFOLDER_H

#include <kopano/memory.hpp>
#include <mapidefs.h>
#include <kopano/Util.h>
#include "WSTransport.h"
#include "ECMsgStore.h"
#include "ECMAPIContainer.h"

class WSMessageStreamExporter;
class WSMessageStreamImporter;

class ECMAPIFolder :
    public ECMAPIContainer, public IMAPIFolder, public IFolderSupport {
protected:
	ECMAPIFolder(ECMsgStore *lpMsgStore, BOOL fModify, WSMAPIFolderOps *lpFolderOps, const char *szClassName);
	virtual ~ECMAPIFolder();

public:
	struct ECFolder {
		unsigned int folder_type = FOLDER_GENERIC, flags = 0;
		const TCHAR *name = nullptr, *comment = nullptr;
		const IID *interface = nullptr;
		/* This will hold the resulting folder object */
		KC::object_ptr<IMAPIFolder> folder;
	};

	static HRESULT Create(ECMsgStore *lpMsgStore, BOOL fModify, WSMAPIFolderOps *lpFolderOps, ECMAPIFolder **lppECMAPIFolder);
	static HRESULT GetPropHandler(unsigned int tag, void *prov, unsigned int flags, SPropValue *, ECGenericProp *lpParam, void *base);
	static HRESULT SetPropHandler(unsigned int tag, void *prov, const SPropValue *, ECGenericProp *);
	HRESULT enable_transaction(bool);

	// Our table-row getprop handler (handles client-side generation of table columns)
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);
	virtual HRESULT	QueryInterface(const IID &, void **) override;
	virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk);

	// Override IMAPIProp
	virtual HRESULT SaveChanges(unsigned int flags) override;
	virtual HRESULT CopyTo(unsigned int nexcl, const IID *excliid, const SPropTagArray *exclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest_obj, unsigned int flags, SPropProblemArray **) override;
	virtual HRESULT CopyProps(const SPropTagArray *inclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest_obj, unsigned int flags, SPropProblemArray **) override;
	virtual HRESULT GetProps(const SPropTagArray *, unsigned int flags, unsigned int *nprops, SPropValue **) override;
	virtual HRESULT SetProps(unsigned int nvals, const SPropValue *, SPropProblemArray **) override;
	virtual HRESULT DeleteProps(const SPropTagArray *, SPropProblemArray **) override;

	// We override from IMAPIContainer
	virtual HRESULT SetSearchCriteria(const SRestriction *, const ENTRYLIST *container, ULONG flags) override;
	virtual HRESULT GetSearchCriteria(ULONG ulFlags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState);

	virtual HRESULT CreateMessage(LPCIID lpInterface, ULONG ulFlags, LPMESSAGE *lppMessage);
	virtual HRESULT CreateMessageWithEntryID(const IID *intf, ULONG flags, ULONG eid_size, const ENTRYID *eid, IMessage **);
	virtual HRESULT CopyMessages(LPENTRYLIST lpMsgList, LPCIID lpInterface, LPVOID lpDestFolder, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags);
	virtual HRESULT DeleteMessages(LPENTRYLIST lpMsgList, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags);
	virtual HRESULT CreateFolder(ULONG folder_type, const TCHAR *name, const TCHAR *comment, const IID *intf, ULONG flags, IMAPIFolder **) override;
	virtual HRESULT create_folders(std::vector<ECFolder> &folders);
	virtual HRESULT CopyFolder(ULONG eid_size, const ENTRYID *eid, const IID *intf, void *dst_fld, const TCHAR *newname, ULONG_PTR ui_param, IMAPIProgress *, ULONG flags);
	virtual HRESULT DeleteFolder(ULONG eid_size, const ENTRYID *, ULONG ui_param, IMAPIProgress *, ULONG flags) override;
	virtual HRESULT SetReadFlags(LPENTRYLIST lpMsgList, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags);
	virtual HRESULT GetMessageStatus(ULONG eid_size, const ENTRYID *, ULONG flags, ULONG *status) override;
	virtual HRESULT SetMessageStatus(ULONG eid_size, const ENTRYID *, ULONG new_status, ULONG stmask, ULONG *old_status) override;
	virtual HRESULT SaveContentsSort(const SSortOrderSet *lpSortCriteria, ULONG ulFlags);
	virtual HRESULT EmptyFolder(ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags);

	// Override IFolderSupport
	virtual HRESULT GetSupportMask(DWORD * pdwSupportMask);

	// Override genericprops
	virtual HRESULT SetEntryId(ULONG eid_size, const ENTRYID *eid);
	virtual HRESULT HrSetPropStorage(IECPropStorage *lpStorage, BOOL fLoadProps);

	// Streaming support
	virtual HRESULT CreateMessageFromStream(ULONG flags, ULONG sync_id, ULONG eid_size, const ENTRYID *eid, WSMessageStreamImporter **);
	virtual HRESULT GetChangeInfo(ULONG eid_size, const ENTRYID *eid, SPropValue **pcl, SPropValue **ck);
	virtual HRESULT UpdateMessageFromStream(ULONG sync_id, ULONG eid_size, const ENTRYID *eid, const SPropValue *conflict, WSMessageStreamImporter **);

protected:
	HRESULT CopyFolder2(unsigned int eid_size, const ENTRYID *eid, const IID *intf, void *dst_fld, const TCHAR *newname, ULONG_PTR ui_param, IMAPIProgress *, unsigned int flags, bool is_public);
	HRESULT CopyMessages2(unsigned int ftype, ENTRYLIST *, const IID *intf, void *dst_fld, unsigned int ui_param, IMAPIProgress *, unsigned int flags);

	KC::object_ptr<IMAPIAdviseSink> m_lpFolderAdviseSink;
	KC::object_ptr<WSMAPIFolderOps> lpFolderOps;
	ULONG m_ulConnection = 0;

	private:
	bool m_transact = false;

	friend class		ECExchangeImportHierarchyChanges;	// Allowed access to lpFolderOps
	ALLOC_WRAP_FRIEND;
};

#endif // ECMAPIFOLDER_H
