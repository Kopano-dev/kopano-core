/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <new>
#include <utility>
#include <kopano/MAPIErrors.h>
#include <kopano/archiver-common.h>
#include <kopano/memory.hpp>
#include "ArchiveStateUpdater.h"
#include "ArchiverSession.h"
#include "helpers/StoreHelper.h"
#include "helpers/ArchiveHelper.h"
#include "ArchiveManageImpl.h"

using namespace KC::helpers;

namespace KC {

namespace Predicates {

	/**
	 * Compare two SObjectEntry instances.
	 * This version does a binary compare of the embedded entry ids.
	 */
	class SObjectEntry_equals_binary {
	public:
		SObjectEntry_equals_binary(const SObjectEntry &objEntry): m_objEntry(objEntry) {}
		bool operator()(const SObjectEntry &objEntry) const { return objEntry == m_objEntry; }
	private:
		const SObjectEntry &m_objEntry;
	};

	/**
	 * Compare two SObjectEntry instances.
	 * This method uses CompareEntryIDs to do the comparison.
	 */
	class SObjectEntry_equals_compareEntryId {
	public:
		SObjectEntry_equals_compareEntryId(IMAPISession *lpSession, const SObjectEntry &objEntry): m_lpSession(lpSession), m_objEntry(objEntry) {}
		bool operator()(const SObjectEntry &objEntry) const {
			ULONG ulResult = 0;

			auto hr = m_lpSession->CompareEntryIDs(m_objEntry.sStoreEntryId.size(), m_objEntry.sStoreEntryId, objEntry.sStoreEntryId.size(), objEntry.sStoreEntryId, 0, &ulResult);
			if (hr != hrSuccess || ulResult == 0)
				return false;
			hr = m_lpSession->CompareEntryIDs(m_objEntry.sItemEntryId.size(), m_objEntry.sItemEntryId, objEntry.sItemEntryId.size(), objEntry.sItemEntryId, 0, &ulResult);
			return (hr == hrSuccess && ulResult == 1);
		}
	private:
		IMAPISession *m_lpSession;
		const SObjectEntry &m_objEntry;
	};
} // namespace Predicates

/**
 * Create an ArchiveStateUpdater instance.
 * @param[in]	ptrSession		The archiver session.
 * @param[in]	lpLogger		The logger.
 * @param[in]	mapArchiveInfo	The map containing the users that have and/or
 * 								should have an archive attached to their
 * 								primary store.
 * @param[out]	lpptrUpdater	The new ArchiveStateUpdater instance
 */
HRESULT ArchiveStateUpdater::Create(const ArchiverSessionPtr &ptrSession, ECLogger *lpLogger, const ArchiveInfoMap &mapArchiveInfo, ArchiveStateUpdaterPtr *lpptrUpdater)
{
	ArchiveStateUpdaterPtr ptrUpdater(
		new(std::nothrow) ArchiveStateUpdater(ptrSession, lpLogger,
		mapArchiveInfo));
	if (ptrUpdater == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	*lpptrUpdater = std::move(ptrUpdater);
	return hrSuccess;
}

/**
 * @param[in]	ptrSession		The archiver session.
 * @param[in]	lpLogger		The logger.
 * @param[in]	mapArchiveInfo	The map containing the users that have and/or
 * 								should have an archive attached to their
 * 								primary store.
 */
ArchiveStateUpdater::ArchiveStateUpdater(const ArchiverSessionPtr &ptrSession,
    ECLogger *lpLogger, const ArchiveInfoMap &mapArchiveInfo) :
	m_ptrSession(ptrSession), m_lpLogger(lpLogger),
	m_mapArchiveInfo(mapArchiveInfo)
{
	if (m_lpLogger == nullptr)
		m_lpLogger.reset(new ECLogger_Null, false);
}

/**
 * Update all users to the required state.
 */
HRESULT ArchiveStateUpdater::UpdateAll(unsigned int ulAttachFlags)
{
    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveStateUpdater::UpdateAll() function entry");

	for (const auto &i : m_mapArchiveInfo) {
		HRESULT hrTmp = UpdateOne(i.first, i.second, ulAttachFlags);
		if (hrTmp != hrSuccess)
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to auto attach store for user \"" TSTRING_PRINTF "\": %s (%x)",
				i.second.userName.c_str(), GetMAPIErrorMessage(hrTmp), hrTmp);
	}
	return hrSuccess;
}

/**
 * Update a single user to the required state.
 * @param[in]	userName	The username of the user to update.
 */
HRESULT ArchiveStateUpdater::Update(const tstring &userName, unsigned int ulAttachFlags)
{
    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveStateUpdater::Update(): function entry");

	// First see if the username can be found in the map.
	auto i = std::find_if(m_mapArchiveInfo.cbegin(),
	         m_mapArchiveInfo.cend(),
	         [&](const decltype(m_mapArchiveInfo)::value_type &p) { return userName.compare(p.second.userName) == 0; });
	if (i == m_mapArchiveInfo.end()) {
		// Resolve the username and search by entryid.
		abentryid_t userId;

		m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Archive map has no entry for user \"" TSTRING_PRINTF "\", trying to resolve.", userName.c_str());
		auto hr = m_ptrSession->GetUserInfo(userName, &userId, NULL, NULL);
		if (hr != hrSuccess)
			return hr;

		i = m_mapArchiveInfo.find(userId);
		if (i == m_mapArchiveInfo.end()) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Archive map has no entry for user entryid \"%s\".", userId.tostring().c_str());
			return MAPI_E_NOT_FOUND;
		}
	}

    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveStateUpdater::Update(): about to call UpdateOne()");
	auto hr = UpdateOne(i->first, i->second, ulAttachFlags);
	if (hr != hrSuccess)
		m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to auto attach store for user \"" TSTRING_PRINTF "\": %s (%x)",
			userName.c_str(), GetMAPIErrorMessage(hr), hr);
	return hr;
}

