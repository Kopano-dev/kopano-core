/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <memory>
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

/**
 * Subclass of DataCollector that is used to get the current state
 * through the MailboxTable.
 */
class MailboxDataCollector final : public DataCollector {
public:
	MailboxDataCollector(ArchiveStateCollector::ArchiveInfoMap &mapArchiveInfo, std::shared_ptr<ECLogger>);
	HRESULT GetRequiredPropTags(IMAPIProp *, SPropTagArray **) const override;
	HRESULT CollectData(IMAPITable *store_table) override;

private:
	ArchiveStateCollector::ArchiveInfoMap &m_mapArchiveInfo;
	std::shared_ptr<ECLogger> m_lpLogger;
};

MailboxDataCollector::MailboxDataCollector(ArchiveStateCollector::ArchiveInfoMap &mapArchiveInfo,
     std::shared_ptr<ECLogger> lpLogger) :
	m_mapArchiveInfo(mapArchiveInfo), m_lpLogger(std::move(lpLogger))
{
}

HRESULT MailboxDataCollector::GetRequiredPropTags(LPMAPIPROP lpProp, LPSPropTagArray *lppPropTagArray) const
{
	SPropTagArrayPtr ptrPropTagArray;

	PROPMAP_START(2)
		PROPMAP_NAMED_ID(STORE_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, "store-entryids")
		PROPMAP_NAMED_ID(ITEM_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, "item-entryids")
	PROPMAP_INIT(lpProp);

	auto hr = MAPIAllocateBuffer(CbNewSPropTagArray(4), &~ptrPropTagArray);
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
			const auto &prop = ptrRows[i].lpProps;
			bool bComplete = true;
			abentryid_t userId;

			for (unsigned j = 0; bComplete && j < IDX_MAX; ++j) {
				if (PROP_TYPE(prop[j].ulPropTag) == PT_ERROR) {
					m_lpLogger->logf(EC_LOGLEVEL_WARNING, "Got incomplete row, row %u, column %u contains error \"%s\" (%x)",
						i, j, GetMAPIErrorMessage(prop[j].Value.err), prop[j].Value.err);
					bComplete = false;
				}
			}
			if (!bComplete)
				continue;
			if (prop[IDX_STORE_ENTRYIDS].Value.MVbin.cValues != prop[IDX_ITEM_ENTRYIDS].Value.MVbin.cValues) {
				m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Mismatch in archive prop count, %u vs. %u", prop[IDX_STORE_ENTRYIDS].Value.MVbin.cValues, prop[IDX_ITEM_ENTRYIDS].Value.MVbin.cValues);
				continue;
			}
			userId = prop[IDX_MAILBOX_OWNER_ENTRYID].Value.bin;
			auto res = m_mapArchiveInfo.emplace(userId, ArchiveStateCollector::ArchiveInfo());
			if (res.second)
				m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Inserting row for user id \"%s\"", userId.tostring().c_str());
			else
				m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Updating row for user \"" TSTRING_PRINTF "\"", res.first->second.userName.c_str());

			// Assign entryid
			res.first->second.storeId = prop[IDX_ENTRYID].Value.bin;

			// Assign archives
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Adding %u archive(s)", prop[IDX_STORE_ENTRYIDS].Value.MVbin.cValues);
			for (ULONG j = 0; j < prop[IDX_STORE_ENTRYIDS].Value.MVbin.cValues; ++j) {
				SObjectEntry objEntry;
				objEntry.sStoreEntryId = prop[IDX_STORE_ENTRYIDS].Value.MVbin.lpbin[j];
				objEntry.sItemEntryId = prop[IDX_ITEM_ENTRYIDS].Value.MVbin.lpbin[j];
				res.first->second.lstArchives.emplace_back(std::move(objEntry));
			}
		}
	}
	return hrSuccess;
}

/**
 * Create an ArchiveStateCollector instance.
 * @param[in]	ArchiverSessionPtr		The archive session
 * @param[in]	lpLogger		The logger.
 * @param[out]	lpptrCollector	The new ArchiveStateCollector instance.
 */
