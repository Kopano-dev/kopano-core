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

// ECMAPIContainer.h: interface for the ECMAPIContainer class.
//
//////////////////////////////////////////////////////////////////////

#ifndef ECMAPICONTAINER
#define ECMAPICONTAINER

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include "WSTransport.h"
#include "ECMsgStore.h"
#include "ECMAPIProp.h"

class ECMAPIContainer : public ECMAPIProp, public virtual IMAPIContainer {
public:
	ECMAPIContainer(ECMsgStore *lpMsgStore, ULONG ulObjType, BOOL fModify, const char *szClassName);
	virtual ~ECMAPIContainer(void) = default;

	// IUnknown
	virtual HRESULT	QueryInterface(REFIID refiid, void **lppInterface) _kc_override;

	// IMAPIContainer
	virtual HRESULT GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	virtual HRESULT SetSearchCriteria(const SRestriction *, const ENTRYLIST *container, ULONG flags) override;
	virtual HRESULT GetSearchCriteria(ULONG ulFlags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState);

	// IMAPIProp
	virtual HRESULT CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, const SPropTagArray *lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
	virtual HRESULT CopyProps(const SPropTagArray *lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
};

#endif // #ifndef ECMAPICONTAINER