/**
 * Update one single user.
 * @param[in]	userId		The entryid of the user to update.
 * @param[in[	info		The ArchiveInfo object containing the current and
 * 							required state.
 */
HRESULT ArchiveStateUpdater::UpdateOne(const abentryid_t &userId, const ArchiveInfo& info, unsigned int ulAttachFlags)
{
	HRESULT hr = hrSuccess;

    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveStateUpdater::UpdateOne() function entry");
	if (info.userName.empty()) {
		// Found a store that has archives attached but no archive- servers or couplings
		// are defined in the GAB.
        m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveStateUpdater::UpdateOne() about to call RemoveImplicit()");
		hr = RemoveImplicit(info.storeId, tstring(), userId, info.lstArchives);
	}
	else if (info.storeId.empty()) {
		// Found a user in the GAB that has at least one archive- server or coupling
		// defined but has no archives attached.
        m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveStateUpdater::UpdateOne() about to call AddCouplingBased()");
		hr = AddCouplingBased(info.userName, info.lstCouplings, ulAttachFlags);
		if (hr == hrSuccess)
        {
            m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveStateUpdater::UpdateOne() about to call AddServerBased()");
			hr = AddServerBased(info.userName, userId, info.lstServers, ulAttachFlags);
        }
	}

	else {
        m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveStateUpdater::UpdateOne() about to call VerifyAndUpdate()");
		hr = VerifyAndUpdate(userId, info, ulAttachFlags);
	}

return hr;
}

/**
 * Remove/detach all implicit attached archives
 * @param[in]	storeId		The entryid of the primary store to process.
 * @param[in]	userName	The name of the user owning the store to process. This
 * 							is an alternative way of finding the store if
 * 							storeId is unwrapped.
 * @param[in]	userId		The entryid of the user owning the store to process.
 * 							This is an alternative way of finding the store if
 * 							storeId is unwrapped and userName is unknown.
 * @param[in]	lstArchives	The list of archives to remove the implicit attached
 * 							archives from.
 */
