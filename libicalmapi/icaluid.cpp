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
#include "icaluid.h"
#include <mapix.h>
#include <kopano/stringutil.h>

/**
 * Check if UID is of outlook format.
 *
 * The UID will start with a static GUID defined by Microsoft.
 *
 * @param[in]	strUid		input string UID to be checked
 * @return		true: outlook format, false: assume ical format
 */
bool IsOutlookUid(const std::string &strUid)
{
	std::string strByteArrayID = "040000008200E00074C5B7101A82E008";
	return (strUid.compare(0, strByteArrayID.length(), strByteArrayID) == 0);
}

/**
 * Generates a new UID in outlook format. Format is described in VConverter::HrMakeBinaryUID
 *
 * @param[out]	lpStrData	returned generated UID string
 * @return		MAPI error code
 */
HRESULT HrGenerateUid(std::string *lpStrData)
{
	std::string strByteArrayID = "040000008200E00074C5B7101A82E008";
	std::string strBinUid;
	GUID sGuid;
	FILETIME ftNow;
	ULONG ulSize = 1;

	HRESULT hr = CoCreateGuid(&sGuid);
	if (hr != hrSuccess)
		return hr;
	hr = UnixTimeToFileTime(time(NULL), &ftNow);
	if (hr != hrSuccess)
		return hr;
	strBinUid = strByteArrayID;	// Outlook Guid
	strBinUid += "00000000";	// InstanceDate
	strBinUid += bin2hex(sizeof(FILETIME), (LPBYTE)&ftNow);
	strBinUid += "0000000000000000"; // Padding
	strBinUid += bin2hex(sizeof(ULONG), (LPBYTE)&ulSize); // always 1
	strBinUid += bin2hex(sizeof(GUID), (LPBYTE)&sGuid);	// new guid

	lpStrData->swap(strBinUid);
	return hrSuccess;
}

/**
 * Generates new UID and sets it in SpropValue structure
 *
 * @param[in]	ulNamedTag		Property tag to set in returned property
 * @param[in]	base			base for allocating memory, can be NULL and returned *lppPropVal must be freed using MAPIFreeBuffer
 * @param[out]	lppPropVal		returned SpropValue structure
 * @return		MAPI error code
 */
HRESULT HrCreateGlobalID(ULONG ulNamedTag, void *base, LPSPropValue *lppPropVal)
{
	HRESULT hr = hrSuccess;
	LPSPropValue lpPropVal = NULL;
	std::string strUid, strBinUid;

	if (base) {
		hr = MAPIAllocateMore(sizeof(SPropValue), base, (void**)&lpPropVal);
	} else {
		hr = MAPIAllocateBuffer(sizeof(SPropValue), (void**)&lpPropVal);
		base = lpPropVal;
	}
	if (hr != hrSuccess)
		goto exit;

	lpPropVal->ulPropTag = ulNamedTag;

	hr = HrGenerateUid(&strUid);
	if (hr != hrSuccess)
		goto exit;

	strBinUid = hex2bin(strUid);

	lpPropVal->Value.bin.cb = strBinUid.length();

	hr = MAPIAllocateMore(lpPropVal->Value.bin.cb, base, (void**)&lpPropVal->Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;

	memcpy(lpPropVal->Value.bin.lpb, strBinUid.data(), lpPropVal->Value.bin.cb);

	*lppPropVal = lpPropVal;

exit:
	if (hr != hrSuccess && lpPropVal)
		MAPIFreeBuffer(lpPropVal);

	return hr;
}

/**
 * Extracts ical UID from the binary outlook UID
 *
 * If the UID is an outlook wrapped ical UID, the ical part is
 * returned, otherwise a hex string representation of the UID is
 * returned.
 *
 * @param[in]	sBin		binary outlook UID
 * @param[out]	lpStrUid	returned ical string UID 
 * @return		MAPI error code
 */
HRESULT HrGetICalUidFromBinUid(SBinary &sBin, std::string *lpStrUid)
{
	HRESULT hr = hrSuccess;
	std::string strUid;

	if (sBin.cb > 0x34 && memcmp(sBin.lpb + 0x28, "vCal-Uid", 8) == 0) {
		strUid = (char*)sBin.lpb + 0x34;
	} else {
		strUid = bin2hex(sBin.cb, sBin.lpb);
	}

	lpStrUid->swap(strUid);

	return hr;
}

/**
 * Converts ical UID to Outlook compatible UIDs.
 *
 * Add a special Outlook GUID and marker to the Ical UID. This format
 * is used in Outlook for non-outlook UIDs send by other ICal clients.
 *
 * @param[in]	strUid			ical UID
 * @param[out]	lpStrBinUid		returned outlook compatible string UID 
 * @return		HRESULT			
 */
HRESULT HrMakeBinUidFromICalUid(const std::string &strUid, std::string *lpStrBinUid)
{
	HRESULT hr = hrSuccess;
	std::string strBinUid;
	int len = 13 + strUid.length();

	strBinUid.insert(0, "\x04\x00\x00\x00\x82\x00\xE0\x00\x74\xC5\xB7\x10\x1A\x82\xE0\x08\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x24);
	strBinUid.append((char*) &len, 4);
	strBinUid.append("vCal-Uid", 8);
	len = 1;				// this is always 1
	strBinUid.append((char*)&len, 4);
	strBinUid.append(strUid);
	strBinUid.append("\x00", 1);

	lpStrBinUid->swap(strBinUid);

	return hr;
}
