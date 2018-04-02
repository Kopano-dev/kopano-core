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
#include <utility>
#include <cstdint>
#include <kopano/platform.h>
#include "icaluid.h"
#include <mapix.h>
#include <kopano/stringutil.h>

namespace KC {

const std::string outlook_guid = "040000008200E00074C5B7101A82E008";

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
	return strUid.compare(0, outlook_guid.length(), outlook_guid) == 0;
}

/**
 * Generates a new UID in outlook format. Format is described in VConverter::HrMakeBinaryUID
 *
 * @param[out]	lpStrData	returned generated UID string
 * @return		MAPI error code
 */
HRESULT HrGenerateUid(std::string *lpStrData)
{
	GUID sGuid;
	ULONG ulSize = 1;

	HRESULT hr = CoCreateGuid(&sGuid);
	if (hr != hrSuccess)
		return hr;
	auto ftNow = UnixTimeToFileTime(time(nullptr));
	auto strBinUid = outlook_guid;
	strBinUid += "00000000";	// InstanceDate
	strBinUid += bin2hex(sizeof(FILETIME), &ftNow);
	strBinUid += "0000000000000000"; // Padding
	strBinUid += bin2hex(sizeof(ULONG), &ulSize); // always 1
	strBinUid += bin2hex(sizeof(GUID), &sGuid); // new guid
	*lpStrData = std::move(strBinUid);
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
	void *origbase = base;
	LPSPropValue lpPropVal = NULL;
	std::string strUid, strBinUid;

	auto hr = MAPIAllocateMore(sizeof(SPropValue), base, reinterpret_cast<void **>(&lpPropVal));
	if (base == nullptr)
		base = lpPropVal;
	if (hr != hrSuccess)
		return hr;

	lpPropVal->ulPropTag = ulNamedTag;

	hr = HrGenerateUid(&strUid);
	if (hr != hrSuccess)
		goto exit;

	strBinUid = hex2bin(strUid);

	lpPropVal->Value.bin.cb = strBinUid.length();
	hr = KAllocCopy(strBinUid.data(), lpPropVal->Value.bin.cb, reinterpret_cast<void **>(&lpPropVal->Value.bin.lpb), base);
	if (hr != hrSuccess)
		goto exit;
	*lppPropVal = lpPropVal;

exit:
	if (hr != hrSuccess && origbase == nullptr)
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
HRESULT HrGetICalUidFromBinUid(const SBinary &sBin, std::string *lpStrUid)
{
	if (sBin.cb > 0x34 && memcmp(sBin.lpb + 0x28, "vCal-Uid", 8) == 0)
		*lpStrUid = reinterpret_cast<const char *>(sBin.lpb) + 0x34;
	else
		*lpStrUid = bin2hex(sBin);
	return hrSuccess;
}

/**
 * Converts ical UID to Outlook compatible UIDs.
 *
 * Add a special Outlook GUID and marker to the Ical UID. This format
 * is used in Outlook for non-outlook UIDs sent by other ICal clients.
 *
 * @param[in]	strUid			ical UID
 * @param[out]	lpStrBinUid		returned outlook compatible string UID 
 * @return		HRESULT			
 */
HRESULT HrMakeBinUidFromICalUid(const std::string &strUid, std::string *lpStrBinUid)
{
	uint32_t len = cpu_to_le32(13 + strUid.length());
	std::string strBinUid("\x04\x00\x00\x00\x82\x00\xE0\x00\x74\xC5\xB7\x10\x1A\x82\xE0\x08\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x24);
	strBinUid.append((char*) &len, 4);
	strBinUid.append("vCal-Uid", 8);
	len = cpu_to_le32(1); /* this is always 1 */
	strBinUid.append((char*)&len, 4);
	strBinUid.append(strUid);
	strBinUid.append("\x00", 1);
	*lpStrBinUid = std::move(strBinUid);
	return hrSuccess;
}

/**
 * Converts string UID to binary property. The converted UID is in a
 * format outlook wants, as described here.
 *
 * UID contents according to [MS-OXCICAL].pdf
 * UID = EncodedGlobalId or ThirdPartyGlobalId
 *
 * EncodedGlobalId    = Header GlobalIdData
 * ThirdPartyGlobalId = 1*UTF8-octets    ; Assuming UTF-8 is the encoding
 *
 * Header = ByteArrayID InstanceDate CreationDateTime Padding DataSize
 *
 * ByteArrayID        = "040000008200E00074C5B7101A82E008"
 * InstanceDate       = InstanceYear InstanceMonth InstanceDay
 * InstanceYear       = 4*4HEXDIGIT      ; UInt16
 * InstanceMonth      = 2*2HEXDIGIT      ; UInt8
 * InstanceDay        = 2*2HEXDIGIT      ; UInt8
 * CreationDateTime   = FileTime
 * FileTime           = 16*16HEXDIGIT    ; UInt6
 * Padding            = 16*16HEXDIGIT    ; "0000000000000000" recommended
 * DataSize           = 8*8HEXDIGIT      ; UInt32 little-endian
 * GlobalIdData       = 2*HEXDIGIT4
 *
 * @param[in]	strUid			String UID
 * @param[in]	base			Base for allocating memory
 * @param[out]	lpPropValue		The binary uid is returned in SPropValue structure
 * @return		Always return hrSuccess
 */
HRESULT HrMakeBinaryUID(const std::string &strUid, void *base, SPropValue *lpPropValue)
{
	SPropValue sPropValue;
	std::string strBinUid, strByteArrayID = "040000008200E00074C5B7101A82E008";

	// Check whether this is a default Outlook UID
	// Exchange example: UID:040000008200E00074C5B7101A82E008 00000000 305D0F2A9A06C901 0000000000000000 10000000 7F64D28AE2DCC64C88F849733F5FBD1D
	// GMail example:    UID:rblkvqecgurvb0all6rjb3d1j8@google.com
	// Sunbird example: UID:1090c3de-36b2-4352-a155-a1436bc806b8
	if (strUid.compare(0, strByteArrayID.length(), strByteArrayID) == 0)
		// EncodedGlobalId
		strBinUid = hex2bin(strUid);
	else
		// ThirdPartyGlobalId
		HrMakeBinUidFromICalUid(strUid, &strBinUid);

	// Caller sets .ulPropTag
	sPropValue.Value.bin.cb = strBinUid.size();
	auto hr = KAllocCopy(strBinUid.data(), sPropValue.Value.bin.cb, reinterpret_cast<void **>(&sPropValue.Value.bin.lpb), base);
	if (hr != hrSuccess)
		return hr;

	// set return value
	lpPropValue->Value.bin.cb  = sPropValue.Value.bin.cb;
	lpPropValue->Value.bin.lpb = sPropValue.Value.bin.lpb;
	return hrSuccess;
}

} /* namespace */