HRESULT ArchiveStateUpdater::RemoveImplicit(const entryid_t &storeId, const tstring &userName, const abentryid_t &userId, const ObjectEntryList &lstArchives)
{
	MsgStorePtr ptrUserStore;
	StoreHelperPtr ptrUserStoreHelper;
	ObjectEntryList lstCurrentArchives;
	ULONG ulDetachCount = 0;

	m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Removing implicitly attached archives.");
	auto hr = m_ptrSession->OpenStore(storeId, &~ptrUserStore);
	if (hr == MAPI_E_INVALID_ENTRYID) {
		m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Got invalid entryid, attempting to resolve...");

		// The storeId was obtained from the MailboxTable that currently does not return
		if (!userName.empty()) {
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Resolving user \"" TSTRING_PRINTF "\"", userName.c_str());
			hr = m_ptrSession->OpenStoreByName(userName, &~ptrUserStore);
			if (hr != hrSuccess) {
				m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to resolve store for user \"" TSTRING_PRINTF "\"", userName.c_str());
				return hr;
			}
		} else if (userId.size() != 0) {
			tstring strUserName;
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Resolving user id \"%s\"", userId.tostring().c_str());
			hr = m_ptrSession->GetUserInfo(userId, &strUserName, NULL);
			if (hr != hrSuccess) {
				m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to get info for user id \"%s\"", userId.tostring().c_str());
				return hr;
			}
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Resolving user \"" TSTRING_PRINTF "\"", userName.c_str());
			hr = m_ptrSession->OpenStoreByName(strUserName, &~ptrUserStore);
			if (hr != hrSuccess) {
				m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to resolve store for user \"" TSTRING_PRINTF "\"", userName.c_str());
				return hr;
			}
		}
	}
	if (hr != hrSuccess)
		return hr;
	hr = StoreHelper::Create(ptrUserStore, &ptrUserStoreHelper);
	if (hr != hrSuccess)
		return hr;
	hr = ptrUserStoreHelper->GetArchiveList(&lstCurrentArchives);
	if (hr != hrSuccess)
		return hr;

	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Processing %zu archives for implicitly attached archives", lstArchives.size());
	for (const auto &i : lstArchives) {
		MsgStorePtr ptrArchStore;
		ULONG ulType;
		MAPIFolderPtr ptrArchFolder;
		ArchiveHelperPtr ptrArchiveHelper;
		AttachType attachType;

		hr = m_ptrSession->OpenStore(i.sStoreEntryId, &~ptrArchStore);
		if (hr == MAPI_E_NOT_FOUND) {
			m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Archive store returned not found, detaching it.");
			lstCurrentArchives.remove_if(Predicates::SObjectEntry_equals_binary(i));
			continue;
		}
		if (hr != hrSuccess) {
			m_lpLogger->perr("Failed to open archive store", hr);
			return hr;
		}
		hr = ptrArchStore->OpenEntry(i.sItemEntryId.size(), i.sItemEntryId, &iid_of(ptrArchFolder), 0, &ulType, &~ptrArchFolder);
		if (hr != hrSuccess) {
			m_lpLogger->perr("Failed to open archive root", hr);
			if (hr == MAPI_E_NOT_FOUND) {
				m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Possibly invalid entry, skipping...");
				continue;
			}
			return hr;
		}

		hr = ArchiveHelper::Create(ptrArchStore, ptrArchFolder, NULL, &ptrArchiveHelper);
		if (hr != hrSuccess)
			return hr;

		hr = ptrArchiveHelper->GetArchiveType(NULL, &attachType);
		if (hr != hrSuccess) {
			m_lpLogger->perr("Failed to get attachType", hr);
			return hr;
		}

		if (attachType == ImplicitAttach) {
			m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Archive was implicitly attached, detaching.");
			lstCurrentArchives.remove_if(Predicates::SObjectEntry_equals_binary(i));
			++ulDetachCount;
		} else
			m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Archive was explicitly attached");
	}

	if (ulDetachCount == 0)
		return hrSuccess;
	hr = ptrUserStoreHelper->SetArchiveList(lstCurrentArchives, true);
	if (hr != hrSuccess) {
		m_lpLogger->perr("Failed to set archive list", hr);
		return hr;
	}
	if (!userName.empty())
		m_lpLogger->logf(EC_LOGLEVEL_FATAL, "Auto detached %u archive(s) from \"" TSTRING_PRINTF "\".", ulDetachCount, userName.c_str());
	else {
		tstring strUserName;
		if (m_ptrSession->GetUserInfo(userId, &strUserName, NULL) == hrSuccess)
			m_lpLogger->logf(EC_LOGLEVEL_FATAL, "Auto detached %u archive(s) from \"" TSTRING_PRINTF "\".", ulDetachCount, strUserName.c_str());
		else
			m_lpLogger->logf(EC_LOGLEVEL_FATAL, "Auto detached %u archive(s).", ulDetachCount);
	}
	return ptrUserStoreHelper->UpdateSearchFolders();
}

