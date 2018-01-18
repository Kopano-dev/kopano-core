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

#ifndef ECEXCHANGEEXPORTCHANGES_H
#define ECEXCHANGEEXPORTCHANGES_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <vector>
#include <set>
#include <string>
#include "ics_client.hpp"
#include "ECMAPIProp.h"

#include <kopano/ECLogger.h>
#include <kopano/ECUnknown.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/memory.hpp>
#include "WSMessageStreamExporter.h"

using namespace KC;

class ECExchangeExportChanges _kc_final :
    public ECUnknown, public IECExportChanges {
protected:
	ECExchangeExportChanges(ECMsgStore *lpStore, const std::string& strSK, const wchar_t *szDisplay, unsigned int ulSyncType);
public:
	static	HRESULT Create(ECMsgStore *lpStore, REFIID iid, const std::string& strSK, const wchar_t *szDisplay, unsigned int ulSyncType, LPEXCHANGEEXPORTCHANGES* lppExchangeExportChanges);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT Config(LPSTREAM lpStream, ULONG ulFlags, LPUNKNOWN lpCollector, LPSRestriction lpRestriction, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize);
	virtual HRESULT ConfigSelective(ULONG ulPropTag, LPENTRYLIST lpEntries, LPENTRYLIST lpParents, ULONG ulFlags, LPUNKNOWN lpCollector, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize);
	virtual HRESULT Synchronize(ULONG *pulSteps, ULONG *pulProgress);
	virtual HRESULT UpdateState(LPSTREAM lpStream);
	
	virtual HRESULT GetChangeCount(ULONG *lpcChanges);
	virtual HRESULT SetMessageInterface(REFIID refiid);
	virtual HRESULT SetLogger(ECLogger *lpLogger);

private:
	void LogMessageProps(int loglevel, ULONG cValues, LPSPropValue lpPropArray);
	HRESULT ExportMessageChanges();
	HRESULT ExportMessageChangesSlow();
	HRESULT ExportMessageChangesFast();
	HRESULT ExportMessageFlags();
	HRESULT ExportMessageDeletes();
	HRESULT ExportFolderChanges();
	HRESULT ExportFolderDeletes();
	HRESULT UpdateStream(LPSTREAM lpStream);
	HRESULT ChangesToEntrylist(std::list<ICSCHANGE> * lpLstChanges, LPENTRYLIST * lppEntryList);

	unsigned long	m_ulSyncType;
	bool m_bConfiged = false;
	std::string		m_sourcekey;
	std::wstring	m_strDisplay;
	ULONG m_ulFlags = 0;
	ULONG m_ulSyncId = 0, m_ulChangeId = 0;
	ULONG m_ulStep = 0, m_ulBatchSize;
	ULONG m_ulBufferSize = 0;
	ULONG m_ulEntryPropTag = PR_SOURCE_KEY; // This is normally the tag that is sent to exportMessageChangeAsStream()

	IID				m_iidMessage;
	WSMessageStreamExporterPtr			m_ptrStreamExporter;
	
	std::vector<ICSCHANGE> m_lstChange;

	typedef std::list<ICSCHANGE>	ChangeList;
	typedef ChangeList::iterator	ChangeListIter;
	ChangeList m_lstFlag;
	ChangeList m_lstSoftDelete;
	ChangeList m_lstHardDelete;

	typedef std::set<std::pair<unsigned int, std::string> > PROCESSEDCHANGESSET;
	
	PROCESSEDCHANGESSET m_setProcessedChanges;
	ULONG m_ulChanges = 0, m_ulMaxChangeId = 0;
	clock_t m_clkStart = 0;
	struct tms			m_tmsStart;
	KC::object_ptr<ECLogger> m_lpLogger;
	KC::memory_ptr<SRestriction> m_lpRestrict;
	KC::object_ptr<IExchangeImportHierarchyChanges> m_lpImportHierarchy;
	KC::object_ptr<IECImportContentsChanges> m_lpImportStreamedContents;
	KC::object_ptr<IExchangeImportContentsChanges> m_lpImportContents;
	KC::object_ptr<IStream> m_lpStream;
	KC::object_ptr<ECMsgStore> m_lpStore;
	KC::memory_ptr<ICSCHANGE> m_lpChanges;
	
	HRESULT AddProcessedChanges(ChangeList &lstChanges);
	ALLOC_WRAP_FRIEND;
};

#endif // ECEXCHANGEEXPORTCHANGES_H
