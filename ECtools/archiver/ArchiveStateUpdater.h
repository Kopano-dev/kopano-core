/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ARCHIVESTATEUPDATER_H_INCLUDED
#define ARCHIVESTATEUPDATER_H_INCLUDED

#include <kopano/memory.hpp>
#include <kopano/zcdefs.h>
#include "ArchiveStateCollector.h"

namespace KC {

/**
 * This class updates the current archive state to the should-be state.
 */
class KC_EXPORT ArchiveStateUpdater final {
public:
	typedef ArchiveStateCollector::ArchiveInfo		ArchiveInfo;
	typedef ArchiveStateCollector::ArchiveInfoMap	ArchiveInfoMap;

	KC_HIDDEN static HRESULT Create(const ArchiverSessionPtr &, std::shared_ptr<ECLogger>, const ArchiveInfoMap &, ArchiveStateUpdaterPtr *);
	HRESULT UpdateAll(unsigned int ulAttachFlags);
	HRESULT Update(const tstring &userName, unsigned int ulAttachFlags);

private:
	KC_HIDDEN ArchiveStateUpdater(const ArchiverSessionPtr &, std::shared_ptr<ECLogger>, const ArchiveInfoMap &);
	KC_HIDDEN HRESULT PopulateUserList();
	KC_HIDDEN HRESULT PopulateFromContainer(IABContainer *);
	KC_HIDDEN HRESULT UpdateOne(const abentryid_t &user_id, const ArchiveInfo &, unsigned int attach_flags);
	KC_HIDDEN HRESULT RemoveImplicit(const entryid_t &store_id, const tstring &usen, const abentryid_t &user_id, const ObjectEntryList &archives);
	KC_HIDDEN HRESULT ParseCoupling(const tstring &coupling, tstring *archive, tstring *folder);
	KC_HIDDEN HRESULT AddCouplingBased(const tstring &user, const std::list<tstring> &couplings, unsigned int attach_flags);
	KC_HIDDEN HRESULT AddServerBased(const tstring &user, const abentryid_t &user_id, const std::list<tstring> &servers, unsigned int attach_flags);
	KC_HIDDEN HRESULT VerifyAndUpdate(const abentryid_t &user_id, const ArchiveInfo &, unsigned int attach_flags);
	KC_HIDDEN HRESULT FindArchiveEntry(const tstring &archive, const tstring &folder, SObjectEntry *obj_entry);

	ArchiverSessionPtr	m_ptrSession;
	std::shared_ptr<ECLogger> m_lpLogger;
	ArchiveInfoMap	m_mapArchiveInfo;
};

} /* namespace */

#endif // !defined ARCHIVESTATEUPDATER_H_INCLUDED
