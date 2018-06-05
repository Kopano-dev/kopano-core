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

#include <kopano/zcdefs.h>
#include <new>
#include <utility>
#include <kopano/memory.hpp>
#include <kopano/platform.h>
#include <kopano/userutil.h>
#include "ArchiveStateCollector.h"
#include <kopano/CommonUtil.h>
#include <kopano/MAPIErrors.h>
#include "ArchiverSession.h"
#include "ArchiveStateUpdater.h"
#include "ECIterators.h"
#include <kopano/hl.hpp>
#include <kopano/ECRestriction.h>

namespace KC {

namespace details {

	/**
	 * Subclass of DataCollector that is used to get the current state
	 * through the MailboxTable.
	 */
	class MailboxDataCollector _kc_final : public DataCollector {
	public:
		MailboxDataCollector(ArchiveStateCollector::ArchiveInfoMap &mapArchiveInfo, ECLogger *lpLogger);
		HRESULT GetRequiredPropTags(LPMAPIPROP lpProp, LPSPropTagArray *lppPropTagArray) const _kc_override;
		HRESULT CollectData(LPMAPITABLE lpStoreTable) _kc_override;

	private:
		ArchiveStateCollector::ArchiveInfoMap &m_mapArchiveInfo;
		object_ptr<ECLogger> m_lpLogger;
	};

	MailboxDataCollector::MailboxDataCollector(ArchiveStateCollector::ArchiveInfoMap &mapArchiveInfo, ECLogger *lpLogger): m_mapArchiveInfo(mapArchiveInfo), m_lpLogger(lpLogger)
	{
	}

	HRESULT MailboxDataCollector::GetRequiredPropTags(LPMAPIPROP lpProp, LPSPropTagArray *lppPropTagArray) const
	{
		HRESULT hr = hrSuccess;
		SPropTagArrayPtr ptrPropTagArray;

		PROPMAP_START(2)
			PROPMAP_NAMED_ID(STORE_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, "store-entryids")
			PROPMAP_NAMED_ID(ITEM_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, "item-entryids")
		PROPMAP_INIT(lpProp);

		hr = MAPIAllocateBuffer(CbNewSPropTagArray(4), &~ptrPropTagArray);
		if (hr != hrSuccess)
			return hr;

		ptrPropTagArray->cValues = 4;
		ptrPropTagArray->aulPropTag[0] = PR_ENTRYID;
		ptrPropTagArray->aulPropTag[1] = PR_MAILBOX_OWNER_ENTRYID;
		ptrPropTagArray->aulPropTag[2] = PROP_STORE_ENTRYIDS;
		ptrPropTagArray->aulPropTag[3] = PROP_ITEM_ENTRYIDS;

		*lppPropTagArray = ptrPropTagArray.release();
		return hr;
	}

	HRESULT MailboxDataCollector::CollectData(LPMAPITABLE lpStoreTable)
	{
		enum {IDX_ENTRYID, IDX_MAILBOX_OWNER_ENTRYID, IDX_STORE_ENTRYIDS, IDX_ITEM_ENTRYIDS, IDX_MAX};

		while (true) {
			SRowSetPtr ptrRows;
			auto hr = lpStoreTable->QueryRows(50, 0, &~ptrRows);
			if (hr != hrSuccess)
				return hr;

			if (ptrRows.size() == 0)
				break;

			for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
				bool bComplete = true;
				abentryid_t userId;

				for (unsigned j = 0; bComplete && j < IDX_MAX; ++j) {
					if (PROP_TYPE(ptrRows[i].lpProps[j].ulPropTag) == PT_ERROR) {
						m_lpLogger->logf(EC_LOGLEVEL_WARNING, "Got incomplete row, row %u, column %u contains error \"%s\" (%x)",
							i, j, GetMAPIErrorMessage(ptrRows[i].lpProps[j].Value.err), ptrRows[i].lpProps[j].Value.err);
						bComplete = false;
					}
				}
						
				if (!bComplete)
					continue;

				if (ptrRows[i].lpProps[IDX_STORE_ENTRYIDS].Value.MVbin.cValues != ptrRows[i].lpProps[IDX_ITEM_ENTRYIDS].Value.MVbin.cValues) {
					m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Mismatch in archive prop count, %u vs. %u", ptrRows[i].lpProps[IDX_STORE_ENTRYIDS].Value.MVbin.cValues, ptrRows[i].lpProps[IDX_ITEM_ENTRYIDS].Value.MVbin.cValues);
					continue;
				}
				userId = ptrRows[i].lpProps[IDX_MAILBOX_OWNER_ENTRYID].Value.bin;
				auto res = m_mapArchiveInfo.emplace(userId, ArchiveStateCollector::ArchiveInfo());
				if (res.second == true)
					m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Inserting row for user id \"%s\"", userId.tostring().c_str());
				else
					m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Updating row for user \"" TSTRING_PRINTF "\"", res.first->second.userName.c_str());

