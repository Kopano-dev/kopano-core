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

/**
 * @defgroup tnef TNEF reader and writer
 * @{
 */

/**
 * @brief  
 * This is our TNEF class, which has been specially designed for
 * simple TNEF reading and writing. 
 *
 * Currently does not support recipient-table properties.
 *
 * TNEF has gone through various versions for Microsoft Mail and
 * other really old systems, and therefore has an elaborate backwards-
 * compatibility system. This means that most properties can be stored
 * in both TNEF properties as within a single 'MAPI' property of the
 * TNEF stream (0x00069003). We basically discard all the backwards-
 * compatibility and write TNEF streams that only work with
 * Outlook 2000 or later (maybe also Outlook 97, not sure about that)
 * by only writing the TNEF stream properties in 0x00069003.
 *
 * -- Steve
 */
 
#include <kopano/platform.h>

#include <mapidefs.h> 
#include <mapiutil.h>
#include <mapiguid.h>
#include <kopano/mapiext.h>
#include <kopano/Util.h>
#include <kopano/charset/convert.h>
#include <kopano/charset/utf16string.h>
#include <string>

#include "tnef.h"

enum {
	ATT_ATTACH_TITLE     = 0x18010,
	ATT_REQUEST_RES      = 0x40009,
	ATT_ATTACH_DATA      = 0x6800F,
	ATT_ATTACH_META_FILE = 0x68011,
	ATT_ATTACH_REND_DATA = 0x69002,
	ATT_MAPI_PROPS       = 0x69003,
	ATT_ATTACHMENT       = 0x69005,
	ATT_MESSAGE_CLASS    = 0x78008,
};

// The mapping between Microsoft Mail IPM classes and those used in MAPI
// see: http://msdn2.microsoft.com/en-us/library/ms527360.aspx
static const struct _sClassMap {
	const char *szScheduleClass;
	const char *szMAPIClass;
} sClassMap[] = {
	{ "IPM.Microsoft Schedule.MtgReq",		"IPM.Schedule.Meeting.Request" },
	{ "IPM.Microsoft Schedule.MtgRespP",	"IPM.Schedule.Meeting.Resp.Pos" },
	{ "IPM.Microsoft Schedule.MtgRespN",	"IPM.Schedule.Meeting.Resp.Neg" },
	{ "IPM.Microsoft Schedule.MtgRespA",	"IPM.Schedule.Meeting.Resp.Tent" },
	{ "IPM.Microsoft Schedule.MtgCncl",		"IPM.Schedule.Meeting.Canceled" },
	{ "IPM.Microsoft Mail.Non-Delivery",	"Report.IPM.Note.NDR" },
	{ "IPM.Microsoft Mail.Read Receipt",	"Report.IPM.Note.IPNRN" },
	{ "IPM.Microsoft Mail.Note",			"IPM.Note" },
	{ "IPM.Microsoft Mail.Note",			"IPM" }
};

static const char *FindMAPIClassByScheduleClass(const char *szSClass)
{
	for (size_t i = 0; i < ARRAY_SIZE(sClassMap); ++i)
		if(strcasecmp(szSClass, sClassMap[i].szScheduleClass) == 0) {
			return sClassMap[i].szMAPIClass;
		}

	return NULL;
}

/**
 * Returns TRUE if the given property tag is in the given property tag array
 *
 * @param[in]	ulPropTag	The property tag to find in lpPropList
 * @param[in]	lpPropList	The proptagarray to loop through
 * @retval	true	ulPropTag is alread present in lpPropList
 * @retval	false	ulPropTag is not present in lpPropList
 */
static bool PropTagInPropList(ULONG ulPropTag, const SPropTagArray *lpPropList)
{
	if (lpPropList == NULL)
		return false;

	for (ULONG i = 0; i < lpPropList->cValues; ++i)
		if (PROP_ID(ulPropTag) == PROP_ID(lpPropList->aulPropTag[i]))
			return true;
	return false;
}

/**
 * ECTNEF constructor, used for base and sub objects in TNEF streams
 *
 * @param[in]		ulFlags		TNEF_ENCODE
 * @param[in]		lpMessage	Properties from this message will be saved to lpStream as TNEF data
 * @param[in,out]	lpStream	An existing empty stream to save the propteries to as TNEF data
 *
 * @param[in]		ulFlags		TNEF_DECODE
 * @param[in,out]	lpMessage	TNEF properties will be saved to this message, and attachments will be create under this message.
 * @param[in]		lpStream	IStream object to the TNEF data
 */
ECTNEF::ECTNEF(ULONG ulFlags, IMessage *lpMessage, IStream *lpStream)
{
	this->ulFlags = ulFlags;
	this->m_lpMessage = lpMessage;
	this->m_lpStream = lpStream;
}

/**
 * ECTNEF destructor frees allocated memory while handling the TNEF
 * stream.
 */
ECTNEF::~ECTNEF()
{
	for (const auto p : lstProps)
		MAPIFreeBuffer(p);
	for (const auto a : lstAttachments)
		FreeAttachmentData(a);
}

/** 
 * Frees all allocated memory for attachments found in the TNEF
 * stream.
 * 
 * @param[in,out] lpTnefAtt free all data in this attachment and delete the pointer too
 */
void ECTNEF::FreeAttachmentData(tnefattachment* lpTnefAtt)
{
	delete[] lpTnefAtt->data;
	for (const auto p : lpTnefAtt->lstProps)
		MAPIFreeBuffer(p);
	delete lpTnefAtt;
}

/**
 * Read data from lpStream and set in memory as one large
 * property. Only used to save MAPI_E_NOT_ENOUGH_MEMORY properties
 * from m_lpMessage to a separate LPSPropValue which will be saved in
 * the TNEF Stream later in Finish().
 *
 * @param[in]	lpStream	Input stream that points to PT_BINARY or PT_UNICODE data
 * @param[in]	ulPropTag	Current data type of lpStream, TYPE part can only contain either PT_BINARY or PT_UNICODE.
 * @param[out]	lppPropValue Property structure to return data from stream in, with ulPropTag
 * @return	MAPI error code, stream errors, memory errors.
 * @retval MAPI_E_INVALID_PARAMETER invalid lpStream of lppPropValue pointer
 * @retval MAPI_E_INVALID_TYPE invalid ulPropTag
 */