/**
 * Split a coupling in a store name and a folder name.
 * A coupling is defined as <storename>:<foldername>
 * @param[in]	strCoupling		The coupling to parse
 * @param[out]	lpstrArchive	The archive store name
 * @param[out]	lpstrFolder		The archive folder name
 */
HRESULT ArchiveStateUpdater::ParseCoupling(const tstring &strCoupling, tstring *lpstrArchive, tstring *lpstrFolder)
{
	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Coupling: \"" TSTRING_PRINTF "\"", strCoupling.c_str());
	auto idxColon = strCoupling.find(':');
	if (idxColon == std::string::npos) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "No ':' separator found in coupling");
		return MAPI_E_INVALID_PARAMETER;
	}

	auto strArchive = strCoupling.substr(0, idxColon);
	auto strFolder = strCoupling.substr(idxColon + 1);
	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Coupling: archive=\"" TSTRING_PRINTF "\", folder=\"" TSTRING_PRINTF "\"", strArchive.c_str(), strFolder.c_str());
	if (strArchive.empty() || strFolder.empty()) {
		m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Invalid coupling: archive=\"" TSTRING_PRINTF "\", folder=\"" TSTRING_PRINTF "\"", strArchive.c_str(), strFolder.c_str());
		return MAPI_E_INVALID_PARAMETER;
	}
	*lpstrArchive = std::move(strArchive);
	*lpstrFolder = std::move(strFolder);
	return hrSuccess;
}

/**
 * Add/attach coupling based archives.
 * @param[in]	userName		The username of the primary store to attach the
 * 								archives to.
 * @param[in]	lstCouplings	The list of couplings to attach to the store.
 */
HRESULT ArchiveStateUpdater::AddCouplingBased(const tstring &userName, const std::list<tstring> &lstCouplings, unsigned int ulAttachFlags)
{
	ArchiveManagePtr ptrManage;
	m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Attaching coupling based archives.");

	if (lstCouplings.empty()) {
		m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Empty coupling list");
		return hrSuccess;
	}
	auto hr = ArchiveManageImpl::Create(m_ptrSession, nullptr, userName.c_str(), m_lpLogger, &ptrManage);
	if (hr != hrSuccess)
		return hr;
	auto lpManage = dynamic_cast<ArchiveManageImpl *>(ptrManage.get());
	assert(lpManage != NULL);
	if (lpManage == NULL) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to dynamic cast to ArchiveManageImpl pointer.");
		return MAPI_E_CALL_FAILED;
	}

	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Attaching %zu couplings", lstCouplings.size());
	for (const auto &i : lstCouplings) {
		tstring strArchive, strFolder;

		hr = ParseCoupling(i, &strArchive, &strFolder);
		if (hr != hrSuccess)
			return hr;

		hr = lpManage->AttachTo(NULL, strArchive.c_str(), strFolder.c_str(), ulAttachFlags, ImplicitAttach);
		if (hr != hrSuccess) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to attach to store \"" TSTRING_PRINTF "\" in folder \"" TSTRING_PRINTF "\": %s (%x)",
				strArchive.c_str(), strFolder.c_str(),
				GetMAPIErrorMessage(hr), hr);
			return hr;
		}
	}
	return hrSuccess;
}

