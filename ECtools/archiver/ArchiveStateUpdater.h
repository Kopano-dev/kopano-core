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

#include "ArchiveStateCollector.h"

/**
 * This class updates the current archive state to the should-be state.
 */
class ArchiveStateUpdater {
public:
	typedef ArchiveStateCollector::ArchiveInfo		ArchiveInfo;
	typedef ArchiveStateCollector::ArchiveInfoMap	ArchiveInfoMap;

	static HRESULT Create(const ArchiverSessionPtr &ptrSession, ECLogger *lpLogger, const ArchiveInfoMap &mapArchiveInfo, ArchiveStateUpdaterPtr *lpptrUpdater);

	virtual ~ArchiveStateUpdater();

	HRESULT UpdateAll(unsigned int ulAttachFlags);
	HRESULT Update(const tstring &userName, unsigned int ulAttachFlags);

private:
	ArchiveStateUpdater(const ArchiverSessionPtr &ptrSession, ECLogger *lpLogger, const ArchiveInfoMap &mapArchiveInfo);

	HRESULT PopulateUserList();
	HRESULT PopulateFromContainer(LPABCONT lpContainer);

	HRESULT UpdateOne(const abentryid_t &userId, const ArchiveInfo& info, unsigned int ulAttachFlags);
	HRESULT RemoveImplicit(const entryid_t &storeId, const tstring &userName, const abentryid_t &userId, const ObjectEntryList &lstArchives);

	HRESULT ParseCoupling(const tstring &strCoupling, tstring *lpstrArchive, tstring *lpstrFolder);
	HRESULT AddCouplingBased(const tstring &userName, const std::list<tstring> &lstCouplings, unsigned int ulAttachFlags);
	HRESULT AddServerBased(const tstring &userName, const abentryid_t &userId, const std::list<tstring> &lstServers, unsigned int ulAttachFlags);
	HRESULT VerifyAndUpdate(const abentryid_t &userId, const ArchiveInfo& info, unsigned int ulAttachFlags);

	HRESULT FindArchiveEntry(const tstring &strArchive, const tstring &strFolder, SObjectEntry *lpObjEntry);

private:
	ArchiverSessionPtr	m_ptrSession;
	ECLogger	*m_lpLogger;

	ArchiveInfoMap	m_mapArchiveInfo;
};

#endif // !defined ARCHIVESTATEUPDATER_H_INCLUDED
