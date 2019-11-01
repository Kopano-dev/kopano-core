/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ARCHIVESTATECOLLECTOR_H_INCLUDED
#define ARCHIVESTATECOLLECTOR_H_INCLUDED

#include <map>
#include <memory>
#include <kopano/zcdefs.h>
#include "archivestateupdater_fwd.h"
#include "ArchiverSessionPtr.h"     // For ArchiverSessionPtr
#include <kopano/archiver-common.h>
#include "ECArchiverLogger.h"

namespace KC {

class ArchiveStateCollector;
typedef std::shared_ptr<ArchiveStateCollector> ArchiveStateCollectorPtr;

/**
 * The ArchiveStateCollector will construct the current archive state, which
 * is the set of currently attached archives for each primary store, and the
 * should-be archive state, which is the set of attached archives for each
 * primary store as specified in LDAP/ADS.
 */
class KC_EXPORT ArchiveStateCollector final {
public:
	static HRESULT Create(const ArchiverSessionPtr &ptrSession, std::shared_ptr<ECLogger>, ArchiveStateCollectorPtr *lpptrCollector);
	HRESULT GetArchiveStateUpdater(ArchiveStateUpdaterPtr *lpptrUpdater);

	struct ArchiveInfo {
		tstring userName;
		entryid_t storeId;
		std::list<tstring> lstServers;
		std::list<tstring> lstCouplings;
		ObjectEntryList lstArchives;
	};
	typedef std::map<abentryid_t, ArchiveInfo> ArchiveInfoMap;

private:
	_kc_hidden ArchiveStateCollector(const ArchiverSessionPtr &, std::shared_ptr<ECLogger>);
	_kc_hidden HRESULT PopulateUserList(void);
	_kc_hidden HRESULT PopulateFromContainer(LPABCONT container);

	ArchiverSessionPtr m_ptrSession;
	std::shared_ptr<ECArchiverLogger> m_lpLogger;
	ArchiveInfoMap	m_mapArchiveInfo;
};

} /* namespace */

#endif // !defined ARCHIVESTATECOLLECTOR_H_INCLUDED