static HRESULT StreamToPropValue(IStream *lpStream, ULONG ulPropTag,
    LPSPropValue *lppPropValue)
{
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpPropValue = NULL;
	STATSTG			sStatstg;
	ULONG			ulRead = 0;
	ULONG			ulTotal = 0;
	BYTE *wptr = NULL;

	if (lpStream == NULL || lppPropValue == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if (PROP_TYPE(ulPropTag) != PT_BINARY && PROP_TYPE(ulPropTag) != PT_UNICODE)
	{
		hr = MAPI_E_INVALID_TYPE;
		goto exit;
	}

	hr = lpStream->Stat(&sStatstg, 0);
	if(hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateBuffer(sizeof(SPropValue), (void**)&lpPropValue);
	if(hr != hrSuccess)
		goto exit;

	lpPropValue->ulPropTag = ulPropTag;
	
	if (PROP_TYPE(ulPropTag) == PT_BINARY) {
		lpPropValue->Value.bin.cb = (ULONG)sStatstg.cbSize.QuadPart;
	
		hr = MAPIAllocateMore((ULONG)sStatstg.cbSize.QuadPart, lpPropValue, (void**)&lpPropValue->Value.bin.lpb);
		if(hr != hrSuccess)
			goto exit;
		wptr = lpPropValue->Value.bin.lpb;
	} else if (PROP_TYPE(ulPropTag) == PT_UNICODE) {
		hr = MAPIAllocateMore((ULONG)sStatstg.cbSize.QuadPart + sizeof(WCHAR), lpPropValue, (void**)&lpPropValue->Value.lpszW);
		if (hr != hrSuccess)
			goto exit;
		// terminate unicode string
		lpPropValue->Value.lpszW[sStatstg.cbSize.QuadPart / sizeof(WCHAR)] = L'\0';
		wptr = (BYTE*)lpPropValue->Value.lpszW;
	}

	while (1) {
		hr = lpStream->Read(wptr + ulTotal, 4096, &ulRead);
		if (hr != hrSuccess)
			goto exit;

		if(ulRead == 0) {
			break;
		}
		ulTotal += ulRead;
	}

	*lppPropValue = lpPropValue;

exit:
	if (hr != hrSuccess)
		MAPIFreeBuffer(lpPropValue);

	return hr;
}

/**
 * Adds the requested properties from the message into the pending
 * TNEF stream. String properties in lpPropList must be in
 * PT_UNICODE. PT_STRING8 properties will never be added.
 *
 * @param[in]	ulFlags		TNEF_PROP_INCLUDE: add only properties from message to stream from the lpPropList, or
 * 							TNEF_PROP_EXCLUDE: add all properties except if listed in lpPropList
 * @param[in]	lpPropList	List of properties to add to the stream if present in m_lpMessage, or
 * 							List of properties to exclude from the message
 * @return	MAPI error code
 */
HRESULT ECTNEF::AddProps(ULONG ulFlags, LPSPropTagArray lpPropList)
{
	HRESULT			hr = hrSuccess;
	LPSPropTagArray lpPropListMessage = NULL;
	LPSPropValue	lpPropValue = NULL;
	LPSPropValue	lpStreamValue = NULL;
	SizedSPropTagArray(1, sPropTagArray);
	unsigned int	i = 0;
	bool			fPropTagInList = false;
	ULONG			cValue = 0;
	IStream*		lpStream = NULL;

	// Loop through all the properties on the message, and only
	// add those that we want to add to the list

	hr = m_lpMessage->GetPropList(MAPI_UNICODE, &lpPropListMessage);
	if (hr != hrSuccess)
		goto exit;

	for (i = 0; i < lpPropListMessage->cValues; ++i) {
		/*
		 * Do not send properties in 0x67XX range, since these seem to
		 * be blacklisted in recent exchange servers, which causes
		 * exchange to drop the entire message.
		 */
		if (PROP_ID(lpPropListMessage->aulPropTag[i]) >= 0x6700 &&
		    PROP_ID(lpPropListMessage->aulPropTag[i]) <= 0x67FF)
			continue;

		// unable to save these properties
		if(PROP_TYPE(lpPropListMessage->aulPropTag[i]) == PT_OBJECT || 
		   PROP_TYPE(lpPropListMessage->aulPropTag[i]) == PT_UNSPECIFIED ||
		   PROP_TYPE(lpPropListMessage->aulPropTag[i]) == PT_NULL)
			continue;

		fPropTagInList = PropTagInPropList(lpPropListMessage->aulPropTag[i], lpPropList);

		if(( (ulFlags & TNEF_PROP_INCLUDE) && fPropTagInList) || ((ulFlags & TNEF_PROP_EXCLUDE) && !fPropTagInList)) {
			sPropTagArray.cValues = 1;
			sPropTagArray.aulPropTag[0] = lpPropListMessage->aulPropTag[i];
			hr = m_lpMessage->GetProps(sPropTagArray, 0, &cValue, &lpPropValue);

			if(hr == hrSuccess) {
				lstProps.push_back(lpPropValue);
				lpPropValue = NULL;
			}
			if(hr == MAPI_W_ERRORS_RETURNED && lpPropValue != NULL && lpPropValue->Value.err == MAPI_E_NOT_ENOUGH_MEMORY) {
				if(m_lpMessage->OpenProperty(lpPropListMessage->aulPropTag[i], &IID_IStream, 0, 0, (LPUNKNOWN *)&lpStream) == hrSuccess)
				{

					hr = StreamToPropValue(lpStream, lpPropListMessage->aulPropTag[i], &lpStreamValue);
					if (hr == hrSuccess) {
						lstProps.push_back(lpStreamValue);
						lpStreamValue = NULL;
					}

					lpStream->Release(); lpStream = NULL;
				}
			}

			MAPIFreeBuffer(lpPropValue);
			lpPropValue = NULL;
		
			hr = hrSuccess; // silently ignore the property
		}
	}

exit:
	MAPIFreeBuffer(lpPropListMessage);
	return hr;
}

/**
 * Extracts the properties from the TNEF stream, and sets them in the message
 *
 * @param[in]	ulFlags		TNEF_PROP_INCLUDE or TNEF_PROP_EXCLUDE
 * @param[in]	lpPropList	List of properties to include from the stream if present in m_lpMessage or
 * 							List of properties to exclude from the stream if present in m_lpMessage
 * 
 * @retval	MAPI_E_CORRUPT_DATA TNEF stream input is broken, or other MAPI error codes
 */
HRESULT	ECTNEF::ExtractProps(ULONG ulFlags, LPSPropTagArray lpPropList)
{
	HRESULT hr = hrSuccess;
	ULONG ulSignature = 0;
	ULONG ulType = 0;
	ULONG ulSize = 0;
	unsigned short ulChecksum = 0;
	unsigned short ulKey = 0;
	unsigned char ulComponent = 0;
	BYTE *lpBuffer = NULL;
	SPropValue sProp;
	char *szSClass = NULL;
	// Attachments props
	LPSPropValue lpProp;
	tnefattachment* lpTnefAtt = NULL;

	hr = HrReadDWord(m_lpStream, &ulSignature);
	if(hr != hrSuccess)
		goto exit;

	// Check signature
	if(ulSignature != TNEF_SIGNATURE) {
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}

	hr = HrReadWord(m_lpStream, &ulKey);
	if(hr != hrSuccess)
		goto exit;

	// File is made of blocks, with each a type and size. Component and Key are ignored.
	while(1) {
		hr = HrReadByte(m_lpStream, &ulComponent);

		if(hr != hrSuccess) {
			hr = hrSuccess; // EOF -> no error
			goto exit;
		}

		hr = HrReadDWord(m_lpStream, &ulType);
		if(hr != hrSuccess)
			goto exit;

		hr = HrReadDWord(m_lpStream, &ulSize);
		if(hr != hrSuccess)
			goto exit;

		if (ulSize == 0) {
			// do not allocate 0 size data block
			hr = MAPI_E_CORRUPT_DATA;
			goto exit;
		}

		hr = MAPIAllocateBuffer(ulSize, (void **)&lpBuffer);
		if(hr != hrSuccess)
			goto exit;

		hr = HrReadData(m_lpStream, (char *)lpBuffer, ulSize);
		if(hr != hrSuccess)
			goto exit;

		hr = HrReadWord(m_lpStream, &ulChecksum);
		if(hr != hrSuccess)
			goto exit;

		// Loop through all the blocks of the TNEF data. We are only interested
		// in the properties block for now (0x00069003)

		switch(ulType) {
		case ATT_MAPI_PROPS:
			hr = HrReadPropStream((char *)lpBuffer, ulSize, lstProps);
			if (hr != hrSuccess)
				goto exit;
			break;
		case ATT_MESSAGE_CLASS: /* PR_MESSAGE_CLASS */
			{
				szSClass = new char[ulSize+1];
				char *szMAPIClass = NULL;

				// NULL terminate the string
				memcpy(szSClass, lpBuffer, ulSize);
				szSClass[ulSize] = 0;

				// We map the Schedule+ message class to the more modern MAPI message
				// class. The mapping should be correct as far as we can find ..

				szMAPIClass = (char *)FindMAPIClassByScheduleClass(szSClass);
				if(szMAPIClass == NULL)
					szMAPIClass = szSClass;	// mapping not found, use string from TNEF file

				sProp.ulPropTag = PR_MESSAGE_CLASS_A;
				sProp.Value.lpszA = szMAPIClass;

				// We do a 'SetProps' now because we want to override the PR_MESSAGE_CLASS
				// setting, while Finish() never overrides already-present properties for
				// security reasons.

				m_lpMessage->SetProps(1, &sProp, NULL);

				delete [] szSClass;
				szSClass = NULL;

				break;
			}
		case 0x00050008: /* PR_OWNER_APPT_ID */
			if(ulSize == 4 && lpBuffer) {
				sProp.ulPropTag = PR_OWNER_APPT_ID;
				sProp.Value.l = *((LONG*)lpBuffer);
				m_lpMessage->SetProps(1, &sProp, NULL);
			}
			break;
		case ATT_REQUEST_RES: /* PR_RESPONSE_REQUESTED */
			if(ulSize == 2 && lpBuffer) {
				sProp.ulPropTag = PR_RESPONSE_REQUESTED;
				sProp.Value.b = (bool)(*(short*)lpBuffer);
				m_lpMessage->SetProps(1, &sProp, NULL);
			}
			break;

// --- TNEF attachemnts ---
		case ATT_ATTACH_REND_DATA:
			// Start marker of attachment
		    if(ulSize == sizeof(struct AttachRendData) && lpBuffer) {
		        struct AttachRendData *lpData = (AttachRendData *)lpBuffer;
		        
                if (lpTnefAtt) {
                    if (lpTnefAtt->data || !lpTnefAtt->lstProps.empty()) // end marker previous attachment
                        lstAttachments.push_back(lpTnefAtt);
                    else
                        FreeAttachmentData(lpTnefAtt);
                }
                lpTnefAtt = new tnefattachment;
                lpTnefAtt->size = 0;
                lpTnefAtt->data = NULL;
                lpTnefAtt->rdata = *lpData;
            }
			break;

		case ATT_ATTACH_TITLE: // PR_ATTACH_FILENAME
			if (!lpTnefAtt) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}

			if ((hr = MAPIAllocateBuffer(sizeof(SPropValue), (void**)&lpProp)) != hrSuccess)
				goto exit;
			lpProp->ulPropTag = PR_ATTACH_FILENAME_A;
			if ((hr = MAPIAllocateMore(ulSize, lpProp, (void**)&lpProp->Value.lpszA)) != hrSuccess)
				goto exit;
			memcpy(lpProp->Value.lpszA, lpBuffer, ulSize);
			lpTnefAtt->lstProps.push_back(lpProp);
			break;

		case ATT_ATTACH_META_FILE:
			// PR_ATTACH_RENDERING, extra icon information
			if (!lpTnefAtt) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}

			if ((hr = MAPIAllocateBuffer(sizeof(SPropValue), (void**)&lpProp)) != hrSuccess)
				goto exit;
			lpProp->ulPropTag = PR_ATTACH_RENDERING;
			if ((hr = MAPIAllocateMore(ulSize, lpProp, (void**)&lpProp->Value.bin.lpb)) != hrSuccess)
				goto exit;
			lpProp->Value.bin.cb = ulSize;
			memcpy(lpProp->Value.bin.lpb, lpBuffer, ulSize);
			lpTnefAtt->lstProps.push_back(lpProp);
			break;

		case ATT_ATTACH_DATA:
			// PR_ATTACH_DATA_BIN, will be set via OpenProperty() in ECTNEF::Finish()
			if (!lpTnefAtt) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			lpTnefAtt->size = ulSize;
			lpTnefAtt->data = new BYTE[ulSize];
			memcpy(lpTnefAtt->data, lpBuffer, ulSize);
			break;

		case ATT_ATTACHMENT: // Attachment property stream
			if (!lpTnefAtt) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			hr = HrReadPropStream((char *)lpBuffer, ulSize, lpTnefAtt->lstProps);
			if (hr != hrSuccess)
				goto exit;
			break;

		default:
			// Ignore this block
			break;
		}

		MAPIFreeBuffer(lpBuffer);
		lpBuffer = NULL;
	}

exit:
	if (lpTnefAtt) {
		if (lpTnefAtt->data || !lpTnefAtt->lstProps.empty())	// attachment should be complete before adding
			lstAttachments.push_back(lpTnefAtt);
		else
			FreeAttachmentData(lpTnefAtt);
	}

	delete[] szSClass;
	MAPIFreeBuffer(lpBuffer);
	return hr;
}

