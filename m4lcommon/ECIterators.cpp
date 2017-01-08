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

#include <kopano/platform.h>
#include "ECIterators.h"
#include <kopano/ECRestriction.h>
#include "HrException.h"

ECHierarchyIteratorBase::ECHierarchyIteratorBase(LPMAPICONTAINER lpContainer, ULONG ulFlags, ULONG ulDepth)
: m_ptrContainer(lpContainer, true)
, m_ulFlags(ulFlags)
, m_ulDepth(ulDepth)
, m_ulRowIndex(0)
{
	increment();
}

void ECHierarchyIteratorBase::increment()
{
	HRESULT hr = hrSuccess;
	ULONG ulType;

	enum {IDX_ENTRYID};

	if (!m_ptrTable) {
		SPropValuePtr ptrFolderType;

		SizedSPropTagArray(1, sptaColumnProps) = {1, {PR_ENTRYID}};

		hr = HrGetOneProp(m_ptrContainer, PR_FOLDER_TYPE, &ptrFolderType);
		if (hr == hrSuccess && ptrFolderType->Value.ul == FOLDER_SEARCH) {
			// No point in processing search folders
			m_ptrCurrent.reset();
			goto exit;
		}			

		hr = m_ptrContainer->GetHierarchyTable(m_ulDepth == 1 ? 0 : CONVENIENT_DEPTH, &m_ptrTable);
		if (hr != hrSuccess)
			goto exit;

		if (m_ulDepth > 1) {
			SPropValue sPropDepth;
			ECPropertyRestriction res(RELOP_LE, PR_DEPTH, &sPropDepth, ECRestriction::Cheap);
			SRestrictionPtr ptrRes;

			sPropDepth.ulPropTag = PR_DEPTH;
			sPropDepth.Value.ul = m_ulDepth;

			hr = res.CreateMAPIRestriction(&ptrRes, ECRestriction::Cheap);
			if (hr != hrSuccess)
				goto exit;

			hr = m_ptrTable->Restrict(ptrRes, TBL_BATCH);
			if (hr != hrSuccess)
				goto exit;
		}

		hr = m_ptrTable->SetColumns((LPSPropTagArray)&sptaColumnProps, TBL_BATCH);
		if (hr != hrSuccess)
			goto exit;
	}

	if (!m_ptrRows.get()) {
		hr = m_ptrTable->QueryRows(32, 0, &m_ptrRows);
		if (hr != hrSuccess)
			goto exit;

		if (m_ptrRows.empty()) {
			m_ptrCurrent.reset();
			goto exit;
		}

		m_ulRowIndex = 0;
	}

	ASSERT(m_ulRowIndex < m_ptrRows.size());
	hr = m_ptrContainer->OpenEntry(m_ptrRows[m_ulRowIndex].lpProps[IDX_ENTRYID].Value.bin.cb, (LPENTRYID)m_ptrRows[m_ulRowIndex].lpProps[IDX_ENTRYID].Value.bin.lpb, &m_ptrCurrent.iid, m_ulFlags, &ulType, &m_ptrCurrent);
	if (hr != hrSuccess)
		goto exit;

	if (++m_ulRowIndex == m_ptrRows.size())
		m_ptrRows.reset();

exit:
	if (hr != hrSuccess)
		throw HrException(hr);	// @todo: Fix this
}
