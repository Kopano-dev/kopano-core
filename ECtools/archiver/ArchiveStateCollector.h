/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <list>
#include <map>
#include <memory>
#include <kopano/zcdefs.h>
#include <kopano/archiver-common.h>
#include "ECArchiverLogger.h"

namespace KC {

class ArchiveStateCollector;
class ArchiveStateUpdater;
class ArchiverSession;

/**
 * The ArchiveStateCollector will construct the current archive state, which
 * is the set of currently attached archives for each primary store, and the
 * should-be archive state, which is the set of attached archives for each
 * primary store as specified in LDAP/ADS.
 */
class KC_EXPORT ArchiveStateCollector final {
public:
	static HRESULT Create(const std::shared_ptr<ArchiverSession> &, std::shared_ptr<ECLogger>, std::shared_ptr<ArchiveStateCollector> *);
	HRESULT GetArchiveStateUpdater(std::shared_ptr<ArchiveStateUpdater> *);

	struct ArchiveInfo {
		tstring userName;
		entryid_t storeId;
		std::list<tstring> lstServers;
		std::list<tstring> lstCouplings;
		std::list<SObjectEntry> lstArchives;
	};
	typedef std::map<abentryid_t, ArchiveInfo> ArchiveInfoMap;

private:
	KC_HIDDEN ArchiveStateCollector(const std::shared_ptr<ArchiverSession> &, std::shared_ptr<ECLogger>);
	KC_HIDDEN HRESULT PopulateUserList();
	KC_HIDDEN HRESULT PopulateFromContainer(IABContainer *);

	std::shared_ptr<ArchiverSession> m_ptrSession;
	std::shared_ptr<ECArchiverLogger> m_lpLogger;
	ArchiveInfoMap	m_mapArchiveInfo;
};

} /* namespace */
