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
#include <kopano/zcdefs.h>

namespace KC {
class ECLogger;
}

class ECExchangeImportContentsChanges KC_FINAL_OPG :
    public KC::ECUnknown, public KC::IECImportContentsChanges {
protected:
	ECExchangeImportContentsChanges(ECMAPIFolder *lpFolder);
public:
	static	HRESULT Create(ECMAPIFolder *lpFolder, LPEXCHANGEIMPORTCONTENTSCHANGES* lppExchangeImportContentsChanges);

	// IUnknown
	virtual HRESULT QueryInterface(const IID &, void **) override;

	// IExchangeImportContentsChanges
	virtual HRESULT GetLastError(HRESULT result, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT Config(IStream *, unsigned int flags) override;
	virtual HRESULT UpdateState(IStream *) override;
	virtual HRESULT ImportMessageChange(unsigned int nvals, SPropValue *, unsigned int flags, IMessage **) override;
	virtual HRESULT ImportMessageDeletion(unsigned int flags, ENTRYLIST *source_entry) override;
	virtual HRESULT ImportPerUserReadStateChange(unsigned int nelem, READSTATE *) override;
	virtual HRESULT ImportMessageMove(unsigned int srcfld_size, BYTE *sk_srcfld, unsigned int msg_size, BYTE *sk_msg, unsigned int pclmsg_size, BYTE *pclmsg, unsigned int dstmsg_size, BYTE *sk_dstmsg, unsigned int cbChangeNumDestMessage, BYTE *pbChangeNumDestMessage) override;

	// IECImportContentsChanges
	virtual HRESULT ImportMessageChangeAsAStream(unsigned int nvals, SPropValue *, unsigned int flags, IStream **) override;

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
