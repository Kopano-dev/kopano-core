/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
#include <memory>
#include <cstdint>
#include <mapidefs.h>
#include <mapiutil.h>
#include <mapiguid.h>
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include <kopano/Util.h>
#include <kopano/charset/convert.h>
#include <string>
#include "tnef.h"

namespace KC {

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
static const struct {
	const char *szScheduleClass, *szMAPIClass;
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
		if (strcasecmp(szSClass, sClassMap[i].szScheduleClass) == 0)
			return sClassMap[i].szMAPIClass;
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
ECTNEF::ECTNEF(ULONG f, IMessage *lpMessage, IStream *lpStream) :
	m_lpStream(lpStream), m_lpMessage(lpMessage), ulFlags(f)
{}

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
	memory_ptr<SPropValue> lpPropValue;
	STATSTG			sStatstg;
	ULONG ulRead = 0, ulTotal = 0;
	BYTE *wptr = NULL;

	if (lpStream == NULL || lppPropValue == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (PROP_TYPE(ulPropTag) != PT_BINARY && PROP_TYPE(ulPropTag) != PT_UNICODE)
		return MAPI_E_INVALID_TYPE;
	auto hr = lpStream->Stat(&sStatstg, 0);
	if(hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpPropValue);
	if(hr != hrSuccess)
		return hr;

	lpPropValue->ulPropTag = ulPropTag;

	if (PROP_TYPE(ulPropTag) == PT_BINARY) {
		lpPropValue->Value.bin.cb = (ULONG)sStatstg.cbSize.QuadPart;

		hr = MAPIAllocateMore((ULONG)sStatstg.cbSize.QuadPart, lpPropValue,
		     reinterpret_cast<void **>(&lpPropValue->Value.bin.lpb));
		if(hr != hrSuccess)
			return hr;
		wptr = lpPropValue->Value.bin.lpb;
	} else if (PROP_TYPE(ulPropTag) == PT_UNICODE) {
		hr = MAPIAllocateMore((ULONG)sStatstg.cbSize.QuadPart + sizeof(wchar_t),
		     lpPropValue, reinterpret_cast<void **>(&lpPropValue->Value.lpszW));
		if (hr != hrSuccess)
			return hr;
		// terminate unicode string
		lpPropValue->Value.lpszW[sStatstg.cbSize.QuadPart / sizeof(wchar_t)] = L'\0';
		wptr = (BYTE*)lpPropValue->Value.lpszW;
	}

	while (1) {
		hr = lpStream->Read(wptr + ulTotal, 4096, &ulRead);
		if (hr != hrSuccess)
			return hr;
		if (ulRead == 0)
			break;
		ulTotal += ulRead;
	}
	*lppPropValue = lpPropValue.release();
	return hrSuccess;
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
HRESULT ECTNEF::AddProps(ULONG flags, const SPropTagArray *lpPropList)
{
	memory_ptr<SPropTagArray> lpPropListMessage;
	memory_ptr<SPropValue> lpPropValue, lpStreamValue;
	SizedSPropTagArray(1, sPropTagArray);
	ULONG			cValue = 0;

	// Loop through all the properties on the message, and only
	// add those that we want to add to the list
	auto hr = m_lpMessage->GetPropList(MAPI_UNICODE, &~lpPropListMessage);
	if (hr != hrSuccess)
		return hr;

	for (unsigned int i = 0; i < lpPropListMessage->cValues; ++i) {
		/*
		 * Do not send properties in 0x67XX range, since these seem to
		 * be blacklisted in recent exchange servers, which causes
		 * exchange to drop the entire message.
		 */
		if (PROP_ID(lpPropListMessage->aulPropTag[i]) >= 0x6700 &&
		    PROP_ID(lpPropListMessage->aulPropTag[i]) <= 0x67FF)
			continue;
		// unable to save these properties
		if (PROP_TYPE(lpPropListMessage->aulPropTag[i]) == PT_OBJECT ||
		   PROP_TYPE(lpPropListMessage->aulPropTag[i]) == PT_UNSPECIFIED ||
		   PROP_TYPE(lpPropListMessage->aulPropTag[i]) == PT_NULL)
			continue;

		bool fPropTagInList = PropTagInPropList(lpPropListMessage->aulPropTag[i], lpPropList);
		bool a = flags & TNEF_PROP_INCLUDE && fPropTagInList;
		a     |= flags & TNEF_PROP_EXCLUDE && !fPropTagInList;
		if (!a)
			continue;
		sPropTagArray.cValues = 1;
		sPropTagArray.aulPropTag[0] = lpPropListMessage->aulPropTag[i];
		hr = m_lpMessage->GetProps(sPropTagArray, 0, &cValue, &~lpPropValue);
		if (hr == hrSuccess)
			lstProps.emplace_back(std::move(lpPropValue));

		object_ptr<IStream> lpStream;
		if (hr == MAPI_W_ERRORS_RETURNED && lpPropValue != NULL &&
		    lpPropValue->Value.err == MAPI_E_NOT_ENOUGH_MEMORY &&
		    m_lpMessage->OpenProperty(lpPropListMessage->aulPropTag[i], &IID_IStream, 0, 0, &~lpStream) == hrSuccess) {
			hr = StreamToPropValue(lpStream, lpPropListMessage->aulPropTag[i], &~lpStreamValue);
			if (hr == hrSuccess) {
				lstProps.emplace_back(std::move(lpStreamValue));
				lpStreamValue = NULL;
			}
		}
		// otherwise silently ignore the property
	}
	return hrSuccess;
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
HRESULT ECTNEF::ExtractProps(ULONG flags, SPropTagArray *lpPropList)
{
	ULONG ulSignature = 0, ulType = 0, ulSize = 0;
	unsigned short ulChecksum = 0, ulKey = 0;
	unsigned char ulComponent = 0;
	memory_ptr<char> lpBuffer;
	SPropValue sProp;
	std::unique_ptr<char[]> szSClass;
	// Attachments props
	memory_ptr<SPropValue> lpProp;
	std::unique_ptr<tnefattachment> lpTnefAtt;

	auto hr = HrReadDWord(m_lpStream, &ulSignature);
	if(hr != hrSuccess)
		return hr;

	// Check signature
	if (ulSignature != TNEF_SIGNATURE)
		return MAPI_E_CORRUPT_DATA;
	hr = HrReadWord(m_lpStream, &ulKey);
	if(hr != hrSuccess)
		return hr;

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

		hr = MAPIAllocateBuffer(ulSize, &~lpBuffer);
		if(hr != hrSuccess)
			goto exit;
		hr = HrReadData(m_lpStream, lpBuffer, ulSize);
		if(hr != hrSuccess)
			goto exit;
		hr = HrReadWord(m_lpStream, &ulChecksum);
		if(hr != hrSuccess)
			goto exit;

		// Loop through all the blocks of the TNEF data. We are only interested
		// in the properties block for now (0x00069003)
		switch(ulType) {
		case ATT_MAPI_PROPS:
			hr = HrReadPropStream(lpBuffer, ulSize, lstProps);
			if (hr != hrSuccess)
				goto exit;
			break;
		case ATT_MESSAGE_CLASS: /* PR_MESSAGE_CLASS */
			{
				szSClass.reset(new char[ulSize+1]);
				// NULL terminate the string
				memcpy(szSClass.get(), lpBuffer, ulSize);
				szSClass[ulSize] = 0;

				// We map the Schedule+ message class to the more modern MAPI message
				// class. The mapping should be correct as far as we can find ..
				auto szMAPIClass = FindMAPIClassByScheduleClass(szSClass.get());
				if(szMAPIClass == NULL)
					szMAPIClass = szSClass.get(); // mapping not found, use string from TNEF file

				sProp.ulPropTag = PR_MESSAGE_CLASS_A;
				sProp.Value.lpszA = const_cast<char *>(szMAPIClass);

				// We do a 'SetProps' now because we want to override the PR_MESSAGE_CLASS
				// setting, while Finish() never overrides already-present properties for
				// security reasons.

				m_lpMessage->SetProps(1, &sProp, NULL);
				break;
			}
		case 0x00050008: /* PR_OWNER_APPT_ID */
			if(ulSize == 4 && lpBuffer) {
				uint32_t tmp4;
				sProp.ulPropTag = PR_OWNER_APPT_ID;
				memcpy(&tmp4, lpBuffer.get(), sizeof(tmp4));
				sProp.Value.l = le32_to_cpu(tmp4);
				m_lpMessage->SetProps(1, &sProp, NULL);
			}
			break;
		case ATT_REQUEST_RES: /* PR_RESPONSE_REQUESTED */
			if(ulSize == 2 && lpBuffer) {
				uint16_t tmp2;
				sProp.ulPropTag = PR_RESPONSE_REQUESTED;
				memcpy(&tmp2, lpBuffer.get(), sizeof(tmp2));
				sProp.Value.b = le16_to_cpu(tmp2);
				m_lpMessage->SetProps(1, &sProp, NULL);
			}
			break;

// --- TNEF attachments ---
		case ATT_ATTACH_REND_DATA:
			// Start marker of attachment
		    if(ulSize == sizeof(struct AttachRendData) && lpBuffer) {
				auto lpData = reinterpret_cast<AttachRendData *>(lpBuffer.get());

				if (lpTnefAtt != nullptr && (lpTnefAtt->data != nullptr || !lpTnefAtt->lstProps.empty()))
					/* end marker previous attachment */
					lstAttachments.emplace_back(std::move(lpTnefAtt));

				lpTnefAtt.reset(new(std::nothrow) tnefattachment);
				if (lpTnefAtt == nullptr) {
					hr = MAPI_E_NOT_ENOUGH_MEMORY;
					goto exit;
				}
                lpTnefAtt->rdata = *lpData;
            }
			break;

		case ATT_ATTACH_TITLE: // PR_ATTACH_FILENAME
			if (!lpTnefAtt) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}

			hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpProp);
			if (hr != hrSuccess)
				goto exit;
			lpProp->ulPropTag = PR_ATTACH_FILENAME_A;
			hr = KAllocCopy(lpBuffer, ulSize, reinterpret_cast<void **>(&lpProp->Value.lpszA), lpProp);
			if (hr != hrSuccess)
				goto exit;
			lpTnefAtt->lstProps.emplace_back(std::move(lpProp));
			break;

		case ATT_ATTACH_META_FILE:
			// PR_ATTACH_RENDERING, extra icon information
			if (!lpTnefAtt) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}

			hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpProp);
			if (hr != hrSuccess)
				goto exit;
			lpProp->ulPropTag = PR_ATTACH_RENDERING;
			lpProp->Value.bin.cb = ulSize;
			hr = KAllocCopy(lpBuffer, ulSize, reinterpret_cast<void **>(&lpProp->Value.bin.lpb), lpProp);
			if (hr != hrSuccess)
				goto exit;
			lpTnefAtt->lstProps.emplace_back(std::move(lpProp));
			break;

		case ATT_ATTACH_DATA:
			// PR_ATTACH_DATA_BIN, will be set via OpenProperty() in ECTNEF::Finish()
			if (!lpTnefAtt) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			lpTnefAtt->size = ulSize;
			hr = KAllocCopy(lpBuffer, ulSize, &~lpTnefAtt->data);
			if (hr != hrSuccess)
				goto exit;
			break;

		case ATT_ATTACHMENT: // Attachment property stream
			if (!lpTnefAtt) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			hr = HrReadPropStream(lpBuffer, ulSize, lpTnefAtt->lstProps);
			if (hr != hrSuccess)
				goto exit;
			break;

		default:
			// Ignore this block
			break;
		}
	}