/**
 * Write the properties from a list to the TNEF stream.
 *
 * @param[in,out]	lpStream	The TNEF stream to write to
 * @param[in]		proplist	std::list of properties to write in the stream.
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWritePropStream(IStream *lpStream, std::list<SPropValue *> &proplist)
{
	HRESULT hr = HrWriteDWord(lpStream, proplist.size());
	if(hr != hrSuccess)
		return hr;

	for (const auto p : proplist) {
		hr = HrWriteSingleProp(lpStream, p);
		if (hr != hrSuccess)
			return hr;
	}
	return hr;
}

/**
 * Write one property to the TNEF stream.
 *
 * @param[in,out]	lpStream	The TNEF stream to write to
 * @param[in]		lpProp		MAPI property to write to the TNEF stream
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWriteSingleProp(IStream *lpStream, LPSPropValue lpProp) 
{
	HRESULT hr = hrSuccess;
	SizedSPropTagArray(1, sPropTagArray);
	LPSPropTagArray lpsPropTagArray = sPropTagArray;
	ULONG cNames = 0;
	MAPINAMEID **lppNames = NULL;
	ULONG ulLen = 0;
	ULONG ulMVProp = 0;
	ULONG ulCount = 0;
	convert_context converter;
	utf16string ucs2;

	if(PROP_ID(lpProp->ulPropTag) >= 0x8000) {
		// Get named property GUID and ID or name
		sPropTagArray.cValues = 1;
		sPropTagArray.aulPropTag[0] = lpProp->ulPropTag;

		hr = m_lpMessage->GetNamesFromIDs(&lpsPropTagArray, NULL, 0, &cNames, &lppNames);
		if(hr != hrSuccess) {
			hr = hrSuccess; // Ignore this property
			goto exit;
		}

		if(cNames == 0 || lppNames == NULL || lppNames[0] == NULL) {
			hr = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}

		// Write the property tag
		hr = HrWriteDWord(lpStream, lpProp->ulPropTag);
		if(hr != hrSuccess)
			goto exit;

		hr = HrWriteData(lpStream, (char *)lppNames[0]->lpguid, sizeof(GUID));
		if(hr != hrSuccess)
			goto exit;

		if(lppNames[0]->ulKind == MNID_ID) {
			hr = HrWriteDWord(lpStream, 0);
			if(hr != hrSuccess)
				goto exit;

			hr = HrWriteDWord(lpStream, lppNames[0]->Kind.lID);
			if(hr != hrSuccess)
				goto exit;
		} else {
			hr = HrWriteDWord(lpStream, 1);
			if(hr != hrSuccess)
				goto exit;

			ucs2 = converter.convert_to<utf16string>(lppNames[0]->Kind.lpwstrName);
			ulLen = ucs2.length()*sizeof(utf16string::value_type)+sizeof(utf16string::value_type);

			hr = HrWriteDWord(lpStream, ulLen);
			if(hr != hrSuccess)
				goto exit;

			hr = HrWriteData(lpStream, (char *)ucs2.c_str(), ulLen);
			if(hr != hrSuccess)
				goto exit;

			// Align to 4-byte boundary
			while(ulLen & 3) {
				hr = HrWriteByte(lpStream, 0);
				if(hr != hrSuccess)
					goto exit;
				++ulLen;
			}
		}
	} else {
		// Write the property tag
		hr = HrWriteDWord(lpStream, lpProp->ulPropTag);
		if(hr != hrSuccess)
			goto exit;
	}

	// Now, write the actual property value
	if(PROP_TYPE(lpProp->ulPropTag) & MV_FLAG) {
		switch(PROP_TYPE(lpProp->ulPropTag)) {
		case PT_MV_I2:
			ulCount = lpProp->Value.MVi.cValues;
			break;
		case PT_MV_LONG:
			ulCount = lpProp->Value.MVl.cValues;
			break;
		case PT_MV_R4:
			ulCount = lpProp->Value.MVflt.cValues;
			break;
		case PT_MV_APPTIME:
			ulCount = lpProp->Value.MVat.cValues;
			break;
		case PT_MV_DOUBLE:
			ulCount = lpProp->Value.MVdbl.cValues;
			break;
		case PT_MV_CURRENCY:
			ulCount = lpProp->Value.MVcur.cValues;
			break;
		case PT_MV_SYSTIME:
			ulCount = lpProp->Value.MVft.cValues;
			break;
		case PT_MV_I8:
			ulCount = lpProp->Value.MVli.cValues;
			break;
		case PT_MV_STRING8:
			ulCount = lpProp->Value.MVszA.cValues;
			break;
		case PT_MV_UNICODE:
			ulCount = lpProp->Value.MVszW.cValues;
			break;
		case PT_MV_BINARY:
			ulCount = lpProp->Value.MVbin.cValues;
			break;
		case PT_MV_CLSID:
			ulCount = lpProp->Value.MVguid.cValues;
			break;
		default:
			hr = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}

		hr = HrWriteDWord(lpStream, ulCount);
	} else {
		ulCount = 1;
	}

	ulMVProp = 0;

	for (ulMVProp = 0; ulMVProp < ulCount; ++ulMVProp) {
		switch(PROP_TYPE(lpProp->ulPropTag) &~ MV_FLAG) {
		case PT_I2:
			if(lpProp->ulPropTag & MV_FLAG)
				hr = HrWriteDWord(lpStream,lpProp->Value.MVi.lpi[ulMVProp]);
			else
				hr = HrWriteDWord(lpStream,lpProp->Value.i);
			break;
		case PT_LONG:
			if(lpProp->ulPropTag & MV_FLAG)
				hr = HrWriteDWord(lpStream,lpProp->Value.MVl.lpl[ulMVProp]);
			else
				hr = HrWriteDWord(lpStream,lpProp->Value.ul);
			break;
		case PT_BOOLEAN:
			hr = HrWriteDWord(lpStream, lpProp->Value.b);
			break;
		case PT_R4:
			if(lpProp->ulPropTag & MV_FLAG)
				hr = HrWriteData(lpStream,(char *)&lpProp->Value.MVflt.lpflt[ulMVProp], sizeof(float));
			else
				hr = HrWriteData(lpStream,(char *)&lpProp->Value.flt, sizeof(float));
			break;
		case PT_APPTIME:
			if(lpProp->ulPropTag & MV_FLAG)
				hr = HrWriteData(lpStream,(char *)&lpProp->Value.MVat.lpat[ulMVProp], sizeof(double));
			else
				hr = HrWriteData(lpStream,(char *)&lpProp->Value.at, sizeof(double));
			break;
		case PT_DOUBLE:
			if(lpProp->ulPropTag & MV_FLAG)
				hr = HrWriteData(lpStream,(char *)&lpProp->Value.MVdbl.lpdbl[ulMVProp], sizeof(double));
			else
				hr = HrWriteData(lpStream,(char *)&lpProp->Value.dbl, sizeof(double));
			break;
		case PT_CURRENCY:
			if(lpProp->ulPropTag & MV_FLAG) {
				hr = HrWriteDWord(lpStream, lpProp->Value.MVcur.lpcur[ulMVProp].Lo);
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteDWord(lpStream, lpProp->Value.MVcur.lpcur[ulMVProp].Hi);
				if(hr != hrSuccess)
					goto exit;
			} else {
				hr = HrWriteDWord(lpStream, lpProp->Value.cur.Lo);
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteDWord(lpStream, lpProp->Value.cur.Hi);
				if(hr != hrSuccess)
					goto exit;
			}
			break;
		case PT_SYSTIME:
			if(lpProp->ulPropTag & MV_FLAG) {
				hr = HrWriteDWord(lpStream, lpProp->Value.MVft.lpft[ulMVProp].dwLowDateTime);
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteDWord(lpStream, lpProp->Value.MVft.lpft[ulMVProp].dwHighDateTime);
				if(hr != hrSuccess)
					goto exit;
			} else {
				hr = HrWriteDWord(lpStream, lpProp->Value.ft.dwLowDateTime);
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteDWord(lpStream, lpProp->Value.ft.dwHighDateTime);
				if(hr != hrSuccess)
					goto exit;
			}
			break;
		case PT_I8:
			if(lpProp->ulPropTag & MV_FLAG) {
				hr = HrWriteDWord(lpStream, lpProp->Value.MVli.lpli[ulMVProp].LowPart);
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteDWord(lpStream, lpProp->Value.MVli.lpli[ulMVProp].HighPart);
				if(hr != hrSuccess)
					goto exit;
			} else {
				hr = HrWriteDWord(lpStream, lpProp->Value.li.LowPart);
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteDWord(lpStream, lpProp->Value.li.HighPart);
				if(hr != hrSuccess)
					goto exit;
			} 
			break;
		case PT_STRING8:
			if(lpProp->ulPropTag & MV_FLAG) {
				ulLen = strlen(lpProp->Value.MVszA.lppszA[ulMVProp])+1;

				hr = HrWriteDWord(lpStream, ulLen);
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteData(lpStream, lpProp->Value.MVszA.lppszA[ulMVProp], ulLen);
				if(hr != hrSuccess)
					goto exit;
			} else {
				ulLen = strlen(lpProp->Value.lpszA)+1;

				hr = HrWriteDWord(lpStream, 1); // unknown why this is here
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteDWord(lpStream, ulLen);
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteData(lpStream, lpProp->Value.lpszA, ulLen);
				if(hr != hrSuccess)
					goto exit;
			}

			// Align to 4-byte boundary
			while(ulLen & 3) {
				hr = HrWriteByte(lpStream, 0);
				if (hr != hrSuccess)
					goto exit;
				++ulLen;
			}
			break;

		case PT_UNICODE:
			// Make sure we write UCS-2, since that's the format of PT_UNICODE in Win32.
			if(lpProp->ulPropTag & MV_FLAG) {
				ucs2 = converter.convert_to<utf16string>(lpProp->Value.MVszW.lppszW[ulMVProp]);
				ulLen = ucs2.length()*sizeof(utf16string::value_type)+sizeof(utf16string::value_type);

				hr = HrWriteDWord(lpStream, ulLen);
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteData(lpStream, (char *)ucs2.c_str(), ulLen);
				if(hr != hrSuccess)
					goto exit;
			} else {
				ucs2 = converter.convert_to<utf16string>(lpProp->Value.lpszW);
				ulLen = ucs2.length()*sizeof(utf16string::value_type)+sizeof(utf16string::value_type);

				hr = HrWriteDWord(lpStream, 1); // unknown why this is here
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteDWord(lpStream, ulLen);
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteData(lpStream, (char *)ucs2.c_str(), ulLen);
				if(hr != hrSuccess)
					goto exit;
			}

			// Align to 4-byte boundary
			while(ulLen & 3) {
				hr = HrWriteByte(lpStream, 0);
				if (hr != hrSuccess)
					goto exit;
				++ulLen;
			}
			break;

        case PT_OBJECT:
		case PT_BINARY:
			if(lpProp->ulPropTag & MV_FLAG) {
				ulLen = lpProp->Value.MVbin.lpbin[ulMVProp].cb;

				hr = HrWriteDWord(lpStream, ulLen);
				if(hr != hrSuccess)
					goto exit;
					
				hr = HrWriteData(lpStream, (char *)lpProp->Value.MVbin.lpbin[ulMVProp].lpb, ulLen);
				if(hr != hrSuccess)
					goto exit;
			} else {
				ulLen = lpProp->Value.bin.cb;

				hr = HrWriteDWord(lpStream, 1); // unknown why this is here
				if(hr != hrSuccess)
					goto exit;

				hr = HrWriteDWord(lpStream, ulLen + (PROP_TYPE(lpProp->ulPropTag) == PT_OBJECT ? sizeof(GUID) : 0));
				if(hr != hrSuccess)
					goto exit;

                if(PROP_TYPE(lpProp->ulPropTag) == PT_OBJECT)
                    HrWriteData(lpStream, (char *)&IID_IStorage, sizeof(GUID));

				hr = HrWriteData(lpStream, (char *)lpProp->Value.bin.lpb, ulLen);
				if(hr != hrSuccess)
					goto exit;
			}

			// Align to 4-byte boundary
			while(ulLen & 3) {
				hr = HrWriteByte(lpStream, 0);
				if (hr != hrSuccess)
					goto exit;
				++ulLen;
			}
			break;
		
		case PT_CLSID:
			if(lpProp->ulPropTag & MV_FLAG) {
				hr = HrWriteData(lpStream, (char *)&lpProp->Value.MVguid.lpguid[ulMVProp], sizeof(GUID));
				if (hr != hrSuccess)
					goto exit;
			} else {
				hr = HrWriteData(lpStream, (char *)lpProp->Value.lpguid, sizeof(GUID));
				if (hr != hrSuccess)
					goto exit;
			}
			break;

		default:
			hr = MAPI_E_INVALID_PARAMETER;
		}
	}

exit:
	MAPIFreeBuffer(lppNames);
	return hr;
}

/**
 * Read from lpBuffer with size ulSize TNEF properties, and save those
 * in the proplist.
 *
 * @param[in]		lpBuffer	(part of) a TNEF stream which contains properties
 * @param[in]		ulSize		size of contents in lpBuffer
 * @param[in,out]	proplist	reference to an existing porplist to append properties to
 * @return MAPI error code
 */
