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

#ifndef ECEXCHANGEIMPORTCONTENTSCHANGES_H
#define ECEXCHANGEIMPORTCONTENTSCHANGES_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include "ECMAPIFolder.h"

#include <kopano/ECUnknown.h>
#include <IECImportContentsChanges.h>

class ECLogger;

class ECExchangeImportContentsChanges _kc_final : public ECUnknown {
protected:
	ECExchangeImportContentsChanges(ECMAPIFolder *lpFolder);
	virtual ~ECExchangeImportContentsChanges();

public:
	static	HRESULT Create(ECMAPIFolder *lpFolder, LPEXCHANGEIMPORTCONTENTSCHANGES* lppExchangeImportContentsChanges);

	// IUnknown
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);

	// IExchangeImportContentsChanges
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT Config(LPSTREAM lpStream, ULONG ulFlags);
	virtual HRESULT UpdateState(LPSTREAM lpStream);
	virtual HRESULT ImportMessageChange(ULONG cValue, LPSPropValue lpPropArray, ULONG ulFlags, LPMESSAGE * lppMessage);
	virtual HRESULT ImportMessageDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList);
	virtual HRESULT ImportPerUserReadStateChange(ULONG cElements, LPREADSTATE lpReadState);
	virtual HRESULT ImportMessageMove(ULONG cbSourceKeySrcFolder, BYTE *pbSourceKeySrcFolder, ULONG cbSourceKeySrcMessage, BYTE *pbSourceKeySrcMessage, ULONG cbPCLMessage, BYTE *pbPCLMessage, ULONG cbSourceKeyDestMessage, BYTE *pbSourceKeyDestMessage, ULONG cbChangeNumDestMessage, BYTE *pbChangeNumDestMessage);

	// IECImportContentsChanges
	virtual HRESULT ConfigForConversionStream(LPSTREAM lpStream, ULONG ulFlags, ULONG cValuesConversion, LPSPropValue lpPropArrayConversion);
	virtual HRESULT ImportMessageChangeAsAStream(ULONG cValue, LPSPropValue lpPropArray, ULONG ulFlags, LPSTREAM *lppstream);
	virtual HRESULT SetMessageInterface(REFIID refiid);

	class xECImportContentsChanges _kc_final :
	    public IECImportContentsChanges {
		#include <kopano/xclsfrag/IUnknown.hpp>

		// <kopano/xclsfrag/IExchangeImportContentsChanges.hpp>
		virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG flags, LPMAPIERROR *lppMAPIError) _kc_override;
		virtual HRESULT __stdcall Config(LPSTREAM lpStream, ULONG flags) _kc_override;
		virtual HRESULT __stdcall UpdateState(LPSTREAM lpStream) _kc_override;
		virtual HRESULT __stdcall ImportMessageChange(ULONG cValue, LPSPropValue lpPropArray, ULONG flags, LPMESSAGE *lppMessage) _kc_override;
		virtual HRESULT __stdcall ImportMessageDeletion(ULONG flags, LPENTRYLIST lpSourceEntryList) _kc_override;
		virtual HRESULT __stdcall ImportPerUserReadStateChange(ULONG cElements, LPREADSTATE lpReadState) _kc_override;
		virtual HRESULT __stdcall ImportMessageMove(ULONG cbSourceKeySrcFolder, BYTE *pbSourceKeySrcFolder, ULONG cbSourceKeySrcMessage, BYTE *pbSourceKeySrcMessage, ULONG cbPCLMessage, BYTE *pbPCLMessage, ULONG cbSourceKeyDestMessage, BYTE *pbSourceKeyDestMessage, ULONG cbChangeNumDestMessage, BYTE *pbChangeNumDestMessage) _kc_override;

		// <kopano/xclsfrag/IECImportContentsChanges.hpp>
		virtual HRESULT __stdcall ConfigForConversionStream(LPSTREAM lpStream, ULONG flags, ULONG cValuesConversion, LPSPropValue lpPropArrayConversion) _kc_override;
		virtual HRESULT __stdcall ImportMessageChangeAsAStream(ULONG cValue, LPSPropValue lpPropArray, ULONG flags, LPSTREAM *lppstream) _kc_override;
		virtual HRESULT __stdcall SetMessageInterface(REFIID refiid) _kc_override;
	} m_xECImportContentsChanges;

private:
	bool	IsProcessed(LPSPropValue lpRemoteCK, LPSPropValue lpLocalPCL);
	bool	IsConflict(LPSPropValue lpLocalCK, LPSPropValue lpRemotePCL);

	HRESULT CreateConflictMessage(LPMESSAGE lpMessage);
	HRESULT CreateConflictMessageOnly(LPMESSAGE lpMessage, LPSPropValue *lppConflictItems);
	HRESULT CreateConflictFolders();
	HRESULT CreateConflictFolder(LPTSTR lpszName, LPSPropValue lpAdditionalREN, ULONG ulMVPos, LPMAPIFOLDER lpParentFolder, LPMAPIFOLDER * lppConflictFolder);

	HRESULT ImportMessageCreateAsStream(ULONG cValue, LPSPropValue lpPropArray, WSMessageStreamImporter **lppMessageImporter);
	HRESULT ImportMessageUpdateAsStream(ULONG cbEntryId, LPENTRYID lpEntryId, ULONG cValue, LPSPropValue lpPropArray, WSMessageStreamImporter **lppMessageImporter);

	static HRESULT HrUpdateSearchReminders(LPMAPIFOLDER lpRootFolder, LPSPropValue lpAdditionalREN);
	friend class ECExchangeImportHierarchyChanges;

private:
	ECLogger *m_lpLogger = nullptr;
	ECMAPIFolder *m_lpFolder = nullptr;
	SPropValue *m_lpSourceKey = nullptr;
	IStream *m_lpStream = nullptr;
	ULONG m_ulFlags = 0;
	ULONG m_ulSyncId = 0;
	ULONG m_ulChangeId = 0;
	IID m_iidMessage;
};

#endif // ECEXCHANGEIMPORTCONTENTSCHANGES_H
