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

#include "ECFeatures.h"
#include "ECFeatureList.h"
#include <kopano/CommonUtil.h>
#include <kopano/mapi_ptr.h>

using namespace std;

/** 
 * Checks if given feature name is an actual kopano feature.
 * 
 * @param[in] feature name of the feature to check
 * 
 * @return 
 */
bool isFeature(const char* feature)
{
	for (size_t i = 0; i < arraySize(kopano_features); ++i)
		if (stricmp(feature, kopano_features[i]) == 0)
			return true;
	return false;
}

/** 
 * Checks if the given feature name is present in the MV list in the given prop
 * 
 * @param[in] feature check for this feature in the MV_STRING8 list in lpProps
 * @param[in] lpProps should be either PR_EC_(DIS/EN)ABLED_FEATURES_A
 * 
 * @return MAPI Error code
 */
HRESULT hasFeature(const char* feature, LPSPropValue lpProps)
{
	if (!feature || !lpProps || PROP_TYPE(lpProps->ulPropTag) != PT_MV_STRING8)
		return MAPI_E_INVALID_PARAMETER;

	for (ULONG i = 0; i < lpProps->Value.MVszA.cValues; ++i)
		if (stricmp(lpProps->Value.MVszA.lppszA[i], feature) == 0)
			return hrSuccess;
	return MAPI_E_NOT_FOUND;
}

/** 
 * Checks if the given feature name is present in the MV list in the given prop
 * 
 * @param[in] feature check for this feature in the MV_STRING8 list in lpProps
 * @param[in] lpProps should be either PR_EC_(DIS/EN)ABLED_FEATURES_A
 * 
 * @return MAPI Error code
 */
HRESULT hasFeature(const WCHAR* feature, LPSPropValue lpProps)
{
	if (!feature || !lpProps || PROP_TYPE(lpProps->ulPropTag) != PT_MV_UNICODE)
		return MAPI_E_INVALID_PARAMETER;

	for (ULONG i = 0; i < lpProps->Value.MVszW.cValues; ++i)
		if (wcscasecmp(lpProps->Value.MVszW.lppszW[i], feature) == 0)
			return hrSuccess;
	return MAPI_E_NOT_FOUND;
}

/** 
 * Retrieve a property from the owner of the given store
 * 
 * @param[in] lpStore Store of a user to get a property of
 * @param[in] ulPropTag Get this property from the owner of the store
 * @param[out] lpProps Prop value
 * 
 * @return MAPI Error code
 */
static HRESULT HrGetUserProp(IAddrBook *lpAddrBook, IMsgStore *lpStore,
    ULONG ulPropTag, LPSPropValue *lpProps)
{
	HRESULT hr = hrSuccess;
	SPropValuePtr ptrProps;
	MailUserPtr ptrUser;
	ULONG ulObjType;

	if (!lpStore || PROP_TYPE(ulPropTag) != PT_MV_STRING8 || !lpProps) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = HrGetOneProp(lpStore, PR_MAILBOX_OWNER_ENTRYID, &ptrProps);
	if (hr != hrSuccess)
		goto exit;

	hr = lpAddrBook->OpenEntry(ptrProps->Value.bin.cb, (LPENTRYID)ptrProps->Value.bin.lpb, &IID_IMailUser, 0, &ulObjType, &ptrUser);
	if (hr != hrSuccess)
		goto exit;

	hr = HrGetOneProp(ptrUser, ulPropTag, &ptrProps);
	if (hr != hrSuccess)
		goto exit;

	*lpProps = ptrProps.release();

exit:
	return hr;
}

/** 
 * Check if a feature is present in a given enabled/disabled list
 * 
 * @param[in] feature check this feature name
 * @param[in] lpStore Get information from this user store
 * @param[in] ulPropTag either enabled or disabled feature list property tag
 * 
 * @return Only return true if explicitly set in property
 */
static bool checkFeature(const char *feature, IAddrBook *lpAddrBook,
    IMsgStore *lpStore, ULONG ulPropTag)
{
	HRESULT hr = hrSuccess;
	SPropValuePtr ptrProps;

	if (!feature || !lpStore || PROP_TYPE(ulPropTag) != PT_MV_STRING8) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = HrGetUserProp(lpAddrBook, lpStore, ulPropTag, &ptrProps);
	if (hr != hrSuccess)
		goto exit;

	hr = hasFeature(feature, ptrProps);

exit:
	return hr == hrSuccess;
}

bool isFeatureDisabled(const char* feature, IAddrBook *lpAddrBook, IMsgStore *lpStore)
{
	return checkFeature(feature, lpAddrBook, lpStore, PR_EC_DISABLED_FEATURES_A);
}