HRESULT ECTNEF::HrReadPropStream(char *lpBuffer, ULONG ulSize, std::list<SPropValue *> &proplist)
{
	ULONG ulRead = 0;
	ULONG ulProps = 0;
	LPSPropValue lpProp = NULL;
	HRESULT hr = hrSuccess;

	ulProps = *(ULONG *)lpBuffer;
	lpBuffer += 4;
	ulSize -= 4;

	// Loop through all the properties in the data and add them to our internal list
	while(ulProps) {
		hr = HrReadSingleProp(lpBuffer, ulSize, &ulRead, &lpProp);
		if(hr != hrSuccess)
			break;

		ulSize -= ulRead;
		lpBuffer += ulRead;

		proplist.push_back(lpProp);
		--ulProps;

		if(ulRead & 3) {
			// Skip padding
			lpBuffer += 4 - (ulRead & 3);
		}
	}

	return hr;
}

/**
 * Read one property from a TNEF block in a buffer, and return the
 * read property and bytes read from the stream.
 *
 * @param[in]	lpBuffer	TNEF stream buffer
 * @param[in]	ulSize		size of lpBuffer
 * @param[out]	lpulRead	number of bytes read from lpBuffer to make lppProp
 * @param[out]	lppProp		returns MAPIAllocateBuffer allocated pointer if return is hrSuccess
 * @return	MAPI error code
 */
