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

#ifndef ECEXPORTADDRESSBOOKCHANGES_H
#define ECEXPORTADDRESSBOOKCHANGES_H

#include <kopano/zcdefs.h>
#include <set>
#include <kopano/memory.hpp>
#include "ECABContainer.h"

namespace KC {

class IECImportAddressbookChanges;
class ECLogger;

}

using namespace KC;

class ECExportAddressbookChanges _kc_final :
    public ECUnknown, public IECExportAddressbookChanges {
public:
	ECExportAddressbookChanges(ECMsgStore *lpContainer);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	// IECExportAddressbookChanges
	virtual HRESULT	Config(LPSTREAM lpState, ULONG ulFlags, IECImportAddressbookChanges *lpCollector);
	virtual HRESULT Synchronize(ULONG *lpulSteps, ULONG *lpulProgress);
	virtual HRESULT UpdateState(LPSTREAM lpState);

private:
	static bool LeftPrecedesRight(const ICSCHANGE &left, const ICSCHANGE &right);

	unsigned int m_ulChangeId = 0;
	ECMsgStore *m_lpMsgStore = nullptr;
	unsigned int m_ulThisChange = 0;
	ULONG m_ulChanges = 0;
	ULONG m_ulMaxChangeId =0;
	std::set<ULONG>				m_setProcessed;
	KC::object_ptr<ECLogger> m_lpLogger;
	KC::object_ptr<IECImportAddressbookChanges> m_lpImporter;
	KC::memory_ptr<ICSCHANGE> m_lpChanges; /* Same data as @m_lpRawChanges, but sorted (users, then groups) */
	KC::memory_ptr<ICSCHANGE> m_lpRawChanges; /* Raw data from server */
};

#endif
