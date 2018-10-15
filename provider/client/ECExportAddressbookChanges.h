/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECEXPORTADDRESSBOOKCHANGES_H
#define ECEXPORTADDRESSBOOKCHANGES_H

#include <memory>
#include <set>
#include <kopano/memory.hpp>
#include "ECABContainer.h"

namespace KC {

class IECImportAddressbookChanges;
class ECLogger;

}

class ECExportAddressbookChanges final :
    public KC::ECUnknown, public KC::IECExportAddressbookChanges {
public:
	ECExportAddressbookChanges(ECMsgStore *lpContainer);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	// IECExportAddressbookChanges
	virtual HRESULT Config(IStream *state, ULONG flags, KC::IECImportAddressbookChanges *collector);
	virtual HRESULT Synchronize(ULONG *lpulSteps, ULONG *lpulProgress);
	virtual HRESULT UpdateState(LPSTREAM lpState);

private:
	static bool LeftPrecedesRight(const ICSCHANGE &left, const ICSCHANGE &right);

	unsigned int m_ulChangeId = 0;
	ECMsgStore *m_lpMsgStore = nullptr;
	unsigned int m_ulThisChange = 0, m_ulChanges = 0, m_ulMaxChangeId = 0;
	std::set<ULONG>				m_setProcessed;
	std::shared_ptr<KC::ECLogger> m_lpLogger;
	KC::object_ptr<KC::IECImportAddressbookChanges> m_lpImporter;
	KC::memory_ptr<ICSCHANGE> m_lpChanges; /* Same data as @m_lpRawChanges, but sorted (users, then groups) */
	KC::memory_ptr<ICSCHANGE> m_lpRawChanges; /* Raw data from server */
};

#endif