HRESULT ECTNEF::HrReadSingleProp(char *lpBuffer, ULONG ulSize, ULONG *lpulRead, LPSPropValue *lppProp)
{
	HRESULT hr = hrSuccess;
	ULONG ulPropTag = 0;
	ULONG ulLen = 0;
	ULONG ulOrigSize = ulSize;
	ULONG ulIsNameId = 0;
	ULONG ulCount = 0;
	ULONG ulMVProp = 0;
	LPSPropValue lpProp = NULL;
	GUID sGuid;
	MAPINAMEID sNameID;
	LPMAPINAMEID lpNameID = &sNameID;
	LPSPropTagArray lpPropTags = NULL;
	std::wstring strUnicodeName;
	utf16string ucs2;

	if(ulSize < 8)
		return MAPI_E_NOT_FOUND;

	ulPropTag = *(ULONG *)lpBuffer;

	lpBuffer += sizeof(ULONG);
	ulSize -= 4;

	hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&lpProp);
	if(hr != hrSuccess)
		goto exit;

	if(PROP_ID(ulPropTag) >= 0x8000) {
		// Named property, first read GUID, then name/id
		if(ulSize < 24) {
			hr = MAPI_E_CORRUPT_DATA;
			goto exit;
		}
		memcpy(&sGuid, lpBuffer, sizeof(GUID));

		lpBuffer += sizeof(GUID);
		ulSize -= sizeof(GUID);

		ulIsNameId = *(ULONG *)lpBuffer;

		lpBuffer += 4;
		ulSize -= 4;

		if(ulIsNameId != 0) {
			// A string name follows
			ulLen = *(ULONG *)lpBuffer;

			lpBuffer += 4;
			ulSize -= 4;

			if(ulLen > ulSize) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}

			// copy through utf16string so we can set the boundary to the given length
			ucs2.assign((utf16string::value_type*)lpBuffer, ulLen/sizeof(utf16string::value_type));
			strUnicodeName = convert_to<std::wstring>(ucs2);

			sNameID.ulKind = MNID_STRING;
			sNameID.Kind.lpwstrName = (WCHAR *)strUnicodeName.c_str();
			lpBuffer += ulLen;
			ulSize -= ulLen;

			// Re-align
			lpBuffer += ulLen & 3 ? 4 - (ulLen & 3) : 0;
			ulSize -= ulLen & 3 ? 4 - (ulLen & 3) : 0;
		} else {
			sNameID.ulKind = MNID_ID;
			sNameID.Kind.lID = *(ULONG *)lpBuffer;

			lpBuffer += 4;
			ulSize -= 4;
		}

		sNameID.lpguid = &sGuid;

		hr = m_lpMessage->GetIDsFromNames(1, &lpNameID, MAPI_CREATE, &lpPropTags);
		if(hr != hrSuccess)
			goto exit;

		// Use the mapped ID, not the original ID. The original ID is discarded
		ulPropTag = PROP_TAG(PROP_TYPE(ulPropTag), PROP_ID(lpPropTags->aulPropTag[0]));
	}

	if(ulPropTag & MV_FLAG) {
		if(ulSize < 4) {
			hr = MAPI_E_CORRUPT_DATA;
			goto exit;
		}

		ulCount = *(ULONG *)lpBuffer;
		lpBuffer += 4;
		ulSize -= 4;
		
		switch(PROP_TYPE(ulPropTag)) {
		case PT_MV_I2:
			lpProp->Value.MVi.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(unsigned short), lpProp, (void **)&lpProp->Value.MVi.lpi);
			break;
		case PT_MV_LONG:
			lpProp->Value.MVl.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(ULONG), lpProp, (void **)&lpProp->Value.MVl.lpl);
			break;
		case PT_MV_R4:
			lpProp->Value.MVflt.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(float), lpProp, (void **)&lpProp->Value.MVflt.lpflt);
			break;
		case PT_MV_APPTIME:
			lpProp->Value.MVat.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(double), lpProp, (void **)&lpProp->Value.MVat.lpat);
			break;
		case PT_MV_DOUBLE:
			lpProp->Value.MVdbl.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(double), lpProp, (void **)&lpProp->Value.MVdbl.lpdbl);
			break;
		case PT_MV_CURRENCY:
			lpProp->Value.MVcur.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(CURRENCY), lpProp, (void **)&lpProp->Value.MVcur.lpcur);
			break;
		case PT_MV_SYSTIME:
			lpProp->Value.MVft.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(FILETIME), lpProp, (void **)&lpProp->Value.MVft.lpft);
			break;
		case PT_MV_I8:
			lpProp->Value.MVli.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(LARGE_INTEGER), lpProp, (void **)&lpProp->Value.MVli.lpli);
			break;
		case PT_MV_STRING8:
			lpProp->Value.MVszA.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(char *), lpProp, (void **)&lpProp->Value.MVszA.lppszA);
			break;
		case PT_MV_UNICODE:
			lpProp->Value.MVszW.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(WCHAR *), lpProp, (void **)&lpProp->Value.MVszW.lppszW);
			break;
		case PT_MV_BINARY:
			lpProp->Value.MVbin.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(SBinary), lpProp, (void **)&lpProp->Value.MVbin.lpbin);
			break;
		case PT_MV_CLSID:
			lpProp->Value.MVguid.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(GUID), lpProp, (void **)&lpProp->Value.MVguid.lpguid);
			break;
		default:
			hr = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
	} else {
		ulCount = 1;
	}

	if(hr != hrSuccess)
		goto exit;

	lpProp->ulPropTag = ulPropTag;

	for (ulMVProp = 0; ulMVProp < ulCount; ++ulMVProp) {
		switch(PROP_TYPE(ulPropTag) & ~MV_FLAG) {
		case PT_I2:
			if(ulPropTag & MV_FLAG)
				lpProp->Value.MVi.lpi[ulMVProp] = *(unsigned short *)lpBuffer;
			else
				lpProp->Value.i = *(unsigned short *)lpBuffer;

			lpBuffer += 4;
			ulSize -= 4;
			break;
		case PT_LONG:
			if(ulPropTag & MV_FLAG)
				lpProp->Value.MVl.lpl[ulMVProp] = *(ULONG *)lpBuffer;
			else
				lpProp->Value.ul = *(ULONG *)lpBuffer;

			lpBuffer += 4;
			ulSize -= 4;
			break;
		case PT_BOOLEAN:
			lpProp->Value.b = *(BOOL *)lpBuffer;
			lpBuffer += 4;
			ulSize -= 4;
			break;
		case PT_R4:
			if(ulPropTag & MV_FLAG)
				lpProp->Value.MVflt.lpflt[ulMVProp] = *(float *)lpBuffer;
			else
				lpProp->Value.flt = *(float *)lpBuffer;

			lpBuffer += 4;
			ulSize -= 4;
			break;
		case PT_APPTIME:
			if(ulSize < 8) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			if(ulPropTag & MV_FLAG)
				lpProp->Value.MVat.lpat[ulMVProp] = *(double *)lpBuffer;
			else
				lpProp->Value.at = *(double *)lpBuffer;

			lpBuffer += 8;
			ulSize -= 8;
			break;
		case PT_DOUBLE:
			if(ulSize < 8) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			if(ulPropTag & MV_FLAG)
				lpProp->Value.MVdbl.lpdbl[ulMVProp] = *(double *)lpBuffer;
			else
				lpProp->Value.dbl = *(double *)lpBuffer;

			lpBuffer += 8;
			ulSize -= 8;
			break;
		case PT_CURRENCY:
			if(ulSize < 8) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			if(ulPropTag & MV_FLAG) {
				lpProp->Value.MVcur.lpcur[ulMVProp].Lo = *(ULONG *)lpBuffer;
				lpProp->Value.MVcur.lpcur[ulMVProp].Hi = *(ULONG *)(lpBuffer+4);
			} else {
				lpProp->Value.cur.Lo = *(ULONG *)lpBuffer;
				lpProp->Value.cur.Hi = *(ULONG *)lpBuffer+4;
			}

			lpBuffer += 8;
			ulSize -= 8;
			break;
		case PT_SYSTIME:
			if(ulSize < 8) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			if(ulPropTag & MV_FLAG) {
				lpProp->Value.MVft.lpft[ulMVProp].dwLowDateTime = *(ULONG *)lpBuffer;
				lpProp->Value.MVft.lpft[ulMVProp].dwHighDateTime = *(ULONG *)(lpBuffer+4);
			} else {
				lpProp->Value.ft.dwLowDateTime = *(ULONG *)lpBuffer;
				lpProp->Value.ft.dwHighDateTime = *(ULONG *)(lpBuffer+4);
			}
			lpBuffer += 8;
			ulSize -= 8;
			break;
		case PT_I8:
			if(ulSize < 8) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			if(ulPropTag & MV_FLAG) {
				lpProp->Value.MVli.lpli[ulMVProp].LowPart = *(ULONG *)lpBuffer;
				lpProp->Value.MVli.lpli[ulMVProp].HighPart = *(ULONG *)(lpBuffer+4);
			} else {
				lpProp->Value.li.LowPart = *(ULONG *)lpBuffer;
				lpProp->Value.li.HighPart = *(ULONG *)(lpBuffer+4);
			} 

			lpBuffer += 8;
			ulSize -= 8;
			break;
		case PT_STRING8:
			if(ulSize < 8) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			
			if((PROP_TYPE(ulPropTag) & MV_FLAG) == 0) {
    			lpBuffer += 4; // Skip next 4 bytes, they are always '1'
	    		ulSize -= 4;
            }
            
			ulLen = *(ULONG *)lpBuffer;

			lpBuffer += 4; 
			ulSize -= 4;

			if(ulSize < ulLen) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			if(ulPropTag & MV_FLAG) {
				hr = MAPIAllocateMore(ulLen+1, lpProp, (void **)&lpProp->Value.MVszA.lppszA[ulMVProp]);
				if(hr != hrSuccess)
					goto exit;

				memcpy(lpProp->Value.MVszA.lppszA[ulMVProp], lpBuffer, ulLen);
				lpProp->Value.MVszA.lppszA[ulMVProp][ulLen] = 0; // should be terminated anyway but we terminte it just to be sure
			} else {
				hr = MAPIAllocateMore(ulLen+1, lpProp, (void **)&lpProp->Value.lpszA);
				if(hr != hrSuccess)
					goto exit;

				memcpy(lpProp->Value.lpszA, lpBuffer, ulLen);
				lpProp->Value.lpszA[ulLen] = 0; // should be terminated anyway but we terminte it just to be sure
			}

			lpBuffer += ulLen;
			ulSize -= ulLen;

			// Re-align
			lpBuffer += ulLen & 3 ? 4 - (ulLen & 3) : 0;
			ulSize -= ulLen & 3 ? 4 - (ulLen & 3) : 0;
			break;

		case PT_UNICODE:
			// Make sure we read UCS-2, since that is the format of PT_UNICODE in Win32.
			if(ulSize < 8) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}

			if((PROP_TYPE(ulPropTag) & MV_FLAG) == 0) {
    			lpBuffer += 4; // Skip next 4 bytes, they are always '1'
	    		ulSize -= 4;
            }
            
			ulLen = *(ULONG *)lpBuffer;	// Assumes 'len' in file is BYTES, not chars

			lpBuffer += 4;
			ulSize -= 4;

			if(ulSize < ulLen) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}

			// copy through utf16string so we can set the boundary to the given length
			ucs2.assign((utf16string::value_type*)lpBuffer, ulLen/sizeof(utf16string::value_type));
			strUnicodeName = convert_to<std::wstring>(ucs2);

			if(ulPropTag & MV_FLAG) {
				hr = MAPIAllocateMore((strUnicodeName.length()+1) * sizeof(WCHAR), lpProp, (void **)&lpProp->Value.MVszW.lppszW[ulMVProp]);
				if(hr != hrSuccess)
					goto exit;

				wcscpy(lpProp->Value.MVszW.lppszW[ulMVProp], strUnicodeName.c_str());
			} else {
				hr = MAPIAllocateMore((strUnicodeName.length()+1) * sizeof(WCHAR), lpProp, (void **)&lpProp->Value.lpszW);
				if(hr != hrSuccess)
					goto exit;

				wcscpy(lpProp->Value.lpszW, strUnicodeName.c_str());
			}

			lpBuffer += ulLen;
			ulSize -= ulLen;

			// Re-align
			lpBuffer += ulLen & 3 ? 4 - (ulLen & 3) : 0;
			ulSize -= ulLen & 3 ? 4 - (ulLen & 3) : 0;
			break;

		case PT_OBJECT:			// PST sends PT_OBJECT data. Treat as PT_BINARY
		case PT_BINARY:
			if(ulSize < 8) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}

			if((PROP_TYPE(ulPropTag) & MV_FLAG) == 0) {
    			lpBuffer += 4;	// Skip next 4 bytes, it's always '1' (ULONG)
	    		ulSize -= 4;
            }
			ulLen = *(ULONG *)lpBuffer;

			lpBuffer += 4;
			ulSize -= 4;

			if (PROP_TYPE(ulPropTag) == PT_OBJECT) {
			    // Can be IID_IMessage, IID_IStorage, IID_IStream (and possibly others)
				lpBuffer += 16;
				ulSize -= 16;
				ulLen -= 16;
			}

			if(ulSize < ulLen) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}

			if(ulPropTag & MV_FLAG) {
				hr = MAPIAllocateMore(ulLen, lpProp, (void **)&lpProp->Value.MVbin.lpbin[ulMVProp].lpb);
				if(hr != hrSuccess)
					goto exit;

				memcpy(lpProp->Value.MVbin.lpbin[ulMVProp].lpb, lpBuffer, ulLen);
				lpProp->Value.MVbin.lpbin[ulMVProp].cb = ulLen;				
			} else {
				hr = MAPIAllocateMore(ulLen, lpProp, (void **)&lpProp->Value.bin.lpb);
				if(hr != hrSuccess)
					goto exit;

				memcpy(lpProp->Value.bin.lpb, lpBuffer, ulLen);
				lpProp->Value.bin.cb = ulLen;
			}

			lpBuffer += ulLen;
			ulSize -= ulLen;

			// Re-align
			lpBuffer += ulLen & 3 ? 4 - (ulLen & 3) : 0;
			ulSize -= ulLen & 3 ? 4 - (ulLen & 3) : 0;
			break;
		case PT_CLSID:
			if(ulSize < sizeof(GUID)) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			if(ulPropTag & MV_FLAG) {
				memcpy(&lpProp->Value.MVguid.lpguid[ulMVProp], lpBuffer, sizeof(GUID));
			} else {
				hr = MAPIAllocateMore(sizeof(GUID), lpProp, (LPVOID*)&lpProp->Value.lpguid);
				if (hr != hrSuccess)
				    goto exit;
				    
				memcpy(lpProp->Value.lpguid, lpBuffer, sizeof(GUID));
			} 

			lpBuffer += sizeof(GUID);
			ulSize -= sizeof(GUID);
			break;

		default:
			hr = MAPI_E_INVALID_PARAMETER;
			break;
		}
	}

	*lpulRead = ulOrigSize - ulSize;
	*lppProp = lpProp;

