/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include "ECIterators.h"
#include <kopano/ECRestriction.h>
#include <kopano/hl.hpp>

namespace KC {

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
	ULONG ulType;

	enum {IDX_ENTRYID};

	if (!m_ptrTable) {
		SPropValuePtr ptrFolderType;
		static constexpr const SizedSPropTagArray(1, sptaColumnProps) = {1, {PR_ENTRYID}};

		auto hr = HrGetOneProp(m_ptrContainer, PR_FOLDER_TYPE, &~ptrFolderType);
		if (hr == hrSuccess && ptrFolderType->Value.ul == FOLDER_SEARCH) {
			// No point in processing search folders
			m_ptrCurrent.reset();
			return;
		}			
		hr = m_ptrContainer->GetHierarchyTable(m_ulDepth == 1 ? 0 : CONVENIENT_DEPTH, &~m_ptrTable);
		if (hr != hrSuccess)
			throw KMAPIError(hr);

		if (m_ulDepth > 1) {
			SPropValue sPropDepth;

			sPropDepth.ulPropTag = PR_DEPTH;
			sPropDepth.Value.ul = m_ulDepth;
			hr = ECPropertyRestriction(RELOP_LE, PR_DEPTH, &sPropDepth, ECRestriction::Cheap)
			     .RestrictTable(m_ptrTable, TBL_BATCH);
			if (hr != hrSuccess)
				throw KMAPIError(hr);
		}
		hr = m_ptrTable->SetColumns(sptaColumnProps, TBL_BATCH);
		if (hr != hrSuccess)
			throw KMAPIError(hr);
	}

	if (!m_ptrRows.get()) {
		auto hr = m_ptrTable->QueryRows(32, 0, &~m_ptrRows);
		if (hr != hrSuccess)
			throw KMAPIError(hr);
		if (m_ptrRows.empty()) {
			m_ptrCurrent.reset();
			return;
		}

		m_ulRowIndex = 0;
	}

	assert(m_ulRowIndex < m_ptrRows.size());
	auto hr = m_ptrContainer->OpenEntry(m_ptrRows[m_ulRowIndex].lpProps[IDX_ENTRYID].Value.bin.cb,
	          reinterpret_cast<ENTRYID *>(m_ptrRows[m_ulRowIndex].lpProps[IDX_ENTRYID].Value.bin.lpb),
	          &iid_of(m_ptrCurrent), m_ulFlags, &ulType, &~m_ptrCurrent);
	if (hr != hrSuccess)
		throw KMAPIError(hr);
	if (++m_ulRowIndex == m_ptrRows.size())
		m_ptrRows.reset();
}

} /* namespace */