/**
 * Add/attach server based archives.
 * @param[in]	userName		The username of the primary store to attach the
 * 								archives to.
 * @param[in]	userId			The entryid of the user whose primary store to
 * 								attach to.
 * @param[in]	lstServers		The list of servers on which an archive for userName
 * 								should be created or opened and attached to the
 * 								primary store.
 */
HRESULT ArchiveStateUpdater::AddServerBased(const tstring &userName, const abentryid_t &userId, const std::list<tstring> &lstServers, unsigned int ulAttachFlags)
{
	ArchiveManagePtr ptrManage;
	m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Attaching servername based archives.");

	if (lstServers.empty()) {
		m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Empty servername list");
		return hrSuccess;
	}
	auto hr = ArchiveManageImpl::Create(m_ptrSession, nullptr, userName.c_str(), m_lpLogger, &ptrManage);
	if (hr != hrSuccess)
		return hr;
	auto lpManage = dynamic_cast<ArchiveManageImpl *>(ptrManage.get());
	assert(lpManage != NULL);
	if (lpManage == NULL) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to dynamic cast to ArchiveManageImpl pointer.");
		return MAPI_E_CALL_FAILED;
	}

	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Attaching %zu servers", lstServers.size());
	for (const auto &i : lstServers) {
		MsgStorePtr ptrArchive;

		hr = m_ptrSession->OpenOrCreateArchiveStore(userName, i, &~ptrArchive);
		if (hr != hrSuccess) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to open or create the archive for user \"" TSTRING_PRINTF "\" on server \"" TSTRING_PRINTF "\": %s (%x)",
				userName.c_str(), i.c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}

		hr = lpManage->AttachTo(ptrArchive, L"", NULL, userId, ulAttachFlags, ImplicitAttach);
		if (hr != hrSuccess) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to attach to archive store for user \"" TSTRING_PRINTF "\" on server \"" TSTRING_PRINTF "\": %s (%x)",
				userName.c_str(), i.c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}
	}
	return hrSuccess;
}

/**
 * Verify the current state and update it to match the required state.
 * @param[in]	userId		The entryid of the user whose primary store to
 * 							process.
 * @param[in]	info		ArchiveInfo instance containing the current and
 * 							requried state.
 */