exit:
	if (hr != hrSuccess)
		MAPIFreeBuffer(lpProp);
	MAPIFreeBuffer(lpPropTags);
	return hr;
}

/**
 * Add specified properties to the TNEF object list to save with
 * Finish. This makes a lazy copy of the lpProps, so make sure you keep
 * them in memory until you call Finish().
 *
 * @param[in]	cValues		Number of properties in lpProps.
 * @param[in]	lpProps		Array of properties to add to the TNEF object
 * @retval	hrSuccess
 */
HRESULT ECTNEF::SetProps(ULONG cValues, LPSPropValue lpProps)
{
	unsigned int i = 0;

	for (i = 0; i < cValues; ++i)
		lstProps.push_back(&lpProps[i]);
	return hrSuccess;
}

/**
 * Add another component to the TNEF stream. This currently only works
 * for attachments, and you have to pass the PR_ATTACH_NUM in
 * 'ulComponentID'. We then serialize all properties passed in
 * lpPropList into the TNEF stream. Currently we do NOT support
 * ATTACH_EMBEDDED_MSG type attachments - this function is currently
 * only really useful for ATTACH_OLE attachments.
 *
 * @param[in]	ulFlags			Must be TNEF_COMPONENT_ATTACHMENT, others currently not supported.
 * @param[in]	ulComponentID	PR_ATTACH_NUM value passed to OpenAttachment()
 * @param[in]	lpPropList		List of proptags to put in the TNEF stream of this attachment
 * @return MAPI error code
 */
HRESULT ECTNEF::FinishComponent(ULONG ulFlags, ULONG ulComponentID, LPSPropTagArray lpPropList)
{
    HRESULT hr = hrSuccess;
    IAttach *lpAttach = NULL;
    LPSPropValue lpProps = NULL;
    LPSPropValue lpAttachProps = NULL;
    IStream *lpStream = NULL;
    ULONG cValues = 0;
    AttachRendData sData;
    SizedSPropTagArray(2, sptaTags) = {2, { PR_ATTACH_METHOD, PR_RENDERING_POSITION }};
    LPSPropValue lpsNewProp = NULL;
    struct tnefattachment sTnefAttach;
    
    if(ulFlags != TNEF_COMPONENT_ATTACHMENT) {
        hr = MAPI_E_NO_SUPPORT;
        goto exit;
    }
    
    if(this->ulFlags != TNEF_ENCODE) {
        hr = MAPI_E_INVALID_PARAMETER;
        goto exit;
    }
    
    hr = m_lpMessage->OpenAttach(ulComponentID, &IID_IAttachment, 0, &lpAttach);
    if(hr != hrSuccess)
        goto exit;
    
    // Get some properties we always need
    hr = lpAttach->GetProps(sptaTags, 0, &cValues, &lpAttachProps);
    if(FAILED(hr))
        goto exit;
        
    // ignore warnings
    hr = hrSuccess;
    
    memset(&sData, 0, sizeof(sData));
    sData.usType =     lpAttachProps[0].ulPropTag == PR_ATTACH_METHOD && lpAttachProps[0].Value.ul == ATTACH_OLE ? AttachTypeOle : AttachTypeFile;
    sData.ulPosition = lpAttachProps[1].ulPropTag == PR_RENDERING_POSITION ? lpAttachProps[1].Value.ul : 0;
        
    // Get user-passed properties
    hr = lpAttach->GetProps(lpPropList, 0, &cValues, &lpProps);
    if(FAILED(hr))
        goto exit;
    
    for (unsigned int i = 0; i < cValues; ++i) {
        // Other properties
        if(PROP_TYPE(lpProps[i].ulPropTag) == PT_ERROR)
            continue;
        
        hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&lpsNewProp);
        if(hr != hrSuccess)
            goto exit;
                
        if(PROP_TYPE(lpProps[i].ulPropTag) == PT_OBJECT) {
            // PT_OBJECT requested, open object as stream and read the data
            hr = lpAttach->OpenProperty(lpProps[i].ulPropTag, &IID_IStream, 0, 0, (IUnknown **)&lpStream);
            if(hr != hrSuccess)
                goto exit;
                
            // We save the actual data same way as PT_BINARY
            hr = HrReadStream(lpStream, lpsNewProp, &lpsNewProp->Value.bin.lpb, &lpsNewProp->Value.bin.cb);
            if(hr != hrSuccess)
                goto exit;
                
            lpsNewProp->ulPropTag = lpProps[i].ulPropTag;
        } else {
            hr = Util::HrCopyProperty(lpsNewProp, &lpProps[i], lpsNewProp);
            if(hr != hrSuccess)
                goto exit;
        }        
        
        sTnefAttach.lstProps.push_back(lpsNewProp);
        lpsNewProp = NULL;
    }

    sTnefAttach.rdata = sData;
    sTnefAttach.data = NULL;
    sTnefAttach.size = 0;
    lstAttachments.push_back(new tnefattachment(sTnefAttach));
    
exit:
    if(lpStream)
        lpStream->Release();
    MAPIFreeBuffer(lpsNewProp);
    if(lpAttach)
        lpAttach->Release();
    MAPIFreeBuffer(lpProps);
    MAPIFreeBuffer(lpAttachProps);
    return hr;
}

/**
 * Finalize the TNEF object. If the constructors ulFlags was
 * TNEF_DECODE, the properties will be saved to the given message. If
 * ulFlags was TNEF_ENCODE, the lpStream will be written the TNEF
 * data.
 *
 * @return MAPI error code
 */
