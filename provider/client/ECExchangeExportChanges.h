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
#include <IECExportChanges.h>
#include <IECImportContentsChanges.h>

#include "WSMessageStreamExporter.h"

class ECExchangeExportChanges _zcp_final : public ECUnknown {
protected:
	ECExchangeExportChanges(ECMsgStore *lpStore, const std::string& strSK, const wchar_t *szDisplay, unsigned int ulSyncType);
	virtual ~ECExchangeExportChanges();

public:
	static	HRESULT Create(ECMsgStore *lpStore, REFIID iid, const std::string& strSK, const wchar_t *szDisplay, unsigned int ulSyncType, LPEXCHANGEEXPORTCHANGES* lppExchangeExportChanges);

	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);

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

private:
	class xECExportChanges _zcp_final : public IECExportChanges {
		#include <kopano/xclsfrag/IUnknown.hpp>

		// <kopano/xclsfrag/IExchangeExportChanges.hpp>
		virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
		virtual HRESULT __stdcall Config(LPSTREAM lpStream, ULONG ulFlags, LPUNKNOWN lpCollector, LPSRestriction lpRestriction, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize);
		virtual HRESULT __stdcall Synchronize(ULONG *pulSteps, ULONG *pulProgress);
		virtual HRESULT __stdcall UpdateState(LPSTREAM lpStream);

		virtual HRESULT __stdcall ConfigSelective(ULONG ulPropTag, LPENTRYLIST lpEntries, LPENTRYLIST lpParents, ULONG ulFlags, LPUNKNOWN lpCollector, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize) _zcp_override;
		virtual HRESULT __stdcall GetChangeCount(ULONG *lpcChanges) _zcp_override;
		virtual HRESULT __stdcall SetMessageInterface(REFIID refiid) _zcp_override;
		virtual HRESULT __stdcall SetLogger(ECLogger *lpLogger) _zcp_override;
	} m_xECExportChanges;
	
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
	bool			m_bConfiged;
	ECMsgStore*		m_lpStore;
	std::string		m_sourcekey;
	std::wstring	m_strDisplay;
	LPSTREAM		m_lpStream;
	ULONG			m_ulFlags;
	ULONG			m_ulSyncId;
	ULONG			m_ulChangeId;
	ULONG			m_ulStep;
	ULONG			m_ulBatchSize;
	ULONG			m_ulBufferSize;
	ULONG			m_ulEntryPropTag;

	IID				m_iidMessage;

	LPEXCHANGEIMPORTCONTENTSCHANGES		m_lpImportContents;
	IECImportContentsChanges *m_lpImportStreamedContents;
	LPEXCHANGEIMPORTHIERARCHYCHANGES	m_lpImportHierarchy;
	
	WSMessageStreamExporterPtr			m_ptrStreamExporter;
	
	std::vector<ICSCHANGE> m_lstChange;

	typedef std::list<ICSCHANGE>	ChangeList;
	typedef ChangeList::iterator	ChangeListIter;
	ChangeList m_lstFlag;
	ChangeList m_lstSoftDelete;
	ChangeList m_lstHardDelete;

	typedef std::set<std::pair<unsigned int, std::string> > PROCESSEDCHANGESSET;
	
	PROCESSEDCHANGESSET m_setProcessedChanges;

	ICSCHANGE *			m_lpChanges;
	ULONG				m_ulChanges;
	ULONG				m_ulMaxChangeId;
	LPSRestriction		m_lpRestrict;

	ECLogger			*m_lpLogger;
	clock_t				m_clkStart;
	struct tms			m_tmsStart;
	
	HRESULT AddProcessedChanges(ChangeList &lstChanges);
	
};

#endif // ECEXCHANGEEXPORTCHANGES_H