exit:
	if (lpTnefAtt != nullptr && (lpTnefAtt->data != nullptr || !lpTnefAtt->lstProps.empty()))
		/* attachment should be complete before adding */
		lstAttachments.emplace_back(std::move(lpTnefAtt));
	return hr;
}

/**
 * Write the properties from a list to the TNEF stream.
 *
 * @param[in,out]	lpStream	The TNEF stream to write to
 * @param[in]		proplist	std::list of properties to write in the stream.
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWritePropStream(IStream *lpStream, std::list<memory_ptr<SPropValue> > &proplist)
{
	HRESULT hr = HrWriteDWord(lpStream, proplist.size());
	if(hr != hrSuccess)
		return hr;

	for (const auto &p : proplist) {
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
	ULONG cNames = 0, ulLen = 0, ulMVProp = 0, ulCount = 0;
	memory_ptr<MAPINAMEID *> lppNames;
	convert_context converter;
	std::u16string ucs2;

	if(PROP_ID(lpProp->ulPropTag) >= 0x8000) {
		memory_ptr<SPropTagArray> lpsPropTagArray;
		// Get named property GUID and ID or name
		sPropTagArray.cValues = 1;
		sPropTagArray.aulPropTag[0] = lpProp->ulPropTag;

		hr = Util::HrCopyPropTagArray(sPropTagArray, &~lpsPropTagArray);
		if (hr != hrSuccess)
			return hr;
		hr = m_lpMessage->GetNamesFromIDs(&+lpsPropTagArray, NULL, 0, &cNames, &~lppNames);
		if(hr != hrSuccess)
			return hrSuccess;
		if (cNames == 0 || lppNames == nullptr || lppNames[0] == nullptr)
			return MAPI_E_INVALID_PARAMETER;

		// Write the property tag
		hr = HrWriteDWord(lpStream, lpProp->ulPropTag);
		if(hr != hrSuccess)
			return hr;
		hr = HrWriteData(lpStream, lppNames[0]->lpguid, sizeof(GUID));
		if(hr != hrSuccess)
			return hr;

		if(lppNames[0]->ulKind == MNID_ID) {
			hr = HrWriteDWord(lpStream, 0);
			if(hr != hrSuccess)
				return hr;
			hr = HrWriteDWord(lpStream, lppNames[0]->Kind.lID);
			if(hr != hrSuccess)
				return hr;
		} else {
			hr = HrWriteDWord(lpStream, 1);
			if(hr != hrSuccess)
				return hr;

			ucs2 = converter.convert_to<std::u16string>(lppNames[0]->Kind.lpwstrName);
			ulLen = ucs2.length() * sizeof(std::u16string::value_type) + sizeof(std::u16string::value_type);
			hr = HrWriteDWord(lpStream, ulLen);
			if(hr != hrSuccess)
				return hr;
			hr = HrWriteData(lpStream, ucs2.c_str(), ulLen);
			if(hr != hrSuccess)
				return hr;

			// Align to 4-byte boundary
			while(ulLen & 3) {
				hr = HrWriteByte(lpStream, 0);
				if(hr != hrSuccess)
					return hr;
				++ulLen;
			}
		}
	} else {
		// Write the property tag
		hr = HrWriteDWord(lpStream, lpProp->ulPropTag);
		if(hr != hrSuccess)
			return hr;
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
			return MAPI_E_INVALID_PARAMETER;
		}

		hr = HrWriteDWord(lpStream, ulCount);
	} else {
		ulCount = 1;
	}

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
				hr = HrWriteData(lpStream, &lpProp->Value.MVflt.lpflt[ulMVProp], sizeof(float));
			else
				hr = HrWriteData(lpStream, &lpProp->Value.flt, sizeof(float));
			break;
		case PT_APPTIME:
			if(lpProp->ulPropTag & MV_FLAG)
				hr = HrWriteData(lpStream, &lpProp->Value.MVat.lpat[ulMVProp], sizeof(double));
			else
				hr = HrWriteData(lpStream, &lpProp->Value.at, sizeof(double));
			break;
		case PT_DOUBLE:
			if(lpProp->ulPropTag & MV_FLAG)
				hr = HrWriteData(lpStream, &lpProp->Value.MVdbl.lpdbl[ulMVProp], sizeof(double));
			else
				hr = HrWriteData(lpStream, &lpProp->Value.dbl, sizeof(double));
			break;
		case PT_CURRENCY:
			if(lpProp->ulPropTag & MV_FLAG) {
				hr = HrWriteDWord(lpStream, lpProp->Value.MVcur.lpcur[ulMVProp].Lo);
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteDWord(lpStream, lpProp->Value.MVcur.lpcur[ulMVProp].Hi);
			} else {
				hr = HrWriteDWord(lpStream, lpProp->Value.cur.Lo);
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteDWord(lpStream, lpProp->Value.cur.Hi);
			}
			if (hr != hrSuccess)
				return hr;
			break;
		case PT_SYSTIME:
			if(lpProp->ulPropTag & MV_FLAG) {
				hr = HrWriteDWord(lpStream, lpProp->Value.MVft.lpft[ulMVProp].dwLowDateTime);
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteDWord(lpStream, lpProp->Value.MVft.lpft[ulMVProp].dwHighDateTime);
			} else {
				hr = HrWriteDWord(lpStream, lpProp->Value.ft.dwLowDateTime);
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteDWord(lpStream, lpProp->Value.ft.dwHighDateTime);
			}
			if (hr != hrSuccess)
				return hr;
			break;
		case PT_I8:
			if(lpProp->ulPropTag & MV_FLAG) {
				hr = HrWriteDWord(lpStream, lpProp->Value.MVli.lpli[ulMVProp].LowPart);
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteDWord(lpStream, lpProp->Value.MVli.lpli[ulMVProp].HighPart);
			} else {
				hr = HrWriteDWord(lpStream, lpProp->Value.li.LowPart);
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteDWord(lpStream, lpProp->Value.li.HighPart);
			}
			if (hr != hrSuccess)
				return hr;
			break;
		case PT_STRING8:
			if(lpProp->ulPropTag & MV_FLAG) {
				ulLen = strlen(lpProp->Value.MVszA.lppszA[ulMVProp])+1;

				hr = HrWriteDWord(lpStream, ulLen);
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteData(lpStream, lpProp->Value.MVszA.lppszA[ulMVProp], ulLen);
			} else {
				ulLen = strlen(lpProp->Value.lpszA)+1;

				hr = HrWriteDWord(lpStream, 1); // unknown why this is here
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteDWord(lpStream, ulLen);
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteData(lpStream, lpProp->Value.lpszA, ulLen);
			}
			if (hr != hrSuccess)
				return hr;

			// Align to 4-byte boundary
			while(ulLen & 3) {
				hr = HrWriteByte(lpStream, 0);
				if (hr != hrSuccess)
					return hr;
				++ulLen;
			}
			break;

		case PT_UNICODE:
			// Make sure we write UCS-2, since that's the format of PT_UNICODE in Win32.
			if(lpProp->ulPropTag & MV_FLAG) {
				ucs2 = converter.convert_to<std::u16string>(lpProp->Value.MVszW.lppszW[ulMVProp]);
				ulLen = ucs2.length() * sizeof(std::u16string::value_type) + sizeof(std::u16string::value_type);
				hr = HrWriteDWord(lpStream, ulLen);
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteData(lpStream, ucs2.c_str(), ulLen);
			} else {
				ucs2 = converter.convert_to<std::u16string>(lpProp->Value.lpszW);
				ulLen = ucs2.length() * sizeof(std::u16string::value_type) + sizeof(std::u16string::value_type);
				hr = HrWriteDWord(lpStream, 1); // unknown why this is here
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteDWord(lpStream, ulLen);
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteData(lpStream, ucs2.c_str(), ulLen);
			}
			if (hr != hrSuccess)
				return hr;

			// Align to 4-byte boundary
			while(ulLen & 3) {
				hr = HrWriteByte(lpStream, 0);
				if (hr != hrSuccess)
					return hr;
				++ulLen;
			}
			break;

        case PT_OBJECT:
			/* XXX: Possible UB? The object pointer is normally ->Value.lpszA in KC. */
		case PT_BINARY:
			if(lpProp->ulPropTag & MV_FLAG) {
				ulLen = lpProp->Value.MVbin.lpbin[ulMVProp].cb;
				hr = HrWriteDWord(lpStream, ulLen);
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteData(lpStream, lpProp->Value.MVbin.lpbin[ulMVProp].lpb, ulLen);
			} else {
				ulLen = lpProp->Value.bin.cb;
				hr = HrWriteDWord(lpStream, 1); // unknown why this is here
				if(hr != hrSuccess)
					return hr;
				hr = HrWriteDWord(lpStream, ulLen + (PROP_TYPE(lpProp->ulPropTag) == PT_OBJECT ? sizeof(GUID) : 0));
				if(hr != hrSuccess)
					return hr;
                if(PROP_TYPE(lpProp->ulPropTag) == PT_OBJECT)
					HrWriteData(lpStream, &IID_IStorage, sizeof(GUID));
				hr = HrWriteData(lpStream, lpProp->Value.bin.lpb, ulLen);
			}
			if (hr != hrSuccess)
				return hr;

			// Align to 4-byte boundary
			while(ulLen & 3) {
				hr = HrWriteByte(lpStream, 0);
				if (hr != hrSuccess)
					return hr;
				++ulLen;
			}
			break;

		case PT_CLSID:
			if (lpProp->ulPropTag & MV_FLAG)
				hr = HrWriteData(lpStream, &lpProp->Value.MVguid.lpguid[ulMVProp], sizeof(GUID));
			else
				hr = HrWriteData(lpStream, lpProp->Value.lpguid, sizeof(GUID));
			if (hr != hrSuccess)
				return hr;
			break;

		default:
			hr = MAPI_E_INVALID_PARAMETER;
		}
	}
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
HRESULT ECTNEF::HrReadPropStream(const char *lpBuffer, ULONG ulSize,
								 std::list<memory_ptr<SPropValue> > &proplist)
{
	ULONG ulRead = 0;
	memory_ptr<SPropValue> lpProp;
	uint32_t ulProps;
	memcpy(&ulProps, lpBuffer, sizeof(ulProps));
	ulProps = le32_to_cpu(ulProps);
	lpBuffer += 4;
	ulSize -= 4;

	// Loop through all the properties in the data and add them to our internal list
	while(ulProps) {
		auto hr = HrReadSingleProp(lpBuffer, ulSize, &ulRead, &~lpProp);
		if(hr != hrSuccess)
			return hr;
		ulSize -= ulRead;
		lpBuffer += ulRead;
		proplist.emplace_back(std::move(lpProp));
		--ulProps;
		if (ulRead & 3)
			// Skip padding
			lpBuffer += 4 - (ulRead & 3);
	}
	return hrSuccess;
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
HRESULT ECTNEF::HrReadSingleProp(const char *lpBuffer, ULONG ulSize,
    ULONG *lpulRead, LPSPropValue *lppProp)
{
	ULONG ulCount = 0, ulOrigSize = ulSize;
	memory_ptr<SPropValue> lpProp;
	GUID sGuid;
	MAPINAMEID sNameID, *lpNameID = &sNameID;
	memory_ptr<SPropTagArray> lpPropTags;
	std::wstring strUnicodeName;
	std::u16string ucs2;

	if(ulSize < 8)
		return MAPI_E_NOT_FOUND;

	uint32_t tmp4;
	memcpy(&tmp4, lpBuffer, sizeof(tmp4));
	unsigned int ulPropTag = le32_to_cpu(tmp4);
	lpBuffer += sizeof(ULONG);
	ulSize -= 4;
	auto hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpProp);
	if(hr != hrSuccess)
		return hr;

	if(PROP_ID(ulPropTag) >= 0x8000) {
		// Named property, first read GUID, then name/id
		if (ulSize < 24)
			return MAPI_E_CORRUPT_DATA;
		memcpy(&sGuid, lpBuffer, sizeof(GUID));

		lpBuffer += sizeof(GUID);
		ulSize -= sizeof(GUID);
		memcpy(&tmp4, lpBuffer, sizeof(tmp4));
		unsigned int ulIsNameId = le32_to_cpu(tmp4);
		lpBuffer += 4;
		ulSize -= 4;

		if(ulIsNameId != 0) {
			// A string name follows
			memcpy(&tmp4, lpBuffer, sizeof(tmp4));
			unsigned int ulLen = le32_to_cpu(tmp4);
			lpBuffer += 4;
			ulSize -= 4;
			if (ulLen > ulSize)
				return MAPI_E_CORRUPT_DATA;

			// copy through u16string so we can set the boundary to the given length
			ucs2.assign(reinterpret_cast<const std::u16string::value_type *>(lpBuffer), ulLen / sizeof(std::u16string::value_type));
			strUnicodeName = convert_to<std::wstring>(ucs2);
			sNameID.ulKind = MNID_STRING;
			sNameID.Kind.lpwstrName = const_cast<wchar_t *>(strUnicodeName.c_str());
			lpBuffer += ulLen;
			ulSize -= ulLen;
			// Re-align
			lpBuffer += ulLen & 3 ? 4 - (ulLen & 3) : 0;
			ulSize -= ulLen & 3 ? 4 - (ulLen & 3) : 0;
		} else {
			sNameID.ulKind = MNID_ID;
			memcpy(&tmp4, lpBuffer, sizeof(tmp4));
			sNameID.Kind.lID = le32_to_cpu(tmp4);
			lpBuffer += 4;
			ulSize -= 4;
		}

		sNameID.lpguid = &sGuid;
		hr = m_lpMessage->GetIDsFromNames(1, &lpNameID, MAPI_CREATE, &~lpPropTags);
		if(hr != hrSuccess)
			return hr;
		// Use the mapped ID, not the original ID. The original ID is discarded
		ulPropTag = CHANGE_PROP_TYPE(lpPropTags->aulPropTag[0], PROP_TYPE(ulPropTag));
	}

	if(ulPropTag & MV_FLAG) {
		if (ulSize < 4)
			return MAPI_E_CORRUPT_DATA;
		memcpy(&tmp4, lpBuffer, sizeof(tmp4));
		ulCount = le32_to_cpu(tmp4);
		lpBuffer += 4;
		ulSize -= 4;

		switch(PROP_TYPE(ulPropTag)) {
		case PT_MV_I2:
			lpProp->Value.MVi.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(unsigned short), lpProp, reinterpret_cast<void **>(&lpProp->Value.MVi.lpi));
			break;
		case PT_MV_LONG:
			lpProp->Value.MVl.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(ULONG), lpProp, reinterpret_cast<void **>(&lpProp->Value.MVl.lpl));
			break;
		case PT_MV_R4:
			lpProp->Value.MVflt.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(float), lpProp, reinterpret_cast<void **>(&lpProp->Value.MVflt.lpflt));
			break;
		case PT_MV_APPTIME:
			lpProp->Value.MVat.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(double), lpProp, reinterpret_cast<void **>(&lpProp->Value.MVat.lpat));
			break;
		case PT_MV_DOUBLE:
			lpProp->Value.MVdbl.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(double), lpProp, reinterpret_cast<void **>(&lpProp->Value.MVdbl.lpdbl));
			break;
		case PT_MV_CURRENCY:
			lpProp->Value.MVcur.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(CURRENCY), lpProp, reinterpret_cast<void **>(&lpProp->Value.MVcur.lpcur));
			break;
		case PT_MV_SYSTIME:
			lpProp->Value.MVft.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(FILETIME), lpProp, reinterpret_cast<void **>(&lpProp->Value.MVft.lpft));
			break;
		case PT_MV_I8:
			lpProp->Value.MVli.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(LARGE_INTEGER), lpProp, reinterpret_cast<void **>(&lpProp->Value.MVli.lpli));
			break;
		case PT_MV_STRING8:
			lpProp->Value.MVszA.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(char *), lpProp, reinterpret_cast<void **>(&lpProp->Value.MVszA.lppszA));
			break;
		case PT_MV_UNICODE:
			lpProp->Value.MVszW.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(wchar_t *),
			     lpProp, reinterpret_cast<void **>(&lpProp->Value.MVszW.lppszW));
			break;
		case PT_MV_BINARY:
			lpProp->Value.MVbin.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(SBinary), lpProp, reinterpret_cast<void **>(&lpProp->Value.MVbin.lpbin));
			break;
		case PT_MV_CLSID:
			lpProp->Value.MVguid.cValues = ulCount;
			hr = MAPIAllocateMore(ulCount * sizeof(GUID), lpProp, reinterpret_cast<void **>(&lpProp->Value.MVguid.lpguid));
			break;
		default:
			return MAPI_E_INVALID_PARAMETER;
		}
	} else {
		ulCount = 1;
	}

	if(hr != hrSuccess)
		return hr;

	lpProp->ulPropTag = ulPropTag;

	for (unsigned int ulMVProp = 0; ulMVProp < ulCount; ++ulMVProp) {
		uint16_t tmp2;
		switch(PROP_TYPE(ulPropTag) & ~MV_FLAG) {
		case PT_I2:
			memcpy(&tmp2, lpBuffer, sizeof(tmp2));
			if (ulPropTag & MV_FLAG)
				lpProp->Value.MVi.lpi[ulMVProp] = le16_to_cpu(tmp2);
			else
				lpProp->Value.i = le16_to_cpu(tmp2);
			lpBuffer += 4;
			ulSize -= 4;
			break;
		case PT_LONG:
			memcpy(&tmp4, lpBuffer, sizeof(tmp4));
			if(ulPropTag & MV_FLAG)
				lpProp->Value.MVl.lpl[ulMVProp] = le32_to_cpu(tmp4);
			else
				lpProp->Value.ul = le32_to_cpu(tmp4);
			lpBuffer += 4;
			ulSize -= 4;
			break;
		case PT_BOOLEAN: {
			BOOL tmp;
			memcpy(&tmp, lpBuffer, sizeof(tmp));
			lpProp->Value.b = le32_to_cpu(tmp);
			lpBuffer += 4;
			ulSize -= 4;
			break;
		}
		case PT_R4:
			static_assert(sizeof(float) == 4, "hard limitation of tnef code");
			if(ulPropTag & MV_FLAG)
				memcpy(&lpProp->Value.MVflt.lpflt[ulMVProp], lpBuffer, sizeof(float));
			else
				memcpy(&lpProp->Value.flt, lpBuffer, sizeof(float));
			lpBuffer += 4;
			ulSize -= 4;
			break;
		case PT_APPTIME:
			if (ulSize < 8)
				return MAPI_E_CORRUPT_DATA;
			static_assert(sizeof(double) == 8, "hard limitation of tnef code");
			if(ulPropTag & MV_FLAG)
				memcpy(&lpProp->Value.MVat.lpat[ulMVProp], lpBuffer, sizeof(double));
			else
				memcpy(&lpProp->Value.at, lpBuffer, sizeof(double));
			lpBuffer += 8;
			ulSize -= 8;
			break;
		case PT_DOUBLE:
			if (ulSize < 8)
				return MAPI_E_CORRUPT_DATA;
			if(ulPropTag & MV_FLAG)
				memcpy(&lpProp->Value.MVdbl.lpdbl[ulMVProp], lpBuffer, sizeof(double));
			else
				memcpy(&lpProp->Value.dbl, lpBuffer, sizeof(double));
			lpBuffer += 8;
			ulSize -= 8;
			break;
		case PT_CURRENCY:
			if (ulSize < 8)
				return MAPI_E_CORRUPT_DATA;
			memcpy(&tmp4, lpBuffer, sizeof(tmp4));
			if(ulPropTag & MV_FLAG) {
				lpProp->Value.MVcur.lpcur[ulMVProp].Lo = le32_to_cpu(tmp4);
				memcpy(&tmp4, lpBuffer + 4, sizeof(tmp4));
				lpProp->Value.MVcur.lpcur[ulMVProp].Hi = le32_to_cpu(tmp4);
			} else {
				lpProp->Value.cur.Lo = le32_to_cpu(tmp4);
				memcpy(&tmp4, lpBuffer + 4, sizeof(tmp4));
				lpProp->Value.cur.Hi = le32_to_cpu(tmp4);
			}

			lpBuffer += 8;
			ulSize -= 8;
			break;
		case PT_SYSTIME:
			if (ulSize < 8)
				return MAPI_E_CORRUPT_DATA;
			static_assert(sizeof(ULONG) == sizeof(DWORD), "tnef code restriction");
			memcpy(&tmp4, lpBuffer, sizeof(tmp4));
			if(ulPropTag & MV_FLAG) {
				lpProp->Value.MVft.lpft[ulMVProp].dwLowDateTime = le32_to_cpu(tmp4);
				memcpy(&tmp4, lpBuffer + 4, sizeof(tmp4));
				lpProp->Value.MVft.lpft[ulMVProp].dwHighDateTime = le32_to_cpu(tmp4);
			} else {
				lpProp->Value.ft.dwLowDateTime = le32_to_cpu(tmp4);
				memcpy(&tmp4, lpBuffer + 4, sizeof(tmp4));
				lpProp->Value.ft.dwHighDateTime = le32_to_cpu(tmp4);
			}
			lpBuffer += 8;
			ulSize -= 8;
			break;
		case PT_I8:
			if (ulSize < 8)
				return MAPI_E_CORRUPT_DATA;
			memcpy(&tmp4, lpBuffer, sizeof(tmp4));
			if(ulPropTag & MV_FLAG) {
				lpProp->Value.MVli.lpli[ulMVProp].LowPart = le32_to_cpu(tmp4);
				memcpy(&tmp4, lpBuffer + 4, sizeof(tmp4));
				lpProp->Value.MVli.lpli[ulMVProp].HighPart = le32_to_cpu(tmp4);
			} else {
				lpProp->Value.li.LowPart = le32_to_cpu(tmp4);
				memcpy(&tmp4, lpBuffer + 4, sizeof(tmp4));
				lpProp->Value.li.HighPart = le32_to_cpu(tmp4);
			}

			lpBuffer += 8;
			ulSize -= 8;
			break;
		case PT_STRING8: {
			if (ulSize < 8)
				return MAPI_E_CORRUPT_DATA;
			if((PROP_TYPE(ulPropTag) & MV_FLAG) == 0) {
    			lpBuffer += 4; // Skip next 4 bytes, they are always '1'
	    		ulSize -= 4;
            }
			memcpy(&tmp4, lpBuffer, sizeof(tmp4));
			unsigned int ulLen = le32_to_cpu(tmp4);
			lpBuffer += 4;
			ulSize -= 4;

			if (ulSize < ulLen)
				return MAPI_E_CORRUPT_DATA;
			if(ulPropTag & MV_FLAG) {
				hr = MAPIAllocateMore(ulLen + 1, lpProp, reinterpret_cast<void **>(&lpProp->Value.MVszA.lppszA[ulMVProp]));
				if(hr != hrSuccess)
					return hr;
				memcpy(lpProp->Value.MVszA.lppszA[ulMVProp], lpBuffer, ulLen);
				lpProp->Value.MVszA.lppszA[ulMVProp][ulLen] = 0; // should be terminated anyway but we terminte it just to be sure
			} else {
				hr = MAPIAllocateMore(ulLen + 1, lpProp, reinterpret_cast<void **>(&lpProp->Value.lpszA));
				if(hr != hrSuccess)
					return hr;
				memcpy(lpProp->Value.lpszA, lpBuffer, ulLen);
				lpProp->Value.lpszA[ulLen] = 0; // should be terminated anyway but we terminte it just to be sure
			}

			lpBuffer += ulLen;
			ulSize -= ulLen;
			// Re-align
			lpBuffer += ulLen & 3 ? 4 - (ulLen & 3) : 0;
			ulSize -= ulLen & 3 ? 4 - (ulLen & 3) : 0;
			break;
		}
		case PT_UNICODE: {
			// Make sure we read UCS-2, since that is the format of PT_UNICODE in Win32.
			if (ulSize < 8)
				return MAPI_E_CORRUPT_DATA;
			if((PROP_TYPE(ulPropTag) & MV_FLAG) == 0) {
    			lpBuffer += 4; // Skip next 4 bytes, they are always '1'
	    		ulSize -= 4;
            }
			memcpy(&tmp4, lpBuffer, sizeof(tmp4)); // Assumes 'len' in file is BYTES, not chars
			unsigned int ulLen = le32_to_cpu(tmp4);
			lpBuffer += 4;
			ulSize -= 4;
			if (ulSize < ulLen)
				return MAPI_E_CORRUPT_DATA;

			// copy through u16string so we can set the boundary to the given length
			ucs2.assign(reinterpret_cast<const std::u16string::value_type *>(lpBuffer), ulLen / sizeof(std::u16string::value_type));
			strUnicodeName = convert_to<std::wstring>(ucs2);

			if(ulPropTag & MV_FLAG) {
				hr = MAPIAllocateMore((strUnicodeName.length() + 1) * sizeof(wchar_t),
				     lpProp, reinterpret_cast<void **>(&lpProp->Value.MVszW.lppszW[ulMVProp]));
				if(hr != hrSuccess)
					return hr;
				wcscpy(lpProp->Value.MVszW.lppszW[ulMVProp], strUnicodeName.c_str());
			} else {
				hr = MAPIAllocateMore((strUnicodeName.length() + 1) * sizeof(wchar_t),
				     lpProp, reinterpret_cast<void **>(&lpProp->Value.lpszW));
				if(hr != hrSuccess)
					return hr;
				wcscpy(lpProp->Value.lpszW, strUnicodeName.c_str());
			}

			lpBuffer += ulLen;
			ulSize -= ulLen;
			// Re-align
			lpBuffer += ulLen & 3 ? 4 - (ulLen & 3) : 0;
			ulSize -= ulLen & 3 ? 4 - (ulLen & 3) : 0;
			break;
		}
		case PT_OBJECT:			// PST sends PT_OBJECT data. Treat as PT_BINARY
		case PT_BINARY: {
			if (ulSize < 8)
				return MAPI_E_CORRUPT_DATA;
			if((PROP_TYPE(ulPropTag) & MV_FLAG) == 0) {
    			lpBuffer += 4;	// Skip next 4 bytes, it's always '1' (ULONG)
	    		ulSize -= 4;
            }
			memcpy(&tmp4, lpBuffer, sizeof(tmp4));
			unsigned int ulLen = le32_to_cpu(tmp4);
			lpBuffer += 4;
			ulSize -= 4;

			if (PROP_TYPE(ulPropTag) == PT_OBJECT) {
			    // Can be IID_IMessage, IID_IStorage, IID_IStream (and possibly others)
				lpBuffer += 16;
				ulSize -= 16;
				ulLen -= 16;
			}
			if (ulSize < ulLen)
				return MAPI_E_CORRUPT_DATA;
			if(ulPropTag & MV_FLAG) {
				lpProp->Value.MVbin.lpbin[ulMVProp].cb = ulLen;
				hr = KAllocCopy(lpBuffer, ulLen, reinterpret_cast<void **>(&lpProp->Value.MVbin.lpbin[ulMVProp].lpb), lpProp);
				if(hr != hrSuccess)
					return hr;
			} else {
				lpProp->Value.bin.cb = ulLen;
				hr = KAllocCopy(lpBuffer, ulLen, reinterpret_cast<void **>(&lpProp->Value.bin.lpb), lpProp);
				if(hr != hrSuccess)
					return hr;
			}

			lpBuffer += ulLen;
			ulSize -= ulLen;
			// Re-align
			lpBuffer += ulLen & 3 ? 4 - (ulLen & 3) : 0;
			ulSize -= ulLen & 3 ? 4 - (ulLen & 3) : 0;
			break;
		}
		case PT_CLSID:
			if (ulSize < sizeof(GUID))
				return MAPI_E_CORRUPT_DATA;
			if(ulPropTag & MV_FLAG) {
				memcpy(&lpProp->Value.MVguid.lpguid[ulMVProp], lpBuffer, sizeof(GUID));
			} else {
				hr = KAllocCopy(lpBuffer, sizeof(GUID), reinterpret_cast<void **>(&lpProp->Value.lpguid), lpProp);
				if (hr != hrSuccess)
					return hr;
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
	*lppProp = lpProp.release();
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
	for (unsigned int i = 0; i < cValues; ++i)
		lstProps.emplace_back(&lpProps[i]);
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
HRESULT ECTNEF::FinishComponent(ULONG flags, ULONG ulComponentID,
    const SPropTagArray *lpPropList)
{
	object_ptr<IAttach> lpAttach;
	memory_ptr<SPropValue> lpProps, lpAttachProps, lpsNewProp;
	object_ptr<IStream> lpStream;
    ULONG cValues = 0;
    AttachRendData sData;
	static constexpr const SizedSPropTagArray(2, sptaTags) =
		{2, {PR_ATTACH_METHOD, PR_RENDERING_POSITION}};
	auto sTnefAttach = make_unique_nt<tnefattachment>();
	if (sTnefAttach == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	if (flags != TNEF_COMPONENT_ATTACHMENT)
		return MAPI_E_NO_SUPPORT;
	if (ulFlags != TNEF_ENCODE)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = m_lpMessage->OpenAttach(ulComponentID, &IID_IAttachment, 0, &~lpAttach);
    if(hr != hrSuccess)
		return hr;

    // Get some properties we always need
	hr = lpAttach->GetProps(sptaTags, 0, &cValues, &~lpAttachProps);
    if(FAILED(hr))
		return hr;

    memset(&sData, 0, sizeof(sData));
    sData.usType =     lpAttachProps[0].ulPropTag == PR_ATTACH_METHOD && lpAttachProps[0].Value.ul == ATTACH_OLE ? AttachTypeOle : AttachTypeFile;
    sData.ulPosition = lpAttachProps[1].ulPropTag == PR_RENDERING_POSITION ? lpAttachProps[1].Value.ul : 0;

    // Get user-passed properties
	hr = lpAttach->GetProps(lpPropList, 0, &cValues, &~lpProps);
    if(FAILED(hr))
		return hr;

    for (unsigned int i = 0; i < cValues; ++i) {
        // Other properties
        if(PROP_TYPE(lpProps[i].ulPropTag) == PT_ERROR)
            continue;
		hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpsNewProp);
        if(hr != hrSuccess)
			return hr;

        if(PROP_TYPE(lpProps[i].ulPropTag) == PT_OBJECT) {
            // PT_OBJECT requested, open object as stream and read the data
            hr = lpAttach->OpenProperty(lpProps[i].ulPropTag, &IID_IStream, 0, 0, &~lpStream);
            if(hr != hrSuccess)
				return hr;
            // We save the actual data same way as PT_BINARY
            hr = HrReadStream(lpStream, lpsNewProp, &lpsNewProp->Value.bin.lpb, &lpsNewProp->Value.bin.cb);
            if(hr != hrSuccess)
				return hr;
            lpsNewProp->ulPropTag = lpProps[i].ulPropTag;
        } else {
            hr = Util::HrCopyProperty(lpsNewProp, &lpProps[i], lpsNewProp);
            if(hr != hrSuccess)
			return hr;
        }
        sTnefAttach->lstProps.emplace_back(std::move(lpsNewProp));
    }

    sTnefAttach->rdata = sData;
    sTnefAttach->data = NULL;
    sTnefAttach->size = 0;
    lstAttachments.emplace_back(std::move(sTnefAttach));
    return hrSuccess;
}

static inline bool is_embedded_msg(const std::list<memory_ptr<SPropValue>> &proplist)
{
	for (const auto &pp : proplist)
		if (pp->ulPropTag == PR_ATTACH_METHOD &&
		    pp->Value.ul == ATTACH_EMBEDDED_MSG)
			return true;
	return false;
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
	STATSTG sStat;
	ULONG ulChecksum, ulAttachNum;
	LARGE_INTEGER zero = {{0,0}};
	ULARGE_INTEGER uzero = {{0,0}};
	// attachment vars
	object_ptr<IMessage> lpAttMessage;
	SPropValue sProp;

	if(ulFlags == TNEF_DECODE) {
		// Write properties to message
		for (const auto &p : lstProps) {
			if (PROP_ID(p->ulPropTag) == PROP_ID(PR_MESSAGE_CLASS) ||
			    !FPropExists(m_lpMessage, p->ulPropTag) ||
			    PROP_ID(p->ulPropTag) == PROP_ID(PR_RTF_COMPRESSED) ||
			    PROP_ID(p->ulPropTag) == PROP_ID(PR_HTML) ||
			    PROP_ID(p->ulPropTag) == PROP_ID(PR_INTERNET_CPID))
  				m_lpMessage->SetProps(1, p, NULL);
			// else, Property already exists, do *not* overwrite it
		}
		// Add all found attachments to message
		for (const auto &att : lstAttachments) {
			object_ptr<IAttach> lpAttach;
			bool has_obj = false;

			auto hr = m_lpMessage->CreateAttach(nullptr, 0, &ulAttachNum, &~lpAttach);
			if (hr != hrSuccess)
				return hr;

			sProp.ulPropTag = PR_ATTACH_METHOD;
			if (att->rdata.usType == AttachTypeOle)
				sProp.Value.ul = ATTACH_OLE;
			else if (is_embedded_msg(att->lstProps))
				sProp.Value.ul = ATTACH_EMBEDDED_MSG;
			else
				sProp.Value.ul = ATTACH_BY_VALUE;

			lpAttach->SetProps(1, &sProp, NULL);

			sProp.ulPropTag = PR_RENDERING_POSITION;
			sProp.Value.ul = att->rdata.ulPosition;
			lpAttach->SetProps(1, &sProp, NULL);

			for (const auto &p : att->lstProps) {
				// must not set PR_ATTACH_NUM by ourselves
				if (PROP_ID(p->ulPropTag) == PROP_ID(PR_ATTACH_NUM))
					continue;

				if (p->ulPropTag != PR_ATTACH_DATA_OBJ) {
					hr = lpAttach->SetProps(1, p, NULL);
					if (hr != hrSuccess)
						return hr;
					continue;
				} else if (att->rdata.usType == AttachTypeOle) {
					// message in PT_OBJECT, was saved in Value.bin
					object_ptr<IStream> lpSubStream;
					hr = lpAttach->OpenProperty(p->ulPropTag, &IID_IStream, 0, MAPI_CREATE | MAPI_MODIFY, &~lpSubStream);
					if (hr != hrSuccess)
						return hr;
					hr = lpSubStream->Write(p->Value.bin.lpb, p->Value.bin.cb, NULL);
					if (hr != hrSuccess)
						return hr;
					hr = lpSubStream->Commit(0);
					if (hr != hrSuccess)
						return hr;
					has_obj = true;
					continue;
				}

				object_ptr<IStream> lpSubStream;
				hr = CreateStreamOnHGlobal(nullptr, true, &~lpSubStream);
				if (hr != hrSuccess)
					return hr;
				hr = lpSubStream->Write(p->Value.bin.lpb, p->Value.bin.cb, NULL);
				if (hr != hrSuccess)
					return hr;
				hr = lpSubStream->Seek(zero, STREAM_SEEK_SET, NULL);
				if (hr != hrSuccess)
					return hr;
				hr = lpAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY, &~lpAttMessage);
				if (hr != hrSuccess)
					return hr;

				ECTNEF SubTNEF(TNEF_DECODE, lpAttMessage, lpSubStream);
				hr = SubTNEF.ExtractProps(TNEF_PROP_EXCLUDE, NULL);
				if (hr != hrSuccess)
					return hr;
				hr = SubTNEF.Finish();
				if (hr != hrSuccess)
					return hr;
				hr = lpAttMessage->SaveChanges(0);
				if (hr != hrSuccess)
					return hr;
				has_obj = true;
			}
			if (!has_obj && att->data != NULL) {
				object_ptr<IStream> lpAttStream;

				hr = lpAttach->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &~lpAttStream);
				if (hr != hrSuccess)
					return hr;
				hr = lpAttStream->Write(att->data, att->size,NULL);
				if (hr != hrSuccess)
					return hr;
				hr = lpAttStream->Commit(0);
				if (hr != hrSuccess)
					return hr;
			}

			hr = lpAttach->SaveChanges(0);
			if (hr != hrSuccess)
				return hr;
		}
		return hrSuccess;
	} else if (ulFlags != TNEF_ENCODE) {
		return hrSuccess;
	}

	// Write properties to stream
	auto hr = HrWriteDWord(m_lpStream, TNEF_SIGNATURE);
	if (hr != hrSuccess)
		return hr;
	hr = HrWriteWord(m_lpStream, 0); // Write Key
	if (hr != hrSuccess)
		return hr;
	hr = HrWriteByte(m_lpStream, 1); // Write component (always 1 ?)
	if (hr != hrSuccess)
		return hr;
	object_ptr<IStream> lpPropStream;
	hr = CreateStreamOnHGlobal(nullptr, TRUE, &~lpPropStream);
	if (hr != hrSuccess)
		return hr;
	hr = HrWritePropStream(lpPropStream, lstProps);
	if (hr != hrSuccess)
		return hr;
	hr = lpPropStream->Stat(&sStat, STATFLAG_NONAME);
	if (hr != hrSuccess)
		return hr;
	hr = HrWriteDWord(m_lpStream, 0x00069003);
	if (hr != hrSuccess)
		return hr;
	hr = HrWriteDWord(m_lpStream, sStat.cbSize.LowPart); // Write size
	if (hr != hrSuccess)
		return hr;
	hr = lpPropStream->Seek(zero, STREAM_SEEK_SET, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = lpPropStream->CopyTo(m_lpStream, sStat.cbSize, NULL, NULL); // Write data
	if (hr != hrSuccess)
		return hr;
	hr = lpPropStream->Seek(zero, STREAM_SEEK_SET, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetChecksum(lpPropStream, &ulChecksum);
	if (hr != hrSuccess)
		return hr;
	hr = HrWriteWord(m_lpStream, (unsigned short)ulChecksum); // Write checksum
	if (hr != hrSuccess)
		return hr;

	// Write attachments
	for (const auto &att : lstAttachments) {
		/* Write attachment start block */
		hr = HrWriteBlock(m_lpStream, reinterpret_cast<char *>(&att->rdata), sizeof(AttachRendData), 0x00069002, 2);
		if (hr != hrSuccess)
			return hr;

		// Write attachment data block if available
		if (att->data != NULL) {
			hr = HrWriteBlock(m_lpStream, reinterpret_cast<char *>(att->data.get()), att->size, 0x0006800f, 2);
			if (hr != hrSuccess)
				return hr;
		}

		// Write property block
		hr = lpPropStream->SetSize(uzero);
		if (hr != hrSuccess)
			return hr;
		hr = HrWritePropStream(lpPropStream, att->lstProps);
		if (hr != hrSuccess)
				return hr;
		hr = HrWriteBlock(m_lpStream, lpPropStream, 0x00069005, 2);
		if (hr != hrSuccess)
			return hr;
		// Note that we don't write any other blocks like PR_ATTACH_FILENAME since this information is also in the property block
	}
	return hrSuccess;
}

/**
 * Read one DWORD (32-bit unsigned integer) from input stream
 *
 * @param[in]	lpStream	input stream to read one ULONG from, stream automatically moves current cursor.
 * @param[out]	ulData		ULONG value from lpStream
 * @retval MAPI_E_NOT_FOUND if stream was too short, other MAPI error code
 * @return MAPI error code
 */
HRESULT ECTNEF::HrReadDWord(IStream *lpStream, uint32_t *value)
{
	ULONG ulRead = 0;
	auto hr = lpStream->Read(value, sizeof(*value), &ulRead);
	if(hr != hrSuccess)
		return hr;
	if (ulRead != sizeof(*value))
		return MAPI_E_NOT_FOUND;
	*value = le32_to_cpu(*value);
	return hrSuccess;
}

/**
 * Read one WORD (16-bit unsigned integer) from input stream
 *
 * @param[in]	lpStream	input stream to read one unsigned short from, stream automatically moves current cursor.
 * @param[out]	ulData		unsigned short value from lpStream
 * @retval MAPI_E_NOT_FOUND if stream was too short, other MAPI error code
 * @return MAPI error code
 */
HRESULT ECTNEF::HrReadWord(IStream *lpStream, uint16_t *value)
{
	ULONG ulRead = 0;
	auto hr = lpStream->Read(value, sizeof(*value), &ulRead);
	if(hr != hrSuccess)
		return hr;
	if (ulRead != sizeof(unsigned short))
		return MAPI_E_NOT_FOUND;
	*value = le16_to_cpu(*value);
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
	ULONG ulRead = 0;
	auto hr = lpStream->Read(ulData, 1, &ulRead);
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
HRESULT ECTNEF::HrReadData(IStream *lpStream, void *data, size_t ulLen)
{
	auto lpData = static_cast<char *>(data);
	ULONG ulRead = 0;

	while(ulLen) {
		auto ulToRead = std::min(ulLen, static_cast<size_t>(4096));
		auto hr = lpStream->Read(lpData, ulToRead, &ulRead);
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
 * Write one DWORD (32-bit integer) to output stream
 *
 * @param[in,out]	lpStream	stream to write one ULONG to, stream automatically moves current cursor.
 * @param[in]		ulData		ULONG value to write in lpStream
 * @retval MAPI_E_NOT_FOUND if stream was not written the same bytes, other MAPI error code
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWriteDWord(IStream *lpStream, uint32_t value)
{
	ULONG ulWritten = 0;
	value = cpu_to_le32(value);
	auto hr = lpStream->Write(&value, sizeof(value), &ulWritten);
	if(hr != hrSuccess)
		return hr;
	if (ulWritten != sizeof(unsigned int))
		return MAPI_E_NOT_FOUND;
	return hrSuccess;
}

/**
 * Write one WORD (16-bit unsigned integer) to output stream
 *
 * @param[in,out]	lpStream	stream to write one unsigned short to, stream automatically moves current cursor.
 * @param[in]		ulData		unsigned short value to write in lpStream
 * @retval MAPI_E_NOT_FOUND if stream was not written the same bytes, other MAPI error code
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWriteWord(IStream *lpStream, uint16_t value)
{
	ULONG ulWritten = 0;
	value = cpu_to_le16(value);
	auto hr = lpStream->Write(&value, sizeof(value), &ulWritten);
	if(hr != hrSuccess)
		return hr;
	if (ulWritten != sizeof(unsigned short))
		return MAPI_E_NOT_FOUND;
	return hrSuccess;
}

/**
 * Write one BYTE (8-bit unsigned integer) to output stream
 *
 * @param[in,out]	lpStream	stream to write one unsigned char to, stream automatically moves current cursor.
 * @param[in]		ulData		unsigned char value to write in lpStream
 * @retval MAPI_E_NOT_FOUND if stream was not written the same bytes, other MAPI error code
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWriteByte(IStream *lpStream, unsigned char ulData)
{
	ULONG ulWritten = 0;
	auto hr = lpStream->Write(&ulData, 1, &ulWritten);
	if(hr != hrSuccess)
		return hr;
	if (ulWritten != 1)
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
HRESULT ECTNEF::HrWriteData(IStream *lpStream, const void *vdata, size_t ulLen)
{
	auto data = static_cast<const char *>(vdata);
	ULONG ulWritten = 0;

	while(ulLen > 0) {
		auto hr = lpStream->Write(data, std::min(ulLen, static_cast<size_t>(4096)), &ulWritten);
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
	ULONG ulChecksum = 0;
	object_ptr<IStream> lpClone;
	LARGE_INTEGER zero = {{0,0}};
	ULONG ulRead = 0;
	unsigned char buffer[4096];
	unsigned int i = 0;

	auto hr = lpStream->Clone(&~lpClone);
	if(hr != hrSuccess)
		return hr;
	hr = lpClone->Seek(zero, STREAM_SEEK_SET, NULL);
	if(hr != hrSuccess)
		return hr;

	while(TRUE) {
		hr = lpClone->Read(buffer, 4096, &ulRead);
		if(hr != hrSuccess)
			return hr;
		if(ulRead == 0)
			break;
		for (i = 0; i < ulRead; ++i)
			ulChecksum += buffer[i];
	}

	*lpulChecksum = ulChecksum;
	return hrSuccess;
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
    ULONG ulChecksum = 0;
    LARGE_INTEGER zero = {{0,0}};
    STATSTG         sStat;

	auto hr = HrWriteByte(lpDestStream, ulLevel);
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
	return HrWriteWord(lpDestStream, ulChecksum);
}

/**
 * Write a buffer to a stream with given TNEF block id and level number.
 *
 * @param[in,out] lpDestStream Stream to write data block in
 * @param[in] lpData Data block to write to stream
 * @param[in] ulLen Length of lpData
 * @param[in] ulBlockID TNEF Block ID number
 * @param[in] ulLevel TNEF Level number
 *
 * @return MAPI error code
 */
HRESULT ECTNEF::HrWriteBlock(IStream *lpDestStream, const char *lpData,
    unsigned int ulLen, ULONG ulBlockID, ULONG ulLevel)
{
	object_ptr<IStream> lpStream;
	auto hr = CreateStreamOnHGlobal(nullptr, TRUE, &~lpStream);
    if (hr != hrSuccess)
		return hr;
    hr = lpStream->Write(lpData, ulLen, NULL);
    if (hr != hrSuccess)
		return hr;
	return HrWriteBlock(lpDestStream, lpStream, ulBlockID, ulLevel);
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
    STATSTG sStat;
	BYTE *lpBuffer = nullptr;
	ULONG ulSize = 0, ulRead = 0;

	if (lpStream == NULL || lpBase == NULL || lppData == NULL || lpulSize == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = lpStream->Stat(&sStat, STATFLAG_NONAME);
    if(hr != hrSuccess)
		return hr;
	hr = MAPIAllocateMore(sStat.cbSize.QuadPart, lpBase, reinterpret_cast<void **>(&lpBuffer));
    if(hr != hrSuccess)
		return hr;
	auto lpWrite = lpBuffer;
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

} /* namespace */

/** @} */