HRESULT ECTNEF::Finish()
{
	HRESULT hr = hrSuccess;
	IStream *lpPropStream = NULL;
	STATSTG sStat;
	ULONG ulChecksum;
	LARGE_INTEGER zero = {{0,0}};
	ULARGE_INTEGER uzero = {{0,0}};
	// attachment vars
	ULONG ulAttachNum;
	LPATTACH lpAttach = NULL;
	LPSTREAM lpAttStream = NULL;
	LPMESSAGE lpAttMessage = NULL;
	IStream *lpSubStream = NULL;
	SPropValue sProp;

	if(ulFlags == TNEF_DECODE) {
		// Write properties to message
		for (const auto p : lstProps) {
			if (PROP_ID(p->ulPropTag) == PROP_ID(PR_MESSAGE_CLASS) ||
			    !FPropExists(m_lpMessage, p->ulPropTag) ||
			    PROP_ID(p->ulPropTag) == PROP_ID(PR_RTF_COMPRESSED) ||
			    PROP_ID(p->ulPropTag) == PROP_ID(PR_HTML) ||
			    PROP_ID(p->ulPropTag) == PROP_ID(PR_INTERNET_CPID))
  				m_lpMessage->SetProps(1, p, NULL);
			// else, Property already exists, do *not* overwrite it

		}
		// Add all found attachments to message
		for (const auto att : lstAttachments) {
			bool has_obj = false;

			hr = m_lpMessage->CreateAttach(NULL, 0, &ulAttachNum, &lpAttach);
			if (hr != hrSuccess)
				goto exit;
				
			sProp.ulPropTag = PR_ATTACH_METHOD;
			sProp.Value.ul = att->rdata.usType == AttachTypeOle ? ATTACH_OLE : att->lstProps.empty() ? ATTACH_BY_VALUE : ATTACH_EMBEDDED_MSG;
			lpAttach->SetProps(1, &sProp, NULL);

			sProp.ulPropTag = PR_RENDERING_POSITION;
			sProp.Value.ul = att->rdata.ulPosition;
			lpAttach->SetProps(1, &sProp, NULL);
            
			for (const auto p : att->lstProps) {
				// must not set PR_ATTACH_NUM by ourselves
				if (PROP_ID(p->ulPropTag) == PROP_ID(PR_ATTACH_NUM))
					continue;

				if (p->ulPropTag != PR_ATTACH_DATA_OBJ) {
					hr = lpAttach->SetProps(1, p, NULL);
					if (hr != hrSuccess)
						goto exit;
				} else {
					// message in PT_OBJECT, was saved in Value.bin
					if (att->rdata.usType == AttachTypeOle) {
						hr = lpAttach->OpenProperty(p->ulPropTag, &IID_IStream, 0, MAPI_CREATE | MAPI_MODIFY, reinterpret_cast<IUnknown **>(&lpSubStream));
                        if(hr != hrSuccess)
                            goto exit;
                        
					hr = lpSubStream->Write(p->Value.bin.lpb, p->Value.bin.cb, NULL);
                        if(hr != hrSuccess)
                            goto exit;
                        
                        hr = lpSubStream->Commit(0);
                        if(hr != hrSuccess)
                            goto exit;
                            
                        has_obj = true;

                        lpSubStream->Release();
                        lpSubStream = NULL;    
                    } else {
                        hr = CreateStreamOnHGlobal(NULL, TRUE, &lpSubStream);
                        if (hr != hrSuccess)
                            goto exit;

                        hr = lpSubStream->Write(p->Value.bin.lpb, p->Value.bin.cb, NULL);
                        if (hr != hrSuccess)
                            goto exit;

                        hr = lpSubStream->Seek(zero, STREAM_SEEK_SET, NULL);
                        if(hr != hrSuccess)
                            goto exit;

                        hr = lpAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, MAPI_CREATE|MAPI_MODIFY, (LPUNKNOWN *)&lpAttMessage);
                        if(hr != hrSuccess)
                            goto exit;

                        ECTNEF SubTNEF(TNEF_DECODE, lpAttMessage, lpSubStream);
                        hr = SubTNEF.ExtractProps(TNEF_PROP_EXCLUDE, NULL);
                        if (hr != hrSuccess)
                            goto exit;

                        hr = SubTNEF.Finish();
                        if (hr != hrSuccess)
                            goto exit;

                        hr = lpAttMessage->SaveChanges(0);
                        if (hr != hrSuccess)
                            goto exit;

                        has_obj = true;
                        
                        lpSubStream->Release();
                        lpSubStream = NULL;
                    }
				}
			}
			if (!has_obj && att->data != NULL) {
				hr = lpAttach->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, STGM_WRITE|STGM_TRANSACTED, MAPI_CREATE|MAPI_MODIFY, (LPUNKNOWN *)&lpAttStream);
				if (hr != hrSuccess)
					goto exit;

				hr = lpAttStream->Write(att->data, att->size,NULL);
				if (hr != hrSuccess)
					goto exit;
				hr = lpAttStream->Commit(0);
				if (hr != hrSuccess)
					goto exit;
				lpAttStream->Release();
				lpAttStream = NULL;
			}

			hr = lpAttach->SaveChanges(0);
			if (hr != hrSuccess)
				goto exit;
			lpAttach->Release();
			lpAttach = NULL;
		}
	} else if(ulFlags == TNEF_ENCODE) {
		// Write properties to stream
		hr = HrWriteDWord(m_lpStream, TNEF_SIGNATURE);
		if(hr != hrSuccess)
			goto exit;

		hr = HrWriteWord(m_lpStream, 0); // Write Key
		if(hr != hrSuccess)
			goto exit;

		hr = HrWriteByte(m_lpStream, 1); // Write component (always 1 ?)
		if(hr != hrSuccess)
			goto exit;

		hr = CreateStreamOnHGlobal(NULL, TRUE, &lpPropStream);
		if(hr != hrSuccess)
			goto exit;

		hr = HrWritePropStream(lpPropStream, lstProps);
		if(hr != hrSuccess)
			goto exit;

		hr = lpPropStream->Stat(&sStat, STATFLAG_NONAME);
		if(hr != hrSuccess)
			goto exit;

		hr = HrWriteDWord(m_lpStream, 0x00069003);
		if(hr != hrSuccess)
			goto exit;

		hr = HrWriteDWord(m_lpStream, sStat.cbSize.LowPart); // Write size
		if(hr != hrSuccess)
			goto exit;

		hr = lpPropStream->Seek(zero, STREAM_SEEK_SET, NULL);
		if(hr != hrSuccess)
			goto exit;

		hr = lpPropStream->CopyTo(m_lpStream, sStat.cbSize, NULL, NULL); // Write data
		if(hr != hrSuccess)
			goto exit;

		hr = lpPropStream->Seek(zero, STREAM_SEEK_SET, NULL);
		if(hr != hrSuccess)
			goto exit;

		hr = HrGetChecksum(lpPropStream, &ulChecksum);
		if(hr != hrSuccess)
			goto exit;

		hr = HrWriteWord(m_lpStream, (unsigned short)ulChecksum); // Write checksum
		if(hr != hrSuccess)
			goto exit;
		
		// Write attachments	
		for (const auto att : lstAttachments) {
			/* Write attachment start block */
			hr = HrWriteBlock(m_lpStream, reinterpret_cast<char *>(&att->rdata), sizeof(AttachRendData), 0x00069002, 2);
            if(hr != hrSuccess)
                goto exit;
                
            // Write attachment data block if available
			if (att->data != NULL) {
				hr = HrWriteBlock(m_lpStream, reinterpret_cast<char *>(att->data), att->size, 0x0006800f, 2);
                if(hr != hrSuccess)
                    goto exit;
            }
                
            // Write property block
            hr = lpPropStream->SetSize(uzero);
            if(hr != hrSuccess)
                goto exit;

			hr = HrWritePropStream(lpPropStream, att->lstProps);
            if(hr != hrSuccess)
                goto exit;
                
            hr = HrWriteBlock(m_lpStream, lpPropStream, 0x00069005, 2);
            if(hr != hrSuccess)
                goto exit;
            
            // Note that we don't write any other blocks like PR_ATTACH_FILENAME since this information is also in the property block
            
        }
	}

exit:
	if (lpSubStream)
		lpSubStream->Release();

	if (lpAttMessage)
		lpAttMessage->Release();

	if (lpAttStream)
		lpAttStream->Release();

	if (lpAttach)
		lpAttach->Release();

	if(lpPropStream)
		lpPropStream->Release();

	return hr;
}

/**
 * Read one DWORD (32bits ULONG) from input stream
 *
 * @param[in]	lpStream	input stream to read one ULONG from, stream automatically moves current cursor.
 * @param[out]	ulData		ULONG value from lpStream
 * @retval MAPI_E_NOT_FOUND if stream was too short, other MAPI error code
 * @return MAPI error code
 */
HRESULT ECTNEF::HrReadDWord(IStream *lpStream, ULONG *ulData)
{
	HRESULT hr;
	ULONG ulRead = 0;

	hr = lpStream->Read(ulData, sizeof(unsigned int), &ulRead);
	if(hr != hrSuccess)
		return hr;
	if (ulRead != sizeof(unsigned int))
		return MAPI_E_NOT_FOUND;
	return hrSuccess;
}

/**
 * Read one WORD (16bits unsigned short) from input stream
 *
 * @param[in]	lpStream	input stream to read one unsigned short from, stream automatically moves current cursor.
 * @param[out]	ulData		unsigned short value from lpStream
 * @retval MAPI_E_NOT_FOUND if stream was too short, other MAPI error code
 * @return MAPI error code
 */
HRESULT ECTNEF::HrReadWord(IStream *lpStream, unsigned short *ulData)
{
	HRESULT hr;
	ULONG ulRead = 0;

	hr = lpStream->Read(ulData, sizeof(unsigned short), &ulRead);
	if(hr != hrSuccess)
		return hr;
	if (ulRead != sizeof(unsigned short))
		return MAPI_E_NOT_FOUND;
	return hrSuccess;
}

/**
 * Read one BYTE (CHAR_BIT-bits unsigned char) from input stream
 *
 * @param[in]	lpStream	input stream to read one unsigned char from, stream automatically moves current cursor.
 * @param[out]	ulData		unsigned char value from lpStream
 * @retval MAPI_E_NOT_FOUND if stream was too short, other MAPI error code
 * @return MAPI error code
 */
HRESULT ECTNEF::HrReadByte(IStream *lpStream, unsigned char *ulData)
{
	HRESULT hr;
	ULONG ulRead = 0;

	hr = lpStream->Read(ulData, 1, &ulRead);
	if(hr != hrSuccess)
		return hr;
	if (ulRead != 1)
		return MAPI_E_NOT_FOUND;
	return hrSuccess;
}

/**
 * Read a block of data from the stream, with given length. Will be
 * processes in blocks of 4096 bytes.
 *
 * @param[in]	lpStream	input stream to read one unsigned char from, stream automatically moves current cursor.
 * @param[out]	lpData		pre-allocated buffer of size given in ulLen
 * @param[in]	ulLen		Length to read from stream, and thus size of lpData
 * @retval MAPI_E_NOT_FOUND if stream was too short, other MAPI error code
 * @return MAPI error code
 */