HRESULT ArchiveStateCollector::Create(const ArchiverSessionPtr &ptrSession,
    std::shared_ptr<ECLogger> lpLogger, ArchiveStateCollectorPtr *lpptrCollector)
{
	ArchiveStateCollectorPtr ptrCollector(
		new(std::nothrow) ArchiveStateCollector(ptrSession, std::move(lpLogger)));
	if (ptrCollector == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	*lpptrCollector = std::move(ptrCollector);
	return hrSuccess;
}

/**
 * @param[in]	ArchiverSessionPtr		The archive session
 * @param[in]	lpLogger		The logger.
 */
ArchiveStateCollector::ArchiveStateCollector(const ArchiverSessionPtr &ptrSession,
    std::shared_ptr<ECLogger> lpLogger) :
	m_ptrSession(ptrSession), m_lpLogger(new ECArchiverLogger(std::move(lpLogger)))
{ }

/**
 * Return an ArchiveStateUpdater instance that can update the current state
 * to the required state.
 * @param[out]	lpptrUpdate		The new ArchiveStateUpdater instance.
 */
HRESULT ArchiveStateCollector::GetArchiveStateUpdater(ArchiveStateUpdaterPtr *lpptrUpdater)
{
	MailboxDataCollector mdc(m_mapArchiveInfo, m_lpLogger);
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
	object_ptr<IABContainer> ptrABContainer;
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
		m_lpLogger->logf(EC_LOGLEVEL_CRIT, "Failed to iterate addressbook containers: %s (%x)",
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
	SPropValue sPropObjType, sPropDispType;
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
			const auto &prop = ptrRows[i].lpProps;
			if (prop[IDX_ENTRYID].ulPropTag != PR_ENTRYID) {
				auto err = prop[IDX_ACCOUNT].Value.err;
				m_lpLogger->perr("Unable to get entryid from address list", err);
				continue;
			}
			if (prop[IDX_ACCOUNT].ulPropTag != PR_ACCOUNT) {
				auto err = prop[IDX_ACCOUNT].Value.err;
				m_lpLogger->perr("Unable to get username from address list", err);
				continue;
			}
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Inserting row for user \"" TSTRING_PRINTF "\"", prop[IDX_ACCOUNT].Value.LPSZ);
			auto iterator = m_mapArchiveInfo.emplace(abentryid_t(prop[IDX_ENTRYID].Value.bin), ArchiveInfo()).first;
			iterator->second.userName = prop[IDX_ACCOUNT].Value.LPSZ;
			if (prop[IDX_EC_ARCHIVE_SERVERS].ulPropTag == PR_EC_ARCHIVE_SERVERS) {
				m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Adding %u archive server(s)", prop[IDX_EC_ARCHIVE_SERVERS].Value.MVSZ.cValues);
				for (ULONG j = 0; j < prop[IDX_EC_ARCHIVE_SERVERS].Value.MVSZ.cValues; ++j)
					iterator->second.lstServers.emplace_back(prop[IDX_EC_ARCHIVE_SERVERS].Value.MVSZ.LPPSZ[j]);
			}
			if (prop[IDX_EC_ARCHIVE_COUPLINGS].ulPropTag == PR_EC_ARCHIVE_COUPLINGS) {
				m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Adding %u archive coupling(s)", prop[IDX_EC_ARCHIVE_COUPLINGS].Value.MVSZ.cValues);
				for (ULONG j = 0; j < prop[IDX_EC_ARCHIVE_COUPLINGS].Value.MVSZ.cValues; ++j)
					iterator->second.lstCouplings.emplace_back(prop[IDX_EC_ARCHIVE_COUPLINGS].Value.MVSZ.LPPSZ[j]);
			}
		}
	}
	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "IABContainer scan yielded %zu users", m_mapArchiveInfo.size());
	return hrSuccess;
}

} /* namespace */
