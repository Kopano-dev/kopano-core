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

#ifndef ECMAPIFOLDER_H
#define ECMAPIFOLDER_H

#include <kopano/memory.hpp>
#include <kopano/zcdefs.h>
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
	static HRESULT Create(ECMsgStore *lpMsgStore, BOOL fModify, WSMAPIFolderOps *lpFolderOps, ECMAPIFolder **lppECMAPIFolder);

	static HRESULT	GetPropHandler(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);	
	static HRESULT SetPropHandler(ULONG ulPropTag, void *lpProvider, const SPropValue *lpsPropValue, void *lpParam);

	// Our table-row getprop handler (handles client-side generation of table columns)
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);
	virtual HRESULT	QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk);

	// Override IMAPIProp
	virtual HRESULT SaveChanges(ULONG ulFlags) _kc_override;
	virtual HRESULT CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, const SPropTagArray *lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) _kc_override;
	virtual HRESULT CopyProps(const SPropTagArray *lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) _kc_override;
	virtual HRESULT GetProps(const SPropTagArray *lpPropTagArray, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppPropArray) _kc_override;
	virtual HRESULT SetProps(ULONG cValues, const SPropValue *lpPropArray, LPSPropProblemArray *lppProblems) _kc_override;
	virtual HRESULT DeleteProps(const SPropTagArray *lpPropTagArray, LPSPropProblemArray *lppProblems) _kc_override;

	// We override from IMAPIContainer
	virtual HRESULT SetSearchCriteria(LPSRestriction lpRestriction, LPENTRYLIST lpContainerList, ULONG ulSearchFlags);
	virtual HRESULT GetSearchCriteria(ULONG ulFlags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState);

	virtual HRESULT CreateMessage(LPCIID lpInterface, ULONG ulFlags, LPMESSAGE *lppMessage);
	virtual HRESULT CreateMessageWithEntryID(const IID *intf, ULONG flags, ULONG eid_size, const ENTRYID *eid, IMessage **);
	virtual HRESULT CopyMessages(LPENTRYLIST lpMsgList, LPCIID lpInterface, LPVOID lpDestFolder, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags);
	virtual HRESULT DeleteMessages(LPENTRYLIST lpMsgList, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags);
	virtual HRESULT CreateFolder(ULONG folder_type, const TCHAR *name, const TCHAR *comment, const IID *intf, ULONG flags, IMAPIFolder **) override;
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
	KC::object_ptr<IMAPIAdviseSink> m_lpFolderAdviseSink;
	KC::object_ptr<WSMAPIFolderOps> lpFolderOps;
	ULONG m_ulConnection = 0;

	friend class		ECExchangeImportHierarchyChanges;	// Allowed access to lpFolderOps
	ALLOC_WRAP_FRIEND;
};



#endif // ECMAPIFOLDER_H
