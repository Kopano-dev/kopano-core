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

#ifndef ARCHIVESTATEUPDATER_H_INCLUDED
#define ARCHIVESTATEUPDATER_H_INCLUDED

#include <kopano/zcdefs.h>
#include "ArchiveStateCollector.h"

namespace KC {

/**
 * This class updates the current archive state to the should-be state.
 */
class _kc_export ArchiveStateUpdater _kc_final {
public:
	typedef ArchiveStateCollector::ArchiveInfo		ArchiveInfo;
	typedef ArchiveStateCollector::ArchiveInfoMap	ArchiveInfoMap;

	_kc_hidden static HRESULT Create(const ArchiverSessionPtr &, ECLogger *, const ArchiveInfoMap &, ArchiveStateUpdaterPtr *);
	_kc_hidden virtual ~ArchiveStateUpdater(void);
	HRESULT UpdateAll(unsigned int ulAttachFlags);
	HRESULT Update(const tstring &userName, unsigned int ulAttachFlags);

private:
	_kc_hidden ArchiveStateUpdater(const ArchiverSessionPtr &, ECLogger *, const ArchiveInfoMap &);
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
	ECLogger	*m_lpLogger;

	ArchiveInfoMap	m_mapArchiveInfo;
};

} /* namespace */

#endif // !defined ARCHIVESTATEUPDATER_H_INCLUDED
