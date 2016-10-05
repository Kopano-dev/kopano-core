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

#include <kopano/platform.h>
#include <kopano/mapi_ptr.h>
#include "ECMAPIDebug.h"
#include <kopano/ECDebug.h>

using namespace std;

HRESULT Dump(std::ostream &os, LPMAPIPROP lpProp, const std::string &strPrefix)
{
	ULONG cValues;
	SPropArrayPtr ptrProps;
	std::string strObjType = "MAPIProp";
	LPSPropValue lpObjType = NULL;
	
	if (lpProp == NULL)
		return MAPI_E_INVALID_PARAMETER;
	HRESULT hr = lpProp->GetProps(NULL, 0, &cValues, &ptrProps);
	if (FAILED(hr))
		return hr;

	lpObjType = PpropFindProp(ptrProps.get(), cValues, PR_OBJECT_TYPE);
	if (lpObjType) {
		switch (lpObjType->Value.l) {
			case MAPI_MESSAGE:
				strObjType = "Message";
				break;
			case MAPI_ATTACH:
				strObjType = "Attach";
				break;
			default:
				break;
		}
	}

	os << strPrefix << "Object type: " << strObjType << endl;
	os << strPrefix << "Properties (count=" << cValues << "):" << endl;
	for (ULONG i = 0; i < cValues; ++i)
		os << strPrefix << "  " << PropNameFromPropTag(ptrProps[i].ulPropTag) << " - " << PropValueToString(&ptrProps[i]) << endl;

	// TODO: Handle more object types
	if (lpObjType && lpObjType->Value.l == MAPI_MESSAGE) {
		MessagePtr ptrMessage;
		MAPITablePtr ptrTable;
		ULONG ulCount = 0;

		hr = lpProp->QueryInterface(ptrMessage.iid, &ptrMessage);
		if (hr != hrSuccess)
			return hr;
		// List recipients
		hr = ptrMessage->GetRecipientTable(0, &ptrTable);
		if (hr != hrSuccess)
			return hr;
		hr = ptrTable->GetRowCount(0, &ulCount);
		if (hr != hrSuccess)
			return hr;

		os << strPrefix << "Recipients (count=" << ulCount << "):" << endl;
		if (ulCount > 0) {
			SRowSetPtr ptrRows;

			while (true) {
				hr = ptrTable->QueryRows(64, 0, &ptrRows);
				if (hr != hrSuccess)
					return hr;

				if (ptrRows.empty())
					break;

				for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
					LPSPropValue lpRowId = PpropFindProp(ptrRows[i].lpProps, ptrRows[i].cValues, PR_ROWID);

					os << strPrefix << "  Recipient: ";
					if (lpRowId)
						os << lpRowId->Value.l;
					else
						os << "???";
					os << endl;

					for (ULONG j = 0; j < ptrRows[i].cValues; ++j)
						os << strPrefix << "    " << PropNameFromPropTag(ptrRows[i].lpProps[j].ulPropTag) << " - " << PropValueToString(&ptrRows[i].lpProps[j]) << endl;
				}
			}
		}

		// List attachments
		hr = ptrMessage->GetAttachmentTable(0, &ptrTable);
		if (hr != hrSuccess)
			return hr;

		hr = ptrTable->GetRowCount(0, &ulCount);
		if (hr != hrSuccess)
			return hr;

		os << strPrefix << "Attachments (count=" << ulCount << "):" << endl;
		if (ulCount > 0) {
			SizedSPropTagArray(1, sptaAttachProps) = {1, {PR_ATTACH_NUM}};

			hr = ptrTable->SetColumns((LPSPropTagArray)&sptaAttachProps, TBL_BATCH);
			if (hr != hrSuccess)
				return hr;

			while (true) {
				SRowSetPtr ptrRows;

				hr = ptrTable->QueryRows(64, 0, &ptrRows);
				if (hr != hrSuccess)
					return hr;

				if (ptrRows.empty())
					break;

				for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
					AttachPtr ptrAttach;

					if (ptrRows[i].lpProps[0].ulPropTag != PR_ATTACH_NUM)
						return hr;

					hr = ptrMessage->OpenAttach(ptrRows[i].lpProps[0].Value.l, &ptrAttach.iid, 0, &ptrAttach);
					if (hr != hrSuccess)
						return hr;

					os << strPrefix << "  Attachment: " << ptrRows[i].lpProps[0].Value.l << endl;
					hr = Dump(os, ptrAttach, strPrefix + "  ");
					if (hr != hrSuccess)
						return hr;
				}
			}
		}
	}

	else if (lpObjType && lpObjType->Value.l == MAPI_ATTACH) {
		AttachPtr ptrAttach;
		SPropValuePtr ptrAttachMethod;

		hr = lpProp->QueryInterface(ptrAttach.iid, &ptrAttach);
		if (hr != hrSuccess)
			return hr;
		hr = HrGetOneProp(ptrAttach, PR_ATTACH_METHOD, &ptrAttachMethod);
		if (hr != hrSuccess)
			return hr;

		// TODO: Handle more attachment types.
		if (ptrAttachMethod->Value.l == ATTACH_EMBEDDED_MSG) {
			MessagePtr ptrMessage;

			hr = ptrAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &ptrMessage.iid, 0, 0, &ptrMessage);
			if (hr != hrSuccess)
				return hr;

			os << strPrefix << "Embedded message:" << endl;
			hr = Dump(os, ptrMessage, strPrefix + "  ");
			if (hr != hrSuccess)
				return hr;
		}
	}
	return hrSuccess;
}
