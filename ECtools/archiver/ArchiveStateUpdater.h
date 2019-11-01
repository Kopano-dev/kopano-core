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

	_kc_hidden static HRESULT Create(const ArchiverSessionPtr &, std::shared_ptr<ECLogger>, const ArchiveInfoMap &, ArchiveStateUpdaterPtr *);
	HRESULT UpdateAll(unsigned int ulAttachFlags);
	HRESULT Update(const tstring &userName, unsigned int ulAttachFlags);

private:
	_kc_hidden ArchiveStateUpdater(const ArchiverSessionPtr &, std::shared_ptr<ECLogger>, const ArchiveInfoMap &);
	_kc_hidden HRESULT PopulateUserList(void);
	_kc_hidden HRESULT PopulateFromContainer(LPABCONT container);
	_kc_hidden HRESULT UpdateOne(const abentryid_t &user_id, const ArchiveInfo &, unsigned int attach_flags);
	_kc_hidden HRESULT RemoveImplicit(const entryid_t &store_id, const tstring &usen, const abentryid_t &user_id, const ObjectEntryList &archives);
	_kc_hidden HRESULT ParseCoupling(const tstring &coupling, tstring *archive, tstring *folder);
	_kc_hidden HRESULT AddCouplingBased(const tstring &user, const std::list<tstring> &couplings, unsigned int attach_flags);
	_kc_hidden HRESULT AddServerBased(const tstring &user, const abentryid_t &user_id, const std::list<tstring> &servers, unsigned int attach_flags);
	_kc_hidden HRESULT VerifyAndUpdate(const abentryid_t &user_id, const ArchiveInfo &, unsigned int attach_flags);
	_kc_hidden HRESULT FindArchiveEntry(const tstring &archive, const tstring &folder, SObjectEntry *obj_entry);

	ArchiverSessionPtr	m_ptrSession;
	std::shared_ptr<ECLogger> m_lpLogger;
	ArchiveInfoMap	m_mapArchiveInfo;
};

} /* namespace */

#endif // !defined ARCHIVESTATEUPDATER_H_INCLUDED