HRESULT ECTNEF::HrReadData(IStream *lpStream, char *lpData, ULONG ulLen)
{
	HRESULT hr;
	ULONG ulRead = 0;
	ULONG ulToRead = 0;

	while(ulLen) {
		ulToRead = ulLen > 4096 ? 4096 : ulLen;

		hr = lpStream->Read(lpData, ulToRead, &ulRead);
		if(hr != hrSuccess)
			return hr;
		if (ulRead != ulToRead)
			return MAPI_E_NOT_FOUND;
		ulLen -= ulRead;
		lpData += ulRead;
	}
	return hrSuccess;
}

/**
 * Write one DWORD (32bits ULONG) to output stream
 *
 * @param[in,out]	lpStream	stream to write one ULONG to, stream automatically moves current cursor.
 * @param[in]		ulData		ULONG value to write in lpStream
 * @retval MAPI_E_NOT_FOUND if stream was not written the same bytes, other MAPI error code
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWriteDWord(IStream *lpStream, ULONG ulData)
{
	HRESULT hr;
	ULONG ulWritten = 0;

	hr = lpStream->Write(&ulData, sizeof(unsigned int), &ulWritten);
	if(hr != hrSuccess)
		return hr;
	if (ulWritten != sizeof(unsigned int))
		return MAPI_E_NOT_FOUND;
	return hrSuccess;
}

/**
 * Write one WORD (16bits unsigned short) to output stream
 *
 * @param[in,out]	lpStream	stream to write one unsigned short to, stream automatically moves current cursor.
 * @param[in]		ulData		unsigned short value to write in lpStream
 * @retval MAPI_E_NOT_FOUND if stream was not written the same bytes, other MAPI error code
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWriteWord(IStream *lpStream, unsigned short ulData)
{
	HRESULT hr;
	ULONG ulWritten = 0;

	hr = lpStream->Write(&ulData, sizeof(unsigned short), &ulWritten);
	if(hr != hrSuccess)
		return hr;
	if (ulWritten != sizeof(unsigned short))
		return MAPI_E_NOT_FOUND;
	return hrSuccess;
}

/**
 * Write one BYTE (8bits unsigned char) to output stream
 *
 * @param[in,out]	lpStream	stream to write one unsigned char to, stream automatically moves current cursor.
 * @param[in]		ulData		unsigned char value to write in lpStream
 * @retval MAPI_E_NOT_FOUND if stream was not written the same bytes, other MAPI error code
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWriteByte(IStream *lpStream, unsigned char ulData)
{
	HRESULT hr;
	ULONG ulWritten = 0;

	hr = lpStream->Write(&ulData, sizeof(unsigned char), &ulWritten);
	if(hr != hrSuccess)
		return hr;
	if (ulWritten != sizeof(unsigned char))
		return MAPI_E_NOT_FOUND;
	return hrSuccess;
}

/**
 * Write a block of data of given size to output stream
 *
 * @param[in,out]	lpStream	stream to write one unsigned char to, stream automatically moves current cursor.
 * @param[in]		ulData		unsigned char value to write in lpStream
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWriteData(IStream *lpStream, char *data, ULONG ulLen)
{
	HRESULT hr;
	ULONG ulWritten = 0;

	while(ulLen > 0) {
		hr = lpStream->Write(data, ulLen > 4096 ? 4096 : ulLen, &ulWritten);
		if(hr != hrSuccess)
			return hr;
		ulLen -= ulWritten;
		data += ulWritten;
	}
	return hrSuccess;
}

/**
 * TNEF uses the rather stupid checksum of adding all the bytes in the stream. 
 * Was TNEF coded by an intern or something ??
 *
 * @param[in]	lpStream		Input TNEF stream, this object will be unmodified
 * @param[out]	lpulChecksum	"Checksum" of the TNEF data
 * @return MAPI error code
 */
HRESULT ECTNEF::HrGetChecksum(IStream *lpStream, ULONG *lpulChecksum)
{
	HRESULT hr = hrSuccess;
	ULONG ulChecksum = 0;
	IStream *lpClone = NULL;
	LARGE_INTEGER zero = {{0,0}};
	ULONG ulRead = 0;
	unsigned char buffer[4096];
	unsigned int i = 0;

	hr = lpStream->Clone(&lpClone);
	if(hr != hrSuccess)
		goto exit;

	hr = lpClone->Seek(zero, STREAM_SEEK_SET, NULL);
	if(hr != hrSuccess)
		goto exit;

	while(TRUE) {
		hr = lpClone->Read(buffer, 4096, &ulRead);
		if(hr != hrSuccess)
			goto exit;

		if(ulRead == 0)
			break;

		for (i = 0; i < ulRead; ++i)
			ulChecksum += buffer[i];
	}

	*lpulChecksum = ulChecksum;

exit:
	if(lpClone)
		lpClone->Release();

	return hr;
}

/**
 * Create a TNEF checksum over a normal char buffer.
 *
 * @param[in]	lpData	Buffer containing TNEF data
 * @param[in]	ulLen	Length of lpData
 * @return TNEF checksum value
 */
ULONG ECTNEF::GetChecksum(char *lpData, unsigned int ulLen)
{
    ULONG ulChecksum = 0;
	for (unsigned int i = 0; i < ulLen; ++i)
		ulChecksum += lpData[i];
    return ulChecksum;
}

/** 
 * Copy stream data to another stream with given TNEF block id and level number.
 * 
 * @param[in,out] lpDestStream Stream to write data to
 * @param[in] lpSourceStream Stream to read data from
 * @param[in] ulBlockID TNEF block id number
 * @param[in] ulLevel TNEF level number
 * 
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWriteBlock(IStream *lpDestStream, IStream *lpSourceStream, ULONG ulBlockID, ULONG ulLevel)
{
    HRESULT hr;
    ULONG ulChecksum = 0;
    LARGE_INTEGER zero = {{0,0}};
    STATSTG         sStat;

    hr = HrWriteByte(lpDestStream, ulLevel);
    if(hr != hrSuccess)
		return hr;
    hr = HrGetChecksum(lpSourceStream, &ulChecksum);
    if(hr != hrSuccess)
		return hr;
    hr = lpSourceStream->Seek(zero, STREAM_SEEK_SET, NULL);
    if(hr != hrSuccess)
		return hr;
    hr = HrWriteDWord(lpDestStream, ulBlockID);
    if(hr != hrSuccess)
		return hr;
    hr = lpSourceStream->Stat(&sStat, STATFLAG_NONAME);
    if(hr != hrSuccess)
		return hr;
    hr = HrWriteDWord(lpDestStream, sStat.cbSize.QuadPart);
    if(hr != hrSuccess)
		return hr;
    hr = lpSourceStream->CopyTo(lpDestStream, sStat.cbSize, NULL, NULL);
    if(hr != hrSuccess)
		return hr;
    hr = HrWriteWord(lpDestStream, ulChecksum);
    if(hr != hrSuccess)
		return hr;
    return hrSuccess;
}

/** 
 * Write a buffer to a stream with given TNEF block id and level number.
 * 
 * @param[in,out] lpDestStream Stream to write data block in
 * @param[in] lpData Data block to write to stream
 * @param[in] ulLen Lenght of lpData
 * @param[in] ulBlockID TNEF Block ID number
 * @param[in] ulLevel TNEF Level number
 * 
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWriteBlock(IStream *lpDestStream, char *lpData, unsigned int ulLen, ULONG ulBlockID, ULONG ulLevel)
{
    HRESULT hr = hrSuccess;
    IStream *lpStream = NULL;
    
    hr = CreateStreamOnHGlobal(NULL, TRUE, &lpStream);
    if (hr != hrSuccess)
        goto exit;

    hr = lpStream->Write(lpData, ulLen, NULL);
    if (hr != hrSuccess)
        goto exit;
        
    hr = HrWriteBlock(lpDestStream, lpStream, ulBlockID, ulLevel);
    if (hr != hrSuccess)
        goto exit;
        
exit:
    if(lpStream)
        lpStream->Release();
        
    return hr;                                                                                                                     
}

/** 
 * Read a complete stream into a buffer. (Don't we have this function somewhere in common/ ?)
 * 
 * @param[in] lpStream stream to read into buffer and return as BYTE array, cursor will be at the end on return
 * @param[in] lpBase pointer to use with MAPIAllocateMore, cannot be NULL
 * @param[out] lppData New allocated (more) buffer with contents of stream
 * @param[out] lpulSize size of *lppData buffer
 * 
 * @return MAPI error code
 */
HRESULT ECTNEF::HrReadStream(IStream *lpStream, void *lpBase, BYTE **lppData, ULONG *lpulSize)
{
    HRESULT hr;
    STATSTG sStat;
    BYTE *lpBuffer = NULL;
    BYTE *lpWrite = NULL;
    ULONG ulSize = 0;
    ULONG ulRead = 0;

	if (lpStream == NULL || lpBase == NULL || lppData == NULL || lpulSize == NULL)
		return MAPI_E_INVALID_PARAMETER;
    hr = lpStream->Stat(&sStat, STATFLAG_NONAME);
    if(hr != hrSuccess)
		return hr;
    hr = MAPIAllocateMore(sStat.cbSize.QuadPart, lpBase, (void **)&lpBuffer);    
    if(hr != hrSuccess)
		return hr;
    lpWrite = lpBuffer;
    while(sStat.cbSize.QuadPart > 0) {
        hr = lpStream->Read(lpWrite, sStat.cbSize.QuadPart, &ulRead);
        if(hr != hrSuccess)
			return hr;
        lpWrite += ulRead;
        ulSize += ulRead;
        sStat.cbSize.QuadPart -= ulRead;
    }
        
    *lppData = lpBuffer;
    *lpulSize = ulSize;
    return hrSuccess;
}

/** @} */