				// Assign entryid
				res.first->second.storeId = ptrRows[i].lpProps[IDX_ENTRYID].Value.bin;

				// Assign archives
				m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Adding %u archive(s)", ptrRows[i].lpProps[IDX_STORE_ENTRYIDS].Value.MVbin.cValues);
				for (ULONG j = 0; j < ptrRows[i].lpProps[IDX_STORE_ENTRYIDS].Value.MVbin.cValues; ++j) {
					SObjectEntry objEntry;
					objEntry.sStoreEntryId = ptrRows[i].lpProps[IDX_STORE_ENTRYIDS].Value.MVbin.lpbin[j];
					objEntry.sItemEntryId = ptrRows[i].lpProps[IDX_ITEM_ENTRYIDS].Value.MVbin.lpbin[j];
					res.first->second.lstArchives.emplace_back(std::move(objEntry));
				}
			}
		}
		return hrSuccess;
	}

}

/**
 * Create an ArchiveStateCollector instance.
 * @param[in]	ArchiverSessionPtr		The archive session
 * @param[in]	lpLogger		The logger.
 * @param[out]	lpptrCollector	The new ArchiveStateCollector instance.
 */
HRESULT ArchiveStateCollector::Create(const ArchiverSessionPtr &ptrSession, ECLogger *lpLogger, ArchiveStateCollectorPtr *lpptrCollector)
{
	ArchiveStateCollectorPtr ptrCollector(
		new(std::nothrow) ArchiveStateCollector(ptrSession, lpLogger));
	if (ptrCollector == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	*lpptrCollector = std::move(ptrCollector);
	return hrSuccess;
}

/**
 * @param[in]	ArchiverSessionPtr		The archive session
 * @param[in]	lpLogger		The logger.
 */
ArchiveStateCollector::ArchiveStateCollector(const ArchiverSessionPtr &ptrSession, ECLogger *lpLogger)
: m_ptrSession(ptrSession)
, m_lpLogger(new ECArchiverLogger(lpLogger), false)
{ }

/**
 * Return an ArchiveStateUpdater instance that can update the current state
 * to the required state.
 * @param[out]	lpptrUpdate		The new ArchiveStateUpdater instance.
 */
HRESULT ArchiveStateCollector::GetArchiveStateUpdater(ArchiveStateUpdaterPtr *lpptrUpdater)
{
	details::MailboxDataCollector mdc(m_mapArchiveInfo, m_lpLogger);
	auto hr = PopulateUserList();
	if (hr != hrSuccess)
		return hr;

	hr = GetMailboxData(m_ptrSession->GetMAPISession(),
	     m_ptrSession->GetSSLPath(), m_ptrSession->GetSSLPass(),
	     false, &mdc);
	if (hr != hrSuccess) {
		m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Failed to get mailbox data: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	hr = ArchiveStateUpdater::Create(m_ptrSession, m_lpLogger, m_mapArchiveInfo, lpptrUpdater);
	if (hr != hrSuccess)
		return hr;

	m_mapArchiveInfo.clear();
	return hrSuccess;
}

/**
 * Populate the user list through the GAL.
 * When this method completes, a list will be available of all users that
 * should have one or more archives attached to their primary store.
 */
HRESULT ArchiveStateCollector::PopulateUserList()
{
	ABContainerPtr ptrABContainer;

	auto hr = m_ptrSession->GetGAL(&~ptrABContainer);
	if (hr != hrSuccess)
		return hr;
	hr = PopulateFromContainer(ptrABContainer);
	if (hr != hrSuccess)
		return hr;

	try {
		for (ECABContainerIterator iter(ptrABContainer, 0); iter != ECABContainerIterator(); ++iter) {
			hr = PopulateFromContainer(*iter);
			if (hr != hrSuccess)
				return hr;
		}
	} catch (const KMAPIError &e) {
		hr = e.code();
		m_lpLogger->logf(EC_LOGLEVEL_FATAL, "Failed to iterate addressbook containers: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	return hrSuccess;
}

/**
 * Populate the user list through one AB container.
 * When this method completes, the userlist will be available for all users
 * from the passed container that should have one or more archives attached to
 * their primary store.
 * @param[in]	lpContainer		The addressbook container to process.
 */
HRESULT ArchiveStateCollector::PopulateFromContainer(LPABCONT lpContainer)
{
	SPropValue sPropObjType;
	SPropValue sPropDispType;
	MAPITablePtr ptrTable;
	SRowSetPtr ptrRows;
	static constexpr const SizedSPropTagArray(4, sptaUserProps) =
		{4, {PR_ENTRYID, PR_ACCOUNT, PR_EC_ARCHIVE_SERVERS,
		PR_EC_ARCHIVE_COUPLINGS}};
	enum {IDX_ENTRYID, IDX_ACCOUNT, IDX_EC_ARCHIVE_SERVERS, IDX_EC_ARCHIVE_COUPLINGS};


	sPropObjType.ulPropTag = PR_OBJECT_TYPE;
	sPropObjType.Value.ul = MAPI_MAILUSER;
	sPropDispType.ulPropTag = PR_DISPLAY_TYPE;
	sPropDispType.Value.ul = DT_MAILUSER;;

	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Scanning IABContainer for users with archives");
	auto hr = lpContainer->GetContentsTable(0, &~ptrTable);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->SetColumns(sptaUserProps, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ECAndRestriction(
		ECPropertyRestriction(RELOP_EQ, PR_OBJECT_TYPE, &sPropObjType, ECRestriction::Cheap) +
		ECPropertyRestriction(RELOP_EQ, PR_DISPLAY_TYPE, &sPropDispType, ECRestriction::Cheap) +
		ECOrRestriction(
			ECExistRestriction(PR_EC_ARCHIVE_SERVERS) +
			ECExistRestriction(PR_EC_ARCHIVE_COUPLINGS)
		)
	).RestrictTable(ptrTable, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;

	while (true) {
		hr = ptrTable->QueryRows(50, 0, &~ptrRows);
		if (hr != hrSuccess)
			return hr;

		if (ptrRows.size() == 0)
			break;

		for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
			if (ptrRows[i].lpProps[IDX_ENTRYID].ulPropTag != PR_ENTRYID) {
				auto err = ptrRows[i].lpProps[IDX_ACCOUNT].Value.err;
				m_lpLogger->perr("Unable to get entryid from address list", err);
				continue;
			}

			if (ptrRows[i].lpProps[IDX_ACCOUNT].ulPropTag != PR_ACCOUNT) {
				auto err = ptrRows[i].lpProps[IDX_ACCOUNT].Value.err;
				m_lpLogger->perr("Unable to get username from address list", err);
				continue;
			}

			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Inserting row for user \"" TSTRING_PRINTF "\"", ptrRows[i].lpProps[IDX_ACCOUNT].Value.LPSZ);
			auto iterator = m_mapArchiveInfo.emplace(abentryid_t(ptrRows[i].lpProps[IDX_ENTRYID].Value.bin), ArchiveInfo()).first;
			iterator->second.userName.assign(ptrRows[i].lpProps[IDX_ACCOUNT].Value.LPSZ);

			if (ptrRows[i].lpProps[IDX_EC_ARCHIVE_SERVERS].ulPropTag == PR_EC_ARCHIVE_SERVERS) {
				m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Adding %u archive server(s)", ptrRows[i].lpProps[IDX_EC_ARCHIVE_SERVERS].Value.MVSZ.cValues);
				for (ULONG j = 0; j < ptrRows[i].lpProps[IDX_EC_ARCHIVE_SERVERS].Value.MVSZ.cValues; ++j)
					iterator->second.lstServers.emplace_back(ptrRows[i].lpProps[IDX_EC_ARCHIVE_SERVERS].Value.MVSZ.LPPSZ[j]);
			}

			if (ptrRows[i].lpProps[IDX_EC_ARCHIVE_COUPLINGS].ulPropTag == PR_EC_ARCHIVE_COUPLINGS) {
				m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Adding %u archive coupling(s)", ptrRows[i].lpProps[IDX_EC_ARCHIVE_COUPLINGS].Value.MVSZ.cValues);
				for (ULONG j = 0; j < ptrRows[i].lpProps[IDX_EC_ARCHIVE_COUPLINGS].Value.MVSZ.cValues; ++j)
					iterator->second.lstCouplings.emplace_back(ptrRows[i].lpProps[IDX_EC_ARCHIVE_COUPLINGS].Value.MVSZ.LPPSZ[j]);
			}
		}
	}
	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "IABContainer scan yielded %zu users", m_mapArchiveInfo.size());
	return hrSuccess;
}

} /* namespace */