HRESULT ArchiveStateUpdater::VerifyAndUpdate(const abentryid_t &userId, const ArchiveInfo& info, unsigned int ulAttachFlags)
{
	std::list<tstring> lstServers, lstCouplings;
	ObjectEntryList lstArchives = info.lstArchives;

	// Handle the automated couplings
	for (const auto &i : info.lstCouplings) {
		tstring strArchive, strFolder;
		SObjectEntry objEntry;

		auto hr = ParseCoupling(i, &strArchive, &strFolder);
		if (hr != hrSuccess)
			return hr;

		hr = FindArchiveEntry(strArchive, strFolder, &objEntry);
		if (hr == MAPI_E_NOT_FOUND) {
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "store \"" TSTRING_PRINTF "\", folder \"" TSTRING_PRINTF "\" does not exist. Adding to coupling list", strArchive.c_str(), strFolder.c_str());
			lstCouplings.emplace_back(i);
			continue;
		}
		if (hr != hrSuccess) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to get archive entry for store \"" TSTRING_PRINTF "\", folder \"" TSTRING_PRINTF "\": %s (%x)",
				strArchive.c_str(), strFolder.c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}

		// see if entry is in list of attached archives.
		auto iObjEntry = std::find_if(lstArchives.begin(), lstArchives.end(), Predicates::SObjectEntry_equals_compareEntryId(m_ptrSession->GetMAPISession(), objEntry));
		if (iObjEntry == lstArchives.end()) {
			// Found a coupling that's not yet attached. Add it to the to-attach-list.
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "store \"" TSTRING_PRINTF "\", folder \"" TSTRING_PRINTF "\" not yet attached. Adding to coupling list", strArchive.c_str(), strFolder.c_str());
			lstCouplings.emplace_back(i);
		} else {
			// Found a coupling that's already attached. Remove it from lstArchives, which is later processed to remove all
			// implicitly attached archives from it.
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "store \"" TSTRING_PRINTF "\", folder \"" TSTRING_PRINTF "\" already attached. Removing from post process list", strArchive.c_str(), strFolder.c_str());
			lstArchives.erase(iObjEntry);
		}
	}

	// Handle the automated archive stores
	for (const auto &i : info.lstServers) {
		entryid_t archiveId;

		auto hr = m_ptrSession->GetArchiveStoreEntryId(info.userName, i, &archiveId);
		if (hr == MAPI_E_NOT_FOUND) {
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "archive store for \"" TSTRING_PRINTF "\" on server \"" TSTRING_PRINTF "\" does not exist. Adding to server list", info.userName.c_str(), i.c_str());
			lstServers.emplace_back(i);
			continue;
		}
		if (hr != hrSuccess) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to get archive store id for \"" TSTRING_PRINTF "\" on server \"" TSTRING_PRINTF "\": %s (%x)",
				info.userName.c_str(), i.c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}

		// see if entry is in list of attached archives (store entryid only)
		auto iObjEntry = std::find_if(lstArchives.begin(), lstArchives.end(),
			[&](const SObjectEntry &e) {
				ULONG r = 0;
				return m_ptrSession->GetMAPISession()->CompareEntryIDs(archiveId.size(), archiveId, e.sStoreEntryId.size(), e.sStoreEntryId, 0, &r) == hrSuccess && r == 1;
			});
		if (iObjEntry == lstArchives.end()) {
			// Found a server/archive that's not yet attached. Add it to the to-attach-list.
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "archive store for \"" TSTRING_PRINTF "\" on server \"" TSTRING_PRINTF "\" not yet attached. Adding to server list", info.userName.c_str(), i.c_str());
			lstServers.emplace_back(i);
		} else {
			// Found a server/archive that's already attached. Remove it from lstArchives, which is later processed to remove all
			// implicitly attached archives from it.
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "archive store for \"" TSTRING_PRINTF "\" on server \"" TSTRING_PRINTF "\" already attached. Removing from post process list", info.userName.c_str(), i.c_str());
			lstArchives.erase(iObjEntry);
		}
	}

	auto hr = RemoveImplicit(info.storeId, info.userName, abentryid_t(), lstArchives);
	if (hr != hrSuccess)
		return hr;
	hr = AddCouplingBased(info.userName, lstCouplings, ulAttachFlags);
	if (hr != hrSuccess)
		return hr;
	return AddServerBased(info.userName, userId, lstServers, ulAttachFlags);
}

/**
 * Find the SObjectEntry for the archive specified by store- and foldername.
 * @param[in]	strArchive	The store name of the archive.
 * @param[in]	strFolder	The folder name of the archive.
 * @param[out]	lpObjEntry	The returned SObjectEntry.
 * @retval	MAPI_E_NOT_FOUND	The requested archive does not exist.
 */
HRESULT ArchiveStateUpdater::FindArchiveEntry(const tstring &strArchive, const tstring &strFolder, SObjectEntry *lpObjEntry)
{
	MsgStorePtr ptrArchiveStore;
	ArchiveHelperPtr ptrArchiveHelper;

	auto hr = m_ptrSession->OpenStoreByName(strArchive, &~ptrArchiveStore);
	if (hr != hrSuccess) {
		m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to open store for user \"" TSTRING_PRINTF "\": %s (%x)",
			strArchive.c_str(), GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	hr = ArchiveHelper::Create(ptrArchiveStore, strFolder, NULL, &ptrArchiveHelper);
	if (hr != hrSuccess)
		return hr;

	hr = ptrArchiveHelper->GetArchiveEntry(false, lpObjEntry);
	if (hr != hrSuccess && hr != MAPI_E_NOT_FOUND)
		m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to get archive entry for folder \"" TSTRING_PRINTF "\": %s (%x)",
			strFolder.c_str(), GetMAPIErrorMessage(hr), hr);
	return hr;
}

} /* namespace */
