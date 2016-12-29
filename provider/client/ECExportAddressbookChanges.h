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

#include "ECABContainer.h"

namespace KC {

class IECImportAddressbookChanges;
class ECLogger;

}

class ECExportAddressbookChanges _kc_final : public ECUnknown {
public:
	ECExportAddressbookChanges(ECMsgStore *lpContainer);
	virtual ~ECExportAddressbookChanges();
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	// IECExportAddressbookChanges
	virtual HRESULT	Config(LPSTREAM lpState, ULONG ulFlags, IECImportAddressbookChanges *lpCollector);
	virtual HRESULT Synchronize(ULONG *lpulSteps, ULONG *lpulProgress);
	virtual HRESULT UpdateState(LPSTREAM lpState);

private:
	static bool LeftPrecedesRight(const ICSCHANGE &left, const ICSCHANGE &right);

private:
	class xECExportAddressbookChanges _kc_final :
	    public IECExportAddressbookChanges {
		#include <kopano/xclsfrag/IUnknown.hpp>
		// <kopano/xclsfrag/IECExportAddressbookChanges.hpp>
		virtual HRESULT __stdcall Config(LPSTREAM lpState, ULONG flags, IECImportAddressbookChanges *lpCollector) _kc_override;
		virtual HRESULT __stdcall Synchronize(ULONG *lpulSteps, ULONG *lpulProgress) _kc_override;
		virtual HRESULT __stdcall UpdateState(LPSTREAM lpState) _kc_override;
	} m_xECExportAddressbookChanges;
	
private:
	IECImportAddressbookChanges *m_lpImporter = nullptr;
	unsigned int m_ulChangeId = 0;
	ECMsgStore *m_lpMsgStore = nullptr;
	unsigned int m_ulThisChange = 0;
	ULONG m_ulChanges = 0;
	ULONG m_ulMaxChangeId =0;
	ICSCHANGE *m_lpRawChanges = nullptr; // Raw data from server
	ICSCHANGE *m_lpChanges = nullptr; // Same data, but sorted (users, then groups)
	std::set<ULONG>				m_setProcessed;
	ECLogger *m_lpLogger = nullptr;
};

#endif
