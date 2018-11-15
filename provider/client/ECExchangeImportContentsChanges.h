/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECEXCHANGEIMPORTCONTENTSCHANGES_H
#define ECEXCHANGEIMPORTCONTENTSCHANGES_H

#include <memory>
#include <kopano/memory.hpp>
#include <mapidefs.h>
#include "ECMAPIFolder.h"
#include <kopano/ECUnknown.h>
#include <kopano/IECInterfaces.hpp>

namespace KC {
class ECLogger;
}

class ECExchangeImportContentsChanges final :
    public KC::ECUnknown, public KC::IECImportContentsChanges {
protected:
	ECExchangeImportContentsChanges(ECMAPIFolder *lpFolder);
public:
	static	HRESULT Create(ECMAPIFolder *lpFolder, LPEXCHANGEIMPORTCONTENTSCHANGES* lppExchangeImportContentsChanges);

	// IUnknown
	virtual HRESULT QueryInterface(const IID &, void **) override;

	// IExchangeImportContentsChanges
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT Config(LPSTREAM lpStream, ULONG ulFlags);
	virtual HRESULT UpdateState(LPSTREAM lpStream);
	virtual HRESULT ImportMessageChange(ULONG cValue, LPSPropValue lpPropArray, ULONG ulFlags, LPMESSAGE * lppMessage);
	virtual HRESULT ImportMessageDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList);
	virtual HRESULT ImportPerUserReadStateChange(ULONG cElements, LPREADSTATE lpReadState);
	virtual HRESULT ImportMessageMove(ULONG cbSourceKeySrcFolder, BYTE *pbSourceKeySrcFolder, ULONG cbSourceKeySrcMessage, BYTE *pbSourceKeySrcMessage, ULONG cbPCLMessage, BYTE *pbPCLMessage, ULONG cbSourceKeyDestMessage, BYTE *pbSourceKeyDestMessage, ULONG cbChangeNumDestMessage, BYTE *pbChangeNumDestMessage);

	// IECImportContentsChanges
	virtual HRESULT ImportMessageChangeAsAStream(ULONG cValue, LPSPropValue lpPropArray, ULONG ulFlags, LPSTREAM *lppstream);

private:
	bool IsProcessed(const SPropValue *remote_ck, const SPropValue *local_pcl);
	bool IsConflict(const SPropValue *local_ck, const SPropValue *remote_pcl);

	HRESULT CreateConflictMessage(LPMESSAGE lpMessage);
	HRESULT CreateConflictMessageOnly(LPMESSAGE lpMessage, LPSPropValue *lppConflictItems);
	HRESULT CreateConflictFolders();
	HRESULT CreateConflictFolder(LPTSTR lpszName, LPSPropValue lpAdditionalREN, ULONG ulMVPos, LPMAPIFOLDER lpParentFolder, LPMAPIFOLDER * lppConflictFolder);

	HRESULT ImportMessageCreateAsStream(ULONG cValue, LPSPropValue lpPropArray, WSMessageStreamImporter **lppMessageImporter);
	HRESULT ImportMessageUpdateAsStream(ULONG eid_size, const ENTRYID *eid, ULONG nvals, const SPropValue *, WSMessageStreamImporter **);
	static HRESULT HrUpdateSearchReminders(LPMAPIFOLDER lpRootFolder, const SPropValue *);
	HRESULT zlog(const char *, HRESULT = 0);
	friend class ECExchangeImportHierarchyChanges;

	IStream *m_lpStream = nullptr;
	unsigned int m_ulFlags = 0, m_ulSyncId = 0, m_ulChangeId = 0;
	KC::memory_ptr<SPropValue> m_lpSourceKey;
	std::shared_ptr<KC::ECLogger> m_lpLogger;
	KC::object_ptr<ECMAPIFolder> m_lpFolder;
};

#endif // ECEXCHANGEIMPORTCONTENTSCHANGES_H
