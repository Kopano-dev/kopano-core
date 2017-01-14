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
#include <memory>
#include <utility>
#include <kopano/ECRestriction.h>
#include <kopano/memory.hpp>
#include <kopano/tie.hpp>
#include "PublishFreeBusy.h"
#include "CalDavProto.h"
#include <kopano/mapi_ptr.h>
#include <kopano/MAPIErrors.h>

using namespace std;
using namespace KCHL;

/**
 * Maping of caldav properties to Mapi properties
 */
static const struct sMymap {
	unsigned int ulPropTag;
	const char *name;
} sPropMap[] = {
	{ PR_LOCAL_COMMIT_TIME_MAX, "getctag" },
	{ PR_LAST_MODIFICATION_TIME, "getetag" },
	{ PR_DISPLAY_NAME_W, "displayname" },
	{ PR_CONTAINER_CLASS_A, "resourcetype" },
	{ PR_DISPLAY_NAME_W, "owner" },
	{ PR_DISPLAY_NAME_W, "calendar-home-set" },
	{ PR_ENTRYID, "calendar-data" },
	{ PR_COMMENT_W, "calendar-description" },
	{ PR_DISPLAY_TYPE, "calendar-user-type" },
	{ PR_SMTP_ADDRESS_W, "email-address-set" },
	{ PR_SMTP_ADDRESS_W, "calendar-user-address-set" },
	{ PR_DISPLAY_NAME_W, "first-name" },
	{ PR_DISPLAY_TYPE, "record-type" }
};

/** 
 * Create a property tag for an XML property (namespace + name combination)
 * 
 * @param[in] lpObj get the property tag from this object
 * @param[in] lpXmlPropName create the named prop string from this xml id
 * @param[in] converter a convert_context object
 * @param[in] ulFlags flags for GetIDsFromNames call (0 or MAPI_CREATE)
 * 
 * @return the (named) property tag for the xml data, (named are set to PT_BINARY)
 */
static ULONG GetPropIDForXMLProp(LPMAPIPROP lpObj,
    const WEBDAVPROPNAME &sXmlPropName, convert_context &converter,
    ULONG ulFlags = 0)
{
	HRESULT hr = hrSuccess;
	memory_ptr<MAPINAMEID> lpNameID;
	SPropTagArrayPtr ptrPropTags;
	string strName;
	wstring wstrName;

	for (size_t i = 0; i < ARRAY_SIZE(sPropMap); ++i)
		// @todo, we really should use the namespace here too
		if (strcmp(sXmlPropName.strPropname.c_str(), sPropMap[i].name) == 0)
			return sPropMap[i].ulPropTag;

	strName = sXmlPropName.strNS + "#" + sXmlPropName.strPropname;
	wstrName = converter.convert_to<wstring>(strName, rawsize(strName), "UTF-8");

	hr = MAPIAllocateBuffer(sizeof(MAPINAMEID), &~lpNameID);
	if (hr != hrSuccess)
		return PR_NULL;

	lpNameID->lpguid = (GUID*)&PSETID_Kopano_CalDav;
	lpNameID->ulKind = MNID_STRING;
	lpNameID->Kind.lpwstrName = (WCHAR*)wstrName.c_str();
	hr = lpObj->GetIDsFromNames(1, &+lpNameID, ulFlags, &~ptrPropTags);
	if (hr != hrSuccess)
		return PR_NULL;
	return PROP_TAG(PT_BINARY, PROP_ID(ptrPropTags->aulPropTag[0]));
}

/**
 * @param[in]	lpRequest	Pointer to Http class object
 * @param[in]	lpSession	Pointer to Mapi session object
 * @param[in]	strSrvTz	String specifying the server timezone, set in ical.cfg
 * @param[in]	strCharset	String specifying the default charset of the http response
 */
CalDAV::CalDAV(Http *lpRequest, IMAPISession *lpSession,
    const std::string &strSrvTz, const std::string &strCharset) :
	WebDav(lpRequest, lpSession, strSrvTz, strCharset)
{
}

/**
 * Process all the caldav requests
 * @param[in]	strMethod	Name of the http request(e.g PROPFIND, REPORT..)
 * @return		MAPI error code
 */
HRESULT CalDAV::HrHandleCommand(const std::string &strMethod)
{
	HRESULT hr = hrSuccess;

	if (!strMethod.compare("PROPFIND"))
		hr = HrPropfind();
	else if (!strMethod.compare("REPORT"))
		hr = HrReport();
	else if (!strMethod.compare("PUT"))
		hr = HrPut();
	else if (!strMethod.compare("DELETE"))
		hr = HrHandleDelete();
	else if (!strMethod.compare("MKCALENDAR"))
		hr = HrMkCalendar();
	else if (!strMethod.compare("PROPPATCH"))
		hr = HrPropPatch();
	else if (!strMethod.compare("POST"))
		hr = HrHandlePost();
	else if (!strMethod.compare("MOVE"))
		hr = HrMove();
	else
		m_lpRequest->HrResponseHeader(501, "Not Implemented");

	if (hr != hrSuccess)
		m_lpRequest->HrResponseHeader(400, "Bad Request");

	return hr;
}

/**
 * Handles the PROPFIND request, identifies the type of PROPFIND request
 *
 * @param[in]	lpsDavProp			Pointer to structure cotaining info about the PROPFIND request
 * @param[out]	lpsDavMulStatus		Response generated for the PROPFIND request
 * @return		HRESULT
 */
HRESULT CalDAV::HrHandlePropfind(WEBDAVREQSTPROPS *lpsDavProp, WEBDAVMULTISTATUS *lpsDavMulStatus)
{
	HRESULT hr;
	ULONG ulDepth = 0;

	/* default depths:
	 * caldav report: 0
	 * webdav propfind: infinity
	 */
	
	m_lpRequest->HrGetDepth(&ulDepth);

	// always load top level container properties
	hr = HrHandlePropfindRoot(lpsDavProp, lpsDavMulStatus);
	if (hr != hrSuccess)
		return hr;

	// m_wstrFldName not set means url is: /caldav/user/ so list calendars
	if (ulDepth == 1 && m_wstrFldName.empty())
		// Retrieve list of calendars
		return HrListCalendar(lpsDavProp, lpsDavMulStatus);
	else if (ulDepth >= 1)
		// Retrieve the Calendar entries list
		return HrListCalEntries(lpsDavProp, lpsDavMulStatus);

	return hrSuccess;
}

/**
 * Handles the Depth 0 PROPFIND request
 *
 * The client requets for information about the user,store and folder by using this request
 *
 * @param[in]	sDavReqstProps		Pointer to structure cotaining info about properties requested by client
 * @param[in]	lpsDavMulStatus		Pointer to structure cotaining response to the request
 * @return		HRESULT
 */
// @todo simplify this .. depth 0 is always on root container props.
HRESULT CalDAV::HrHandlePropfindRoot(WEBDAVREQSTPROPS *sDavReqstProps, WEBDAVMULTISTATUS *lpsDavMulStatus)
{
	HRESULT hr = hrSuccess;
	WEBDAVPROP *lpsDavProp = NULL;
	WEBDAVRESPONSE sDavResp;
	IMAPIProp *lpMapiProp = NULL;
	memory_ptr<SPropTagArray> lpPropTagArr;
	memory_ptr<SPropValue> lpSpropVal;
	ULONG cbsize = 0;
	int i = 0;

	lpsDavProp = &(sDavReqstProps->sProp);

	// number of properties requested by client.
	cbsize = lpsDavProp->lstProps.size();

	// @todo, we only select the store so we don't have a PR_CONTAINER_CLASS property when querying calendar list.
	if(m_wstrFldName.empty())
		lpMapiProp = m_lpActiveStore;
	else
		lpMapiProp = m_lpUsrFld;

	hr = MAPIAllocateBuffer(CbNewSPropTagArray(cbsize), &~lpPropTagArr);
	if (hr != hrSuccess)
	{
		ec_log_err("Cannot allocate memory");
		return hr;
	}

	lpPropTagArr->cValues = cbsize;
	
	// Get corresponding mapi properties.
	for (const auto &iter : lpsDavProp->lstProps)
		lpPropTagArr->aulPropTag[i++] = GetPropIDForXMLProp(lpMapiProp, iter.sPropName, m_converter);

	hr = lpMapiProp->GetProps(lpPropTagArr, 0, &cbsize, &~lpSpropVal);
	if (FAILED(hr)) {
		ec_log_err("Error in GetProps for user %ls, error code: 0x%08X %s", m_wstrUser.c_str(), hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	hr = hrSuccess;

	HrSetDavPropName(&(sDavResp.sPropName), "response", WEBDAVNS);

	HrSetDavPropName(&(sDavResp.sHRef.sPropName), "href", WEBDAVNS);
	// fetches escaped url
	m_lpRequest->HrGetRequestUrl(&sDavResp.sHRef.strValue);

	// map values and properties in WEBDAVRESPONSE structure.
	hr = HrMapValtoStruct(lpMapiProp, lpSpropVal, cbsize, NULL, 0, false, &(lpsDavProp->lstProps), &sDavResp);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandlePropfindRoot HrMapValtoStruct failed 0x%08x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	HrSetDavPropName(&(lpsDavMulStatus->sPropName), "multistatus", WEBDAVNS);
	lpsDavMulStatus->lstResp.push_back(std::move(sDavResp));
	return hrSuccess;
}

/** 
 * Retrieve list of entries in the calendar folder
 *
 * The function handles REPORT(calendar-query) and PROPFIND(depth 1) request,
 * REPORT method is used by mozilla clients and mac ical.app uses PROPFIND request
 *
 * @param[in]	lpsWebRCalQry	Pointer to structure containing the list of properties requested by client
 * @param[out]	lpsWebMStatus	Pointer to structure containing the response
 * @return		HRESULT
 */
HRESULT CalDAV::HrListCalEntries(WEBDAVREQSTPROPS *lpsWebRCalQry, WEBDAVMULTISTATUS *lpsWebMStatus)
{
	HRESULT hr = hrSuccess;
	std::string strConvVal;
	std::string strReqUrl;
	object_ptr<IMAPITable> lpTable;
	LPSRowSet lpRowSet = NULL;
	memory_ptr<SPropTagArray> lpPropTagArr;
	memory_ptr<SPropValue> lpsPropVal;
	std::unique_ptr<MapiToICal> lpMtIcal;
	ULONG cbsize = 0;
	ULONG ulTagGOID = 0;
	ULONG ulTagTsRef = 0;
	ULONG ulTagPrivate = 0;
	WEBDAVPROP sDavProp;
	WEBDAVRESPONSE sWebResponse;
	bool blCensorPrivate = false;
	ULONG ulCensorFlag = 0;
	ULONG cValues = 0;
	memory_ptr<SPropValue> lpProps;
	SPropValue sResData;
	ULONG ulItemCount = 0;
	ECOrRestriction rst;
	int i;

	ulTagGOID = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY);
	ulTagTsRef = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_APPTTSREF], PT_UNICODE);
	ulTagPrivate = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PRIVATE], PT_BOOLEAN);

	m_lpRequest->HrGetRequestUrl(&strReqUrl);
	if (strReqUrl.empty() || *--strReqUrl.end() != '/')
		strReqUrl.append(1, '/');

	if ((m_ulFolderFlag & SHARED_FOLDER) && !HasDelegatePerm(m_lpDefStore, m_lpActiveStore))
		blCensorPrivate = true;
	
	HrSetDavPropName(&(sWebResponse.sPropName), "response", WEBDAVNS);
	HrSetDavPropName(&(sWebResponse.sHRef.sPropName), "href", WEBDAVNS);
		
	sDavProp = lpsWebRCalQry->sProp;

	
	if (!lpsWebRCalQry->sFilter.lstFilters.empty())
	{
		hr = HrGetOneProp(m_lpUsrFld, PR_CONTAINER_CLASS_A, &~lpsPropVal);
		if (hr != hrSuccess) {
			ec_log_debug("CalDAV::HrListCalEntries HrGetOneProp failed 0x%08x %s", hr, GetMAPIErrorMessage(hr));
			goto exit;
		}

		if (lpsWebRCalQry->sFilter.lstFilters.back() == "VTODO"
			&& strncmp(lpsPropVal->Value.lpszA, "IPF.Task", strlen("IPF.Task")))
				goto exit;
		if (lpsWebRCalQry->sFilter.lstFilters.back() == "VEVENT"
			&& strncmp(lpsPropVal->Value.lpszA, "IPF.Appointment", strlen("IPF.Appointment")))
			goto exit;
	}

	hr = m_lpUsrFld->GetContentsTable(0, &~lpTable);
	if (hr != hrSuccess) {
		ec_log_err("Error in GetContentsTable, error code: 0x%08X %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	// restrict on meeting requests and appointments
	sResData.ulPropTag = PR_MESSAGE_CLASS_A;
	sResData.Value.lpszA = const_cast<char *>("IPM.Appointment");
	rst += ECContentRestriction(FL_IGNORECASE | FL_PREFIX, PR_MESSAGE_CLASS_A, &sResData, ECRestriction::Shallow);
	sResData.Value.lpszA = const_cast<char *>("IPM.Meeting");
	rst += ECContentRestriction(FL_IGNORECASE | FL_PREFIX, PR_MESSAGE_CLASS_A, &sResData, ECRestriction::Shallow);
	sResData.Value.lpszA = const_cast<char *>("IPM.Task");
	rst += ECContentRestriction(FL_IGNORECASE | FL_PREFIX, PR_MESSAGE_CLASS_A, &sResData, ECRestriction::Shallow);
	hr = rst.RestrictTable(lpTable, 0);
	if (hr != hrSuccess) {
		ec_log_err("Unable to restrict folder contents, error code: 0x%08X %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	// +4 to add GlobalObjid, dispidApptTsRef , PR_ENTRYID and private in SetColumns along with requested data.
	cbsize = (ULONG)sDavProp.lstProps.size() + 4;
	hr = MAPIAllocateBuffer(CbNewSPropTagArray(cbsize), &~lpPropTagArr);
	if(hr != hrSuccess)
	{
		ec_log_err("Cannot allocate memory");
		goto exit;
	}

	lpPropTagArr->cValues = cbsize;
	lpPropTagArr->aulPropTag[0] = ulTagTsRef;
	lpPropTagArr->aulPropTag[1] = ulTagGOID;
	lpPropTagArr->aulPropTag[2] = PR_ENTRYID;
	lpPropTagArr->aulPropTag[3] = ulTagPrivate;
	//mapi property mapping for requested properties.
	//FIXME what if the property mapping is not found.
	i = 4;
	for (const auto &sDavProperty : sDavProp.lstProps)
		lpPropTagArr->aulPropTag[i++] = GetPropIDForXMLProp(m_lpUsrFld, sDavProperty.sPropName, m_converter);

	hr = m_lpUsrFld->GetProps(lpPropTagArr, 0, &cValues, &~lpProps);
	if (FAILED(hr)) {
		ec_log_err("Unable to receive folder properties, error 0x%08X %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	// @todo, add "start time" property and recurrence data to table and filter in loop
	// if lpsWebRCalQry->sFilter.tStart is set.
	hr = lpTable->SetColumns(lpPropTagArr, 0);
	if(hr != hrSuccess)
		goto exit;

	// @todo do we really need this converter, since we're only listing the items?
	CreateMapiToICal(m_lpAddrBook, "utf-8", &unique_tie(lpMtIcal));
	if (!lpMtIcal)
	{
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	while(1)
	{
		hr = lpTable->QueryRows(50, 0, &lpRowSet);
		if(hr != hrSuccess)
		{
			ec_log_err("Error retrieving rows of table");
			goto exit;
		}

		if(lpRowSet->cRows == 0)
			break;

		//add data from each requested property.
		for (ULONG ulRowCntr = 0; ulRowCntr < lpRowSet->cRows; ++ulRowCntr)
		{
			// test PUT url part
			if (lpRowSet->aRow[ulRowCntr].lpProps[0].ulPropTag == ulTagTsRef)
				strConvVal = W2U((const WCHAR*)lpRowSet->aRow[ulRowCntr].lpProps[0].Value.lpszW);
			// test ical UID value
			else if (lpRowSet->aRow[ulRowCntr].lpProps[1].ulPropTag == ulTagGOID)
				strConvVal = SPropValToString(&(lpRowSet->aRow[ulRowCntr].lpProps[1]));
			else
				strConvVal.clear();

			// On some items, webaccess never created the uid, so we need to create one for ical
			if (strConvVal.empty())
			{
				// this really shouldn't happen, every item should have a guid.

				hr = CreateAndGetGuid(lpRowSet->aRow[ulRowCntr].lpProps[2].Value.bin, ulTagGOID, &strConvVal);
				if(hr == E_ACCESSDENIED)
				{
					// @todo shouldn't we use PR_ENTRYID in the first place? Saving items in a read-only command is a serious no-no.
					// use PR_ENTRYID since we couldn't create a new guid for the item
					strConvVal = bin2hex(lpRowSet->aRow[ulRowCntr].lpProps[2].Value.bin.cb, lpRowSet->aRow[ulRowCntr].lpProps[2].Value.bin.lpb);
					hr = hrSuccess;
				}
				else if (hr != hrSuccess) {
					ec_log_debug("CreateAndGetGuid failed: 0x%08x %s", hr, GetMAPIErrorMessage(hr));
					continue;
				}
			} else {
				strConvVal = urlEncode(strConvVal);
			}

			sWebResponse.sHRef.strValue = strReqUrl + strConvVal + ".ics";

			if (blCensorPrivate && lpRowSet->aRow[ulRowCntr].lpProps[3].ulPropTag == ulTagPrivate && lpRowSet->aRow[ulRowCntr].lpProps[3].Value.b)
				ulCensorFlag |= M2IC_CENSOR_PRIVATE;
			else
				ulCensorFlag = 0;

			hr = HrMapValtoStruct(m_lpUsrFld, lpRowSet->aRow[ulRowCntr].lpProps, lpRowSet->aRow[ulRowCntr].cValues, lpMtIcal.get(), ulCensorFlag, true, &(lpsWebRCalQry->sProp.lstProps), &sWebResponse);
			++ulItemCount;
			lpsWebMStatus->lstResp.push_back(sWebResponse);
			sWebResponse.lstsPropStat.clear();
		}
		FreeProws(lpRowSet);
		lpRowSet = NULL;
	}

exit:
	if (hr == hrSuccess)
		ec_log_info("Number of items in folder returned: %u", ulItemCount);
	if(lpRowSet)
		FreeProws(lpRowSet);
	return hr;
}

/**
 * Handles Report (calendar-multiget) caldav request.
 *
 * Sets values of requested caldav properties in WEBDAVMULTISTATUS structure.
 *
 * @param[in]	sWebRMGet		structure that contains the list of calendar entries and properties requested.
 * @param[out]	sWebMStatus		structure that values of requested properties.
 * @retval		HRESULT
 * @return		S_OK			always returns S_OK.
 *
 */
HRESULT CalDAV::HrHandleReport(WEBDAVRPTMGET *sWebRMGet, WEBDAVMULTISTATUS *sWebMStatus)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMAPITable> lpTable;
	memory_ptr<SPropTagArray> lpPropTagArr;
	std::unique_ptr<MapiToICal> lpMtIcal;
	std::string strReqUrl;
	memory_ptr<SRestriction> lpsRoot;
	ULONG cbsize = 0;
	WEBDAVPROP sDavProp;
	WEBDAVRESPONSE sWebResponse;
	bool blCensorPrivate = false;
	int i;

	m_lpRequest->HrGetRequestUrl(&strReqUrl);
	if (strReqUrl.empty() || *--strReqUrl.end() != '/')
		strReqUrl.append(1, '/');

	HrSetDavPropName(&(sWebResponse.sPropName), "response", WEBDAVNS);
	HrSetDavPropName(&sWebMStatus->sPropName, "multistatus", WEBDAVNS);

	if ((m_ulFolderFlag & SHARED_FOLDER) && !HasDelegatePerm(m_lpDefStore, m_lpActiveStore))
		blCensorPrivate = true;
	hr = m_lpUsrFld->GetContentsTable(0, &~lpTable);
	if(hr != hrSuccess) {
		ec_log_err("Error in GetContentsTable, error code: 0x%08X %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	sDavProp = sWebRMGet->sProp;

	//Add GUID in Setcolumns.
	cbsize = (ULONG)sDavProp.lstProps.size() + 2;
	hr = MAPIAllocateBuffer(CbNewSPropTagArray(cbsize), &~lpPropTagArr);
	if (hr != hrSuccess) {
		ec_log_err("Error allocating memory, error code: 0x%08X %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	
	lpPropTagArr->cValues = cbsize;
	lpPropTagArr->aulPropTag[0] = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY);
	lpPropTagArr->aulPropTag[1] = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PRIVATE], PT_BOOLEAN);
	i = 2;
	for (const auto &sDavProperty : sDavProp.lstProps)
		lpPropTagArr->aulPropTag[i++] = GetPropIDForXMLProp(m_lpUsrFld, sDavProperty.sPropName, m_converter);

	hr = lpTable->SetColumns(lpPropTagArr, 0);
	if(hr != hrSuccess)
		return hr;

	cbsize = (ULONG)sWebRMGet->lstWebVal.size();
	ec_log_info("Requesting conversion of %u items", cbsize);
	CreateMapiToICal(m_lpAddrBook, "utf-8", &unique_tie(lpMtIcal));
	if (!lpMtIcal)
		return MAPI_E_CALL_FAILED;

	for (ULONG i = 0; i < cbsize; ++i) {
		WEBDAVVALUE sWebDavVal;
		SRowSet *lpValRows = NULL;
		ULONG ulCensorFlag = (ULONG)blCensorPrivate;
		
		sWebDavVal = sWebRMGet->lstWebVal.front();
		sWebRMGet->lstWebVal.pop_front();

		sWebResponse.sHRef = sWebDavVal;
		sWebResponse.sHRef.strValue = strReqUrl + urlEncode(sWebDavVal.strValue) + ".ics";
		sWebResponse.sStatus = WEBDAVVALUE();

		hr = HrMakeRestriction(sWebDavVal.strValue, m_lpNamedProps, &~lpsRoot);
		if (hr != hrSuccess) {
			ec_log_debug("CalDAV::HrHandleReport HrMakeRestriction failed 0x%08x %s", hr, GetMAPIErrorMessage(hr));
			goto next;
		}
		
		hr = lpTable->FindRow(lpsRoot, BOOKMARK_BEGINNING, 0);
		if (hr != hrSuccess)
			ec_log_debug("Entry not found (%s), error code: 0x%08X %s", sWebDavVal.strValue.c_str(), hr, GetMAPIErrorMessage(hr));

		// conversion if everthing goes ok, otherwise, add empty item with failed status field
		// we need to return all items requested in the multistatus reply, otherwise sunbird will stop, displaying nothing to the user.

		if (hr == hrSuccess) {
			hr = lpTable->QueryRows(1, TBL_NOADVANCE, &lpValRows); // TODO: what if we get multiple items ?
		
			if(hr != hrSuccess || lpValRows->cRows != 1)
				return hr;

			if (blCensorPrivate && PROP_TYPE(lpValRows->aRow[0].lpProps[1].ulPropTag) != PT_ERROR && lpValRows->aRow[0].lpProps[1].Value.b)
				ulCensorFlag |= M2IC_CENSOR_PRIVATE;
			else
				ulCensorFlag = 0;
		}

		if(hr == hrSuccess) {
			hr = HrMapValtoStruct(m_lpUsrFld, lpValRows->aRow[0].lpProps, lpValRows->aRow[0].cValues, lpMtIcal.get(), ulCensorFlag, true, &sDavProp.lstProps, &sWebResponse);
			if (hr != hrSuccess)
				return hr;
		} else {
			// no: "status" can only be in <D:propstat xmlns:D="DAV:"> tag, so fix in HrMapValtoStruct
			HrSetDavPropName(&(sWebResponse.sStatus.sPropName), "status", WEBDAVNS);
			sWebResponse.sStatus.strValue = "HTTP/1.1 404 Not Found";
		}

		sWebMStatus->lstResp.push_back(sWebResponse);
		sWebResponse.lstsPropStat.clear();
next:
		if(lpValRows)
			FreeProws(lpValRows);
		lpValRows = NULL;

	}
	return hrSuccess;
}

/**
 * Generates response to Property search set request
 *
 * The request is to list the properties that can be requested by client,
 * while requesting for attendee suggestions
 *
 * @param[out]	lpsWebMStatus	Response structure returned
 *
 * @return		HRESULT			Always returns hrSuccess
 */
HRESULT CalDAV::HrHandlePropertySearchSet(WEBDAVMULTISTATUS *lpsWebMStatus)
{
	HRESULT hr = hrSuccess;
	WEBDAVRESPONSE sDavResponse;
	WEBDAVPROPSTAT sDavPropStat;
	WEBDAVPROPERTY sWebProperty;
	WEBDAVPROP sWebProp;

	HrSetDavPropName(&(lpsWebMStatus->sPropName), "principal-search-property-set", WEBDAVNS);

	HrSetDavPropName(&sDavResponse.sPropName, "principal-search-property", WEBDAVNS);
	HrSetDavPropName(&sDavPropStat.sPropName, "prop", WEBDAVNS);
	HrSetDavPropName(&sDavPropStat.sProp.sPropName, "displayname", WEBDAVNS);
	sDavResponse.lstsPropStat.push_back(sDavPropStat);
	HrSetDavPropName(&sDavResponse.sHRef.sPropName, "description", "xml:lang", "en", WEBDAVNS);	
	sDavResponse.sHRef.strValue = "Display Name";	
	HrSetDavPropName(&sDavPropStat.sProp.sPropName, "", "");

	lpsWebMStatus->lstResp.push_back(sDavResponse);	
	sDavResponse.lstsPropStat.clear();

	HrSetDavPropName(&sDavResponse.sPropName, "principal-search-property", WEBDAVNS);
	HrSetDavPropName(&sDavPropStat.sPropName, "prop", WEBDAVNS);
	HrSetDavPropName(&sDavPropStat.sProp.sPropName, "calendar-user-type", WEBDAVNS);
	sDavResponse.lstsPropStat.push_back(sDavPropStat);
	HrSetDavPropName(&sDavResponse.sHRef.sPropName, "description", "xml:lang", "en", WEBDAVNS);	
	sDavResponse.sHRef.strValue = "Calendar user type";	
	HrSetDavPropName(&sDavPropStat.sProp.sPropName, "", "");

	lpsWebMStatus->lstResp.push_back(sDavResponse);	
	sDavResponse.lstsPropStat.clear();

	HrSetDavPropName(&sDavResponse.sPropName, "principal-search-property", WEBDAVNS);
	HrSetDavPropName(&sDavPropStat.sPropName, "prop", WEBDAVNS);
	HrSetDavPropName(&sDavPropStat.sProp.sPropName, "calendar-user-address-set", WEBDAVNS);
	sDavResponse.lstsPropStat.push_back(sDavPropStat);
	HrSetDavPropName(&sDavResponse.sHRef.sPropName, "description", "xml:lang", "en", WEBDAVNS);	
	sDavResponse.sHRef.strValue = "Calendar User Address Set";	
	HrSetDavPropName(&sDavPropStat.sProp.sPropName, "", "");

	lpsWebMStatus->lstResp.push_back(sDavResponse);	
	sDavResponse.lstsPropStat.clear();

	HrSetDavPropName(&sDavResponse.sPropName, "principal-search-property", WEBDAVNS);
	HrSetDavPropName(&sDavPropStat.sPropName, "prop", WEBDAVNS);
	HrSetDavPropName(&sDavPropStat.sProp.sPropName, "email-address-set", "http://calendarserver.org/ns/");
	sDavResponse.lstsPropStat.push_back(sDavPropStat);
	HrSetDavPropName(&sDavResponse.sHRef.sPropName, "description", "xml:lang", "en", WEBDAVNS);	
	sDavResponse.sHRef.strValue = "Email Address";
	HrSetDavPropName(&sDavPropStat.sProp.sPropName, "", "");
	

	lpsWebMStatus->lstResp.push_back(sDavResponse);
	sDavResponse.lstsPropStat.clear();
	return hr;
}

/**
 * Handles attendee suggestion list request
 * 
 * @param[in]	sWebRMGet		Pointer to WEBDAVRPTMGET structure containing user to search in global address book
 * @param[out]	sWebMStatus		Pointer to WEBDAVMULTISTATUS structure cotaning attndees list matching the request
 *
 * @return		HRESULT			Always returns hrSuccess 
 */
HRESULT CalDAV::HrHandlePropertySearch(WEBDAVRPTMGET *sWebRMGet, WEBDAVMULTISTATUS *sWebMStatus)
{
	HRESULT hr = hrSuccess;
	object_ptr<IABContainer> lpAbCont;
	object_ptr<IMAPITable> lpTable;
	SRowSet *lpValRows = NULL;
	memory_ptr<SPropTagArray> lpPropTagArr;
	ULONG cbsize = 0;
	ULONG ulPropTag = 0;
	ULONG ulTagPrivate = 0;
	std::list<WEBDAVVALUE>::const_iterator iterWebVal;
	SBinary sbEid = {0, NULL};
	WEBDAVPROP sDavProp;
	WEBDAVRESPONSE sWebResponse;
	ULONG ulObjType = 0;
	std::string strReq;	
	ECOrRestriction rst;
	int i;

	m_lpRequest->HrGetRequestUrl(&strReq);

	// Open Global Address book
	hr = m_lpAddrBook->GetDefaultDir(&sbEid.cb, (LPENTRYID*)&sbEid.lpb);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandlePropertySearch GetDefaultDir failed: 0x%08x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}
	hr = m_lpSession->OpenEntry(sbEid.cb, reinterpret_cast<ENTRYID *>(sbEid.lpb), nullptr, 0, &ulObjType, &~lpAbCont);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandlePropertySearch OpenEntry failed: 0x%08x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}
	hr = lpAbCont->GetContentsTable(0, &~lpTable);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandlePropertySearch GetContentsTable failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	hr = lpTable->GetRowCount(0, &ulObjType);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandlePropertySearch GetRowCount failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	// create restriction
	iterWebVal = sWebRMGet->lstWebVal.cbegin();

	for (size_t i = 0; i < sWebRMGet->lstWebVal.size(); ++i, ++iterWebVal) {
		wstring content = U2W(iterWebVal->strValue);
		SPropValue pv;
		pv.ulPropTag = GetPropIDForXMLProp(lpAbCont, iterWebVal->sPropName, m_converter);
		pv.Value.lpszW = const_cast<wchar_t *>(content.c_str());
		rst += ECContentRestriction(FL_SUBSTRING | FL_IGNORECASE, pv.ulPropTag, &pv, ECRestriction::Full);
	}

	// create proptagarray.
	sDavProp = sWebRMGet->sProp;

	//Add GUID in Setcolumns.
	cbsize = (ULONG)sDavProp.lstProps.size() + 3;
	hr = MAPIAllocateBuffer(CbNewSPropTagArray(cbsize), &~lpPropTagArr);
	if (hr != hrSuccess) {
		ec_log_err("Error allocating memory, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}
	
	ulTagPrivate = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PRIVATE], PT_BOOLEAN);
	ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY);
	
	lpPropTagArr->cValues = cbsize;
	lpPropTagArr->aulPropTag[0] = ulPropTag;
	lpPropTagArr->aulPropTag[1] = ulTagPrivate;
	lpPropTagArr->aulPropTag[2] = PR_ACCOUNT;
	i = 3;
	for (const auto &sDavProperty : sDavProp.lstProps)
		lpPropTagArr->aulPropTag[i++] = GetPropIDForXMLProp(lpAbCont, sDavProperty.sPropName, m_converter);

	hr = lpTable->SetColumns(lpPropTagArr, 0);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandlePropertySearch SetColumns failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}
	
	// restrict table
	hr = rst.RestrictTable(lpTable, 0);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandlePropertySearch restrict failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	hr = lpTable->GetRowCount(0, &ulObjType);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandlePropertySearch getrowcount failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	HrSetDavPropName(&(sWebResponse.sPropName), "response", WEBDAVNS);
	HrSetDavPropName(&sWebMStatus->sPropName, "multistatus", WEBDAVNS);
	
	// set rows into Dav structures
	while (1) {
		hr = lpTable->QueryRows(50, 0, &lpValRows); // TODO: what if we get multiple items ?
		if (hr != hrSuccess || lpValRows->cRows == 0)
			break;

		for (ULONG i = 0; i < lpValRows->cRows; ++i) {
			WEBDAVVALUE sWebDavVal;
			auto lpsPropVal = PCpropFindProp(lpValRows->aRow[i].lpProps, lpValRows->aRow[i].cValues, PR_ACCOUNT_W);
			if (!lpsPropVal)
				continue;		// user without account name is useless

			HrSetDavPropName(&(sWebResponse.sHRef.sPropName), "href", WEBDAVNS);
			sWebResponse.sHRef.strValue = strReq + urlEncode(lpsPropVal->Value.lpszW, "utf-8") + "/";
			
			hr = HrMapValtoStruct(lpAbCont, lpValRows->aRow[i].lpProps, lpValRows->aRow[i].cValues, NULL, 0, true, &sDavProp.lstProps, &sWebResponse);
			if (hr != hrSuccess) {
				ec_log_err("Unable to convert user properties to entry for user %ls", lpsPropVal->Value.lpszW);
				continue;
			}

			sWebMStatus->lstResp.push_back(sWebResponse);
			sWebResponse.lstsPropStat.clear();

		}
		FreeProws(lpValRows);
		lpValRows = NULL;
	}

	hr = hrSuccess;

exit:
	if (lpValRows)
		FreeProws(lpValRows);
	MAPIFreeBuffer(sbEid.lpb);
	return hr;
}

/**
 * Function moves a folder or message to the deleted items folder
 * @note does not check if-match: if you had a message modified which you now want to delete, we delete anyway
 * 
 * @return	mapi error code
 * @retval	MAPI_E_NO_ACCESS	Insufficient permissions on folder or request to delete default folder.
 * @retval	MAPI_E_NOT_FOUND	Message or folder to be deleted not found.
 */
HRESULT CalDAV::HrHandleDelete()
{
	HRESULT hr = hrSuccess;
	int nFldId = 1;
	std::string strGuid;
	std::string strUrl;
	std::wstring wstrFldName;
	std::wstring wstrFldTmpName;
	SBinary sbEid = {0,0};
	ULONG ulObjType = 0;
	ULONG cValues = 0;
	object_ptr<IMAPIFolder> lpWastBoxFld;
	memory_ptr<SPropValue> lpProps, lpPropWstBxEID;
	memory_ptr<ENTRYLIST> lpEntryList;
	bool bisFolder = false;
	static constexpr const SizedSPropTagArray(3, lpPropTagArr) =
		{3, {PR_ENTRYID, PR_LAST_MODIFICATION_TIME, PR_DISPLAY_NAME_W}};

	m_lpRequest->HrGetUrl(&strUrl);
	bisFolder = m_ulUrlFlag & REQ_COLLECTION;

	// deny delete of default folder
	if (!m_blFolderAccess && bisFolder)
	{
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}
	hr = HrGetOneProp(m_lpDefStore, PR_IPM_WASTEBASKET_ENTRYID, &~lpPropWstBxEID);
	if(hr != hrSuccess) {
		ec_log_err("Error finding \"Deleted items\" folder, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}
	hr = m_lpDefStore->OpenEntry(lpPropWstBxEID->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpPropWstBxEID->Value.bin.lpb), nullptr, MAPI_MODIFY, &ulObjType, &~lpWastBoxFld);
	if (hr != hrSuccess)
	{
		ec_log_err("Error opening \"Deleted items\" folder, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}
	
	// if the request is to delete calendar entry
	if (!bisFolder) {
		strGuid = StripGuid(strUrl);
		hr = HrMoveEntry(strGuid, lpWastBoxFld);
		goto exit;
	}
	hr = m_lpUsrFld->GetProps(lpPropTagArr, 0, &cValues, &~lpProps);
	if (FAILED(hr)) {
		ec_log_debug("CalDAV::HrHandleDelete getprops failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	// Entry ID
	if (lpProps[0].ulPropTag != PT_ERROR)
		sbEid = lpProps[0].Value.bin;

	// Folder display name
	if (lpProps[2].ulPropTag != PT_ERROR)
		wstrFldName = lpProps[2].Value.lpszW;
	
	//Create Entrylist
	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~lpEntryList);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleDelete mapiallocatebuffer failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	lpEntryList->cValues = 1;

	hr = MAPIAllocateMore(sizeof(SBinary), lpEntryList, (void**)&lpEntryList->lpbin);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleDelete mapiallocatemore failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}
	
	hr = MAPIAllocateMore(sbEid.cb, lpEntryList, (void**)&lpEntryList->lpbin[0].lpb);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleDelete mapiallocatemore(2) failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	lpEntryList->lpbin[0].cb = sbEid.cb;
	memcpy(lpEntryList->lpbin[0].lpb, sbEid.lpb, sbEid.cb);

	wstrFldTmpName = wstrFldName;
	while (true) {

		hr = m_lpIPMSubtree->CopyFolder(sbEid.cb, (LPENTRYID)sbEid.lpb, NULL, lpWastBoxFld, (LPTSTR)wstrFldTmpName.c_str(), 0, NULL, MAPI_MOVE | MAPI_UNICODE);
		if (hr == MAPI_E_COLLISION) {
			// rename the folder if same folder name is present in Deleted items folder
			if (nFldId >= 1000) { // Max 999 folders
				ec_log_err("Error Deleting Folder error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
				goto exit;
			}
			wstrFldTmpName = wstrFldName + std::to_wstring(nFldId);
			++nFldId;
		} else if (hr != hrSuccess ) {
			ec_log_err("Error Deleting Folder error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
			goto exit;
		} else
			break;
	}

exit:
	if (hr == MAPI_E_NO_ACCESS)
	{
		m_lpRequest->HrResponseHeader(403, "Forbidden");
		m_lpRequest->HrResponseBody("This item cannot be deleted");
	}
	else if (hr != hrSuccess)
	{
		m_lpRequest->HrResponseHeader(404, "Not Found");
		m_lpRequest->HrResponseBody("Item to be Deleted not found");
	}
	else
		m_lpRequest->HrResponseHeader(204, "No Content");
	return hr;
}

/**
 * Moves calendar entry to destination folder
 *
 * Function searches for the calendar refrenced by the guid value in the
 * folder opened by HrGetFolder() and moves the entry to the destination folder.
 *
 * @param[in] strGuid		The Guid refrencing a calendar entry
 * @param[in] lpDestFolder	The destination folder to which the entry is moved.
 *
 * @return	mapi error codes
 *
 * @retval	MAPI_E_NOT_FOUND	No message found containing the guid value
 * @retval	MAPI_E_NO_ACCESS	Insufficient rights on the calendar entry
 *
 * @todo	Check folder type and message type are same(i.e tasks are copied to task folder only)
 */
HRESULT CalDAV::HrMoveEntry(const std::string &strGuid, LPMAPIFOLDER lpDestFolder)
{
	HRESULT hr = hrSuccess;
	SBinary sbEid = {0,0};
	memory_ptr<SPropValue> lpProps;
	object_ptr<IMessage> lpMessage;
	memory_ptr<ENTRYLIST> lpEntryList;
	bool bMatch = false;

	//Find Entry With Particular Guid
	hr = HrFindAndGetMessage(strGuid, m_lpUsrFld, m_lpNamedProps, &~lpMessage);
	if (hr != hrSuccess)
	{
		ec_log_err("Entry to be deleted not found: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	bMatch = ! m_lpRequest->CheckIfMatch(lpMessage);
	if (bMatch)
		return MAPI_E_DECLINE_COPY;

	// Check if the user is accessing a shared folder
	// Check for delegate permissions on shared folder
	// Check if the entry to be deleted in private
	if ((m_ulFolderFlag & SHARED_FOLDER) && 
		!HasDelegatePerm(m_lpDefStore, m_lpActiveStore) &&
		IsPrivate(lpMessage, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PRIVATE], PT_BOOLEAN)) )
		return MAPI_E_NO_ACCESS;
	hr = HrGetOneProp(lpMessage, PR_ENTRYID, &~lpProps);
	if (hr != hrSuccess)
		return hr;

	sbEid = lpProps[0].Value.bin;
	
	//Create Entrylist
	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~lpEntryList);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrMoveEntry MAPIAllocateBuffer failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	lpEntryList->cValues = 1;

	hr = MAPIAllocateMore(sizeof(SBinary), lpEntryList, (void**)&lpEntryList->lpbin);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrMoveEntry MAPIAllocateMore failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	
	hr = MAPIAllocateMore(sbEid.cb, lpEntryList, (void**)&lpEntryList->lpbin[0].lpb);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrMoveEntry MAPIAllocateMore(2) failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	lpEntryList->lpbin[0].cb = sbEid.cb;
	memcpy(lpEntryList->lpbin[0].lpb, sbEid.lpb, sbEid.cb);

	hr = m_lpUsrFld->CopyMessages(lpEntryList, NULL, lpDestFolder, 0, NULL, MAPI_MOVE);
	if (hr != hrSuccess)
	{
		ec_log_err("Error Deleting Entry");
		return hr;
	}
	
	// publish freebusy for default folder
	if (m_ulFolderFlag & DEFAULT_FOLDER)
		hr = HrPublishDefaultCalendar(m_lpSession, m_lpDefStore, time(NULL), FB_PUBLISH_DURATION);
	if (hr != hrSuccess)
		ec_log_err("Error Publishing Freebusy, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
	return hrSuccess;
}

/**
 * Handles adding new entries and modify existing entries in folder
 *
 * @return	HRESULT
 * @retval	MAPI_E_INVALID_PARAMETER	invalid folder specified in URL
 * @retval	MAPI_E_NO_ACCESS			insufficient permissions on folder or message
 * @retval	MAPI_E_INVALID_OBJECT		no message in ical data.
 */
HRESULT CalDAV::HrPut()
{
	HRESULT hr = hrSuccess;
	std::string strGuid;
	std::string strUrl;
	std::string strIcal;
	std::string strIfMatch;
	SPropValuePtr ptrPropModTime;
	memory_ptr<SPropValue> lpsPropVal;
	eIcalType etype = VEVENT;
	SBinary sbUid;
	time_t ttLastModTime = 0;
	
	object_ptr<IMessage> lpMessage;
	ICalToMapi *lpICalToMapi = NULL;
	bool blNewEntry = false;
	bool bMatch = false;

	m_lpRequest->HrGetUrl(&strUrl);
	
	strGuid = StripGuid(strUrl);

	//Find the Entry with particular Guid
	hr = HrFindAndGetMessage(strGuid, m_lpUsrFld, m_lpNamedProps, &~lpMessage);
	if(hr == hrSuccess)
	{
		// check if entry can be modified by the user
		// check if the user is accessing shared folder
		// check if delegate permissions
		// check if the entry is private
		if ( m_ulFolderFlag & SHARED_FOLDER &&
			!HasDelegatePerm(m_lpDefStore, m_lpActiveStore) &&
			IsPrivate(lpMessage, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PRIVATE], PT_BOOLEAN)) ) 
		{
			hr = MAPI_E_NO_ACCESS;
			goto exit;
		}
	} else {
		SPropValue sProp;

		blNewEntry = true;
		hr = m_lpUsrFld->CreateMessage(nullptr, 0, &~lpMessage);
		if (hr != hrSuccess) {
			ec_log_err("Error creating new message, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
			goto exit;
		}

		// we need to be able to find the message under the url that was used in the PUT
		// PUT /caldav/user/folder/item.ics
		// GET /caldav/user/folder/item.ics
		// and item.ics has UID:unrelated, the above urls should work, so we save the item part in a custom tag.
		sProp.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_APPTTSREF], PT_STRING8);
		sProp.Value.lpszA = (char*)strGuid.c_str();
		hr = HrSetOneProp(lpMessage, &sProp);
		if (hr != hrSuccess) {
			ec_log_err("Error adding property to new message, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
			goto exit;
		}
	}

	bMatch = ! m_lpRequest->CheckIfMatch(lpMessage);
	if (bMatch)
		goto exit;

	//save Ical data to mapi.
	CreateICalToMapi(m_lpActiveStore, m_lpAddrBook, false, &lpICalToMapi);
	
	m_lpRequest->HrGetBody(&strIcal);

	hr = lpICalToMapi->ParseICal(strIcal, m_strCharset, m_strSrvTz, m_lpLoginUser, 0);
	if(hr!=hrSuccess)
	{
		ec_log_err("Error Parsing ical data in PUT request, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		ec_log_debug("Error Parsing ical data: %s", strIcal.c_str());
		goto exit;
	}

	if (lpICalToMapi->GetItemCount() == 0)
	{
		hr = MAPI_E_INVALID_OBJECT;
		ec_log_err("No message in ical data in PUT request, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	if (lpICalToMapi->GetItemCount() > 1)
		ec_log_warn("More than one message found in PUT, trying to combine messages");
	hr = HrGetOneProp(m_lpUsrFld, PR_CONTAINER_CLASS_A, &~lpsPropVal);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrPut get property PR_CONTAINER_CLASS_A failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	hr = lpICalToMapi->GetItemInfo(0, &etype, &ttLastModTime, &sbUid);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrPut no access(1) 0x%x %s", hr, GetMAPIErrorMessage(hr));
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}
	
	// FIXME: fix the check
	if ((etype == VEVENT && strncmp(lpsPropVal->Value.lpszA, "IPF.Appointment", strlen("IPF.Appointment")))
		|| (etype == VTODO && strncmp(lpsPropVal->Value.lpszA, "IPF.Task", strlen("IPF.Task"))))
	{
		ec_log_debug("CalDAV::HrPut no access(2) 0x%x %s", hr, GetMAPIErrorMessage(hr));
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}

	hr = lpICalToMapi->GetItem(0, 0, lpMessage);
	if(hr != hrSuccess)
	{
		ec_log_err("Error converting ical data to Mapi message in PUT request, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	// get other messages if present
	for (ULONG n = 1; n < lpICalToMapi->GetItemCount(); ++n) {
		SBinary sbSubUid;
		eIcalType eSubType = VEVENT;

		hr = lpICalToMapi->GetItemInfo(n, &eSubType, NULL, &sbSubUid);
		if (hr != hrSuccess) {
			ec_log_debug("CalDAV::HrPut no access(3) 0x%x %s", hr, GetMAPIErrorMessage(hr));
			hr = MAPI_E_NO_ACCESS;
			goto exit;
		}

		if (etype != eSubType || Util::CompareSBinary(sbUid, sbSubUid) != hrSuccess) {
			hr = MAPI_E_INVALID_OBJECT;
			ec_log_err("Invalid sub item in ical, unable to save message");
			goto exit;
		}

		// merge in the same message, and hope for the best
		hr = lpICalToMapi->GetItem(n, 0, lpMessage);
		if(hr != hrSuccess)
		{
			ec_log_err("Error converting ical data to Mapi message in PUT request, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
			goto exit;
		}
	}

	hr = lpMessage->SaveChanges(0);
	if (hr != hrSuccess) {
		ec_log_err("Error saving Mapi message in PUT request, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	// new modification time
	if (HrGetOneProp(lpMessage, PR_LAST_MODIFICATION_TIME, &~ptrPropModTime) == hrSuccess)
		m_lpRequest->HrResponseHeader("Etag", SPropValToString(ptrPropModTime));

	// Publish freebusy only for default Calendar
	if (m_ulFolderFlag & DEFAULT_FOLDER &&
	    HrPublishDefaultCalendar(m_lpSession, m_lpDefStore, time(NULL), FB_PUBLISH_DURATION) != hrSuccess)
		// @todo already logged, since we pass the logger in the publish function?
		ec_log_err("Error Publishing Freebusy, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
exit:
	if (hr == hrSuccess && blNewEntry)
		m_lpRequest->HrResponseHeader(201, "Created");
	else if (hr == hrSuccess && bMatch)
		m_lpRequest->HrResponseHeader(412, "Precondition failed");
	else if (hr == hrSuccess)
		m_lpRequest->HrResponseHeader(204, "No Content");
	else if (hr == MAPI_E_NOT_FOUND)
		m_lpRequest->HrResponseHeader(404, "Not Found");
	else if (hr == MAPI_E_NO_ACCESS)
		m_lpRequest->HrResponseHeader(403, "Forbidden");
	else
		m_lpRequest->HrResponseHeader(400, "Bad Request");
	
	delete lpICalToMapi;
	return hr;
}

/**
 * Creates a new guid in the message and returns it
 * 
 * @param[in]	sbEid		EntryID of the message
 * @param[in]	ulPropTag	Property tag of the Guid property
 * @param[out]	lpstrGuid	The newly created guid is returned
 *
 * @return		HRESULT 
 */
HRESULT CalDAV::CreateAndGetGuid(SBinary sbEid, ULONG ulPropTag, std::string *lpstrGuid)
{
	HRESULT hr = hrSuccess;
	string strGuid;
	object_ptr<IMessage> lpMessage;
	ULONG ulObjType = 0;
	memory_ptr<SPropValue> lpProp;

	hr = m_lpActiveStore->OpenEntry(sbEid.cb, reinterpret_cast<ENTRYID *>(sbEid.lpb), nullptr, MAPI_BEST_ACCESS, &ulObjType, &~lpMessage);
	if (hr != hrSuccess) {
		ec_log_err("Error opening message to add Guid, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	hr = HrCreateGlobalID(ulPropTag, NULL, &~lpProp);
	if (hr != hrSuccess) {
		ec_log_err("Error creating Guid, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	hr = lpMessage->SetProps(1, lpProp, NULL);
	if (hr != hrSuccess) {
		ec_log_err("Error while adding Guid to message, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	hr = lpMessage->SaveChanges(0);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::CreateAndGetGuid SaveChanges failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	*lpstrGuid = bin2hex(lpProp->Value.bin.cb, lpProp->Value.bin.lpb);
	return hrSuccess;
}

/**
 * Creates new calendar folder
 * 
 * @param[in]	lpsDavProp		Pointer to WEBDAVPROP structure, contains properite to be set on new folder
 * @return		HRESULT
 * @retval		MAPI_E_NO_ACCESS	Unable to create folder, while accessing single calendar
 * @retval		MAPI_E_COLLISION	Folder with same name already exists
 */
HRESULT CalDAV::HrHandleMkCal(WEBDAVPROP *lpsDavProp)
{
	HRESULT hr = hrSuccess;
	std::wstring wstrNewFldName;
	object_ptr<IMAPIFolder> lpUsrFld;
	SPropValue sPropValSet[2];
	ULONG ulPropTag = 0;
	std::string strContainerClass = "IPF.Appointment";

	// @todo handle other props as in proppatch command
	for (const auto &p : lpsDavProp->lstProps) {
		if (p.sPropName.strPropname.compare("displayname") == 0) {
			wstrNewFldName = U2W(p.strValue);
			continue;
		}
		if (p.sPropName.strPropname.compare("supported-calendar-component-set") != 0)
			continue;
		if (p.strValue.compare("VTODO") == 0)
			strContainerClass = "IPF.Task";
		else if (p.strValue.compare("VEVENT") != 0) {
			ec_log_err("Unable to create folder for supported-calendar-component-set type: %s", p.strValue.c_str());
			return MAPI_E_INVALID_PARAMETER;
		}
	}
	if (wstrNewFldName.empty())
		return MAPI_E_COLLISION;

	// @todo handle conflicts better. caldav conflicts on the url (guid), not the folder name...
	hr = m_lpIPMSubtree->CreateFolder(FOLDER_GENERIC, (LPTSTR)wstrNewFldName.c_str(), nullptr, nullptr, MAPI_UNICODE, &~lpUsrFld);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleMkCal create folder failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	
	sPropValSet[0].ulPropTag = PR_CONTAINER_CLASS_A;
	sPropValSet[0].Value.lpszA = (char*)strContainerClass.c_str();
	sPropValSet[1].ulPropTag = PR_COMMENT_A;
	sPropValSet[1].Value.lpszA = const_cast<char *>("Created by CalDAV Gateway");

	hr = lpUsrFld->SetProps(2, sPropValSet, NULL);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleMkCal SetProps failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	
	ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_FLDID], PT_UNICODE);
	// saves the url name (guid) into the guid named property, @todo fix function name to reflect action better
	hr = HrAddProperty(lpUsrFld, ulPropTag, true, &m_wstrFldName);
	if(hr != hrSuccess) {
		ec_log_err("Cannot Add named property, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	// @todo set all xml properties as named properties on this folder
	return hrSuccess;
}

/**
 * Retrieves the list of calendar and sets it in WEBDAVMULTISTATUS structure
 *
 * @param[in]	sDavProp		Pointer to requested properties of folders
 * @param[out]	lpsMulStatus	Pointer to WEBDAVMULTISTATUS structure, the calendar list and its properties are set in this structure
 *
 * @return		HRESULT
 * @retval		MAPI_E_BAD_VALUE	Method called by a non mac client
 */
/*
 * input		  		output
 * /caldav					list of /caldav/user/FLDPRFX_id
 * /caldav/other			list of /caldav/other/FLDPRFX_id
 * /caldav/other/folder		NO! should not have been called
 * /caldav/public			list of /caldav/public/FLDPRFX_id
 * /caldav/public/folder	NO! should not have been called
 */
HRESULT CalDAV::HrListCalendar(WEBDAVREQSTPROPS *sDavProp, WEBDAVMULTISTATUS *lpsMulStatus)
{
	HRESULT hr = hrSuccess;	
	WEBDAVPROP *lpsDavProp = &sDavProp->sProp;
	object_ptr<IMAPITable> lpHichyTable, lpDelHichyTable;
	object_ptr<IMAPIFolder> lpWasteBox;
	memory_ptr<SPropValue> lpSpropWbEID, lpsPropSingleFld;
	memory_ptr<SPropTagArray> lpPropTagArr;
	LPSRowSet lpRowsALL = NULL;
	LPSRowSet lpRowsDeleted = NULL;
	size_t cbsize = 0;
	ULONG ulPropTagFldId = 0;
	ULONG ulObjType = 0;
	ULONG ulCmp = 0;
	ULONG ulDelEntries = 0;
	WEBDAVRESPONSE sDavResponse;
	std::string strReqUrl;
	int i;

	// @todo, check input url not to have 3rd level path? .. see input/output list above.

	if(!(m_ulUrlFlag & REQ_PUBLIC))
		strReqUrl = "/caldav/" + urlEncode(m_wstrFldOwner, "utf-8") + "/";
	else
		strReqUrl = "/caldav/public/";

	// all folder properties to fill request.
	cbsize = lpsDavProp->lstProps.size() + 2;
	hr = MAPIAllocateBuffer(CbNewSPropTagArray(cbsize), &~lpPropTagArr);
	if(hr != hrSuccess)
	{
		ec_log_err("Cannot allocate memory");
		goto exit;
	}

	ulPropTagFldId = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_FLDID], PT_UNICODE);
	//add PR_ENTRYID & FolderID in setcolumns along with requested data.
	lpPropTagArr->cValues = (ULONG)cbsize;
	lpPropTagArr->aulPropTag[0] = PR_ENTRYID;
	lpPropTagArr->aulPropTag[1] = ulPropTagFldId;
	i = 2;
	for (const auto &iter : lpsDavProp->lstProps)
		lpPropTagArr->aulPropTag[i++] = GetPropIDForXMLProp(m_lpUsrFld, iter.sPropName, m_converter);

	if (m_ulFolderFlag & SINGLE_FOLDER)
	{
		hr = m_lpUsrFld->GetProps(lpPropTagArr, 0, reinterpret_cast<ULONG *>(&cbsize), &~lpsPropSingleFld);
		if (FAILED(hr)) {
			ec_log_debug("CalDAV::HrListCalendar GetProps failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
			goto exit;
		}

		hr = HrMapValtoStruct(m_lpUsrFld, lpsPropSingleFld, cbsize, NULL, 0, true, &lpsDavProp->lstProps, &sDavResponse);
		if (hr != hrSuccess) {
			ec_log_debug("CalDAV::HrListCalendar HrMapValtoStruct failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
			goto exit;
		}

		lpsMulStatus->lstResp.push_back(sDavResponse);
		goto exit;
	}

	hr = HrGetSubCalendars(m_lpSession, m_lpIPMSubtree, nullptr, &~lpHichyTable);
	if (hr != hrSuccess) {
		ec_log_err("Error retrieving subcalendars for IPM_Subtree, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	// public definitly doesn't have a wastebasket to filter
	if ((m_ulUrlFlag & REQ_PUBLIC) == 0)
	{
		// always try to get the wastebasket from the current store to filter calendars from
		// make it optional, because we may not have rights on the folder
		hr = HrGetOneProp(m_lpActiveStore, PR_IPM_WASTEBASKET_ENTRYID, &~lpSpropWbEID);
		if(hr != hrSuccess)
		{
			ec_log_debug("CalDAV::HrListCalendar HrGetOneProp(PR_IPM_WASTEBASKET_ENTRYID) failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
			hr = hrSuccess;
			goto nowaste;
		}
		hr = m_lpActiveStore->OpenEntry(lpSpropWbEID->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpSpropWbEID->Value.bin.lpb), nullptr, MAPI_BEST_ACCESS, &ulObjType, &~lpWasteBox);
		if(hr != hrSuccess)
		{
			hr = hrSuccess;
			goto nowaste;
		}
		hr = HrGetSubCalendars(m_lpSession, lpWasteBox, nullptr, &~lpDelHichyTable);
		if(hr != hrSuccess)
		{
			hr = hrSuccess;
			goto nowaste;
		}
	}

nowaste:
	hr = lpHichyTable->SetColumns(lpPropTagArr, 0);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrListCalendar SetColumns failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	if (lpDelHichyTable) {
		hr = lpDelHichyTable->SetColumns(lpPropTagArr, 0);
		if (hr != hrSuccess) {
			ec_log_debug("CalDAV::HrListCalendar SetColumns(2) failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
			goto exit;
		}
	}

	while(1)
	{
		hr = lpHichyTable->QueryRows(50, 0, &lpRowsALL);
		if(hr != hrSuccess || lpRowsALL->cRows == 0)
			break;

		if (lpDelHichyTable)
			hr = lpDelHichyTable->QueryRows(50, 0, &lpRowsDeleted);
		if(hr != hrSuccess)
			break;

		for (ULONG i = 0; i < lpRowsALL->cRows; ++i) {
			std::wstring wstrFldPath;

			if (lpDelHichyTable && lpRowsDeleted->cRows != 0 && ulDelEntries != lpRowsDeleted->cRows)
			{
				// @todo is this optimized, or just pure luck that this works? don't we need a loop?
				ulCmp = memcmp(lpRowsALL->aRow[i].lpProps[0].Value.bin.lpb,
					       lpRowsDeleted->aRow[ulDelEntries].lpProps[0].Value.bin.lpb,
					       lpRowsALL->aRow[i].lpProps[0].Value.bin.cb);
				if(ulCmp == 0)
				{
					++ulDelEntries;
					continue;
				}
			}

			HrSetDavPropName(&(sDavResponse.sPropName), "response", lpsDavProp->sPropName.strNS);

			if (lpRowsALL->aRow[i].lpProps[1].ulPropTag == ulPropTagFldId)
				wstrFldPath = lpRowsALL->aRow[i].lpProps[1].Value.lpszW;
			else if (lpRowsALL->aRow[i].lpProps[0].ulPropTag == PR_ENTRYID)
				// creates new ulPropTagFldId on this folder, or return PR_ENTRYID in wstrFldPath
				// @todo boolean should become default return proptag if save fails, PT_NULL for no default
				hr = HrAddProperty(m_lpActiveStore, lpRowsALL->aRow[i].lpProps[0].Value.bin, ulPropTagFldId, true, &wstrFldPath);

			if (hr != hrSuccess || wstrFldPath.empty()) {
				ec_log_err("Error adding Folder id property, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
				continue;
			}
			// @todo FOLDER_PREFIX only needed for ulPropTagFldId versions
			// but it doesn't seem we return named folders here? why not?

			wstrFldPath = FOLDER_PREFIX + wstrFldPath + L"/";

			HrSetDavPropName(&(sDavResponse.sHRef.sPropName), "href", lpsDavProp->sPropName.strNS);
			sDavResponse.sHRef.strValue = strReqUrl + W2U(wstrFldPath);
			HrMapValtoStruct(m_lpUsrFld, lpRowsALL->aRow[i].lpProps, lpRowsALL->aRow[i].cValues, NULL, 0, true, &lpsDavProp->lstProps, &sDavResponse);

			lpsMulStatus->lstResp.push_back(sDavResponse);
			sDavResponse.lstsPropStat.clear();
		}
		FreeProws(lpRowsALL);
		lpRowsALL = NULL;
		if(lpRowsDeleted)
		{
			FreeProws(lpRowsDeleted);
			lpRowsDeleted = NULL;
		}
	}

exit:
	if(lpRowsALL)
		FreeProws(lpRowsALL);
	
	if(lpRowsDeleted)
		FreeProws(lpRowsDeleted);
	return hr;
}

/**
 * Sets the values on a MAPI object (folder) for PROPPATCH request
 *
 * @note although the proppatch command should return a 207
 * multistatus, setting each prop their own success or error code, the
 * Mac iCal App doesn't parse that, and the user will be left clueless
 * on the update. When we return a direct http error code, iCal App
 * alerts the user (rename default calendar, or rename to an already
 * existing calendar folder.
 *
 * @param[in]	lpsDavProp	WEBDAVPROP structure containing properties to be modified and its values
 *
 * @return		HRESULT
 * @retval		MAPI_E_NO_ACCESS	Error returned while renaming the default calendar
 *
 * @todo handle all properties in proppatch, and handle unknown properties using named props, which should be returned as patched.
 */
HRESULT CalDAV::HrHandlePropPatch(WEBDAVPROP *lpsDavProp, WEBDAVMULTISTATUS *lpsMultiStatus)
{
	HRESULT hr;
	std::wstring wstrConvProp;
	SPropValue sProp;
	WEBDAVRESPONSE sDavResponse;
	WEBDAVPROPSTAT sPropStatusOK;
	WEBDAVPROPSTAT sPropStatusForbidden;
	WEBDAVPROPSTAT sPropStatusCollision;

	HrSetDavPropName(&lpsMultiStatus->sPropName, "multistatus", WEBDAVNS);
	HrSetDavPropName(&sDavResponse.sPropName, "response", WEBDAVNS);
	HrSetDavPropName(&sDavResponse.sHRef.sPropName, "href", WEBDAVNS);
	m_lpRequest->HrGetRequestUrl(&sDavResponse.sHRef.strValue);

	HrSetDavPropName(&sPropStatusOK.sPropName, "propstat", WEBDAVNS);
	HrSetDavPropName(&sPropStatusOK.sStatus.sPropName, "status", WEBDAVNS);
	sPropStatusOK.sStatus.strValue = "HTTP/1.1 200 OK";
	HrSetDavPropName(&sPropStatusOK.sProp.sPropName, "prop", WEBDAVNS);

	HrSetDavPropName(&sPropStatusForbidden.sPropName, "propstat", WEBDAVNS);
	HrSetDavPropName(&sPropStatusForbidden.sStatus.sPropName, "status", WEBDAVNS);
	sPropStatusForbidden.sStatus.strValue = "HTTP/1.1 403 Forbidden";
	HrSetDavPropName(&sPropStatusForbidden.sProp.sPropName, "prop", WEBDAVNS);

	HrSetDavPropName(&sPropStatusCollision.sPropName, "propstat", WEBDAVNS);
	HrSetDavPropName(&sPropStatusCollision.sStatus.sPropName, "status", WEBDAVNS);
	sPropStatusCollision.sStatus.strValue = "HTTP/1.1 409 Conflict";
	HrSetDavPropName(&sPropStatusCollision.sProp.sPropName, "prop", WEBDAVNS);
	
	for (const auto &iter : lpsDavProp->lstProps) {
		WEBDAVPROPERTY sDavProp;
		sDavProp.sPropName = iter.sPropName; // only copy the propname + namespace part, value is empty
		sProp.ulPropTag = PR_NULL;
		if (iter.sPropName.strPropname == "displayname") {
			// deny rename of default Calendar
			if (!m_blFolderAccess) {
				sPropStatusForbidden.sProp.lstProps.push_back(std::move(sDavProp));
				continue;
			}
		} else if (iter.sPropName.strPropname == "calendar-free-busy-set") {
			// not allowed to select which calendars give freebusy information
			sPropStatusForbidden.sProp.lstProps.push_back(std::move(sDavProp));
			continue;
		} else if (iter.sPropName.strNS.compare(WEBDAVNS) == 0) {
			// only DAV:displayname may be modified, the rest is read-only
			sPropStatusForbidden.sProp.lstProps.push_back(std::move(sDavProp));
			continue;
		}

		sProp.ulPropTag = GetPropIDForXMLProp(m_lpUsrFld, iter.sPropName, m_converter, MAPI_CREATE);
		if (sProp.ulPropTag == PR_NULL) {
			sPropStatusForbidden.sProp.lstProps.push_back(std::move(sDavProp));
			continue;
		}

		if (PROP_TYPE(sProp.ulPropTag) == PT_UNICODE) {
			wstrConvProp = U2W(iter.strValue);
			sProp.Value.lpszW = (WCHAR*)wstrConvProp.c_str();
		} else {
			sProp.Value.bin.cb = iter.strValue.size();
			sProp.Value.bin.lpb = reinterpret_cast<BYTE *>(const_cast<char *>(iter.strValue.data()));
		}

		hr = m_lpUsrFld->SetProps(1, &sProp, NULL);
		if (hr == hrSuccess) {
			sPropStatusOK.sProp.lstProps.push_back(std::move(sDavProp));
			continue;
		}
		if (hr == MAPI_E_COLLISION) {
			// set error 409 collision
			sPropStatusCollision.sProp.lstProps.push_back(std::move(sDavProp));
			// returned on folder rename, directly return an error and skip all other properties, see note above
			return hr;
		}
		// set error 403 forbidden
		sPropStatusForbidden.sProp.lstProps.push_back(std::move(sDavProp));
	}

	// @todo, maybe only do this for certain Mac iCal app versions?
	if (!sPropStatusForbidden.sProp.lstProps.empty())
		return MAPI_E_CALL_FAILED;
	else if (!sPropStatusCollision.sProp.lstProps.empty())
		return MAPI_E_COLLISION;

	// this is the normal code path to return the correct 207 Multistatus

	if (!sPropStatusOK.sProp.lstProps.empty())
		sDavResponse.lstsPropStat.push_back(std::move(sPropStatusOK));
	if (!sPropStatusForbidden.sProp.lstProps.empty())
		sDavResponse.lstsPropStat.push_back(std::move(sPropStatusForbidden));
	if (!sPropStatusCollision.sProp.lstProps.empty())
		sDavResponse.lstsPropStat.push_back(std::move(sPropStatusCollision));
	lpsMultiStatus->lstResp.push_back(std::move(sDavResponse));
	return hrSuccess;
}

/**
 * Processes the POST request from caldav client
 *
 * POST is used to request freebusy info of attendees or send
 * meeting request to attendees(used by mac ical.app only)
 *
 * @return	HRESULT
 */
HRESULT CalDAV::HrHandlePost()
{
	HRESULT hr = hrSuccess;
	std::string strIcal;
	std::unique_ptr<ICalToMapi> lpIcalToMapi;

	hr = m_lpRequest->HrGetBody(&strIcal);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandlePost HrGetBody failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	
	CreateICalToMapi(m_lpDefStore, m_lpAddrBook, false, &unique_tie(lpIcalToMapi));
	if (!lpIcalToMapi)
	{
		ec_log_debug("CalDAV::HrHandlePost CreateICalToMapi failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}

	hr = lpIcalToMapi->ParseICal(strIcal, m_strCharset, m_strSrvTz, m_lpLoginUser, 0);
	if (hr != hrSuccess) {
		ec_log_err("Unable to parse received ical message: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	if (lpIcalToMapi->GetFreeBusyInfo(NULL, NULL, NULL, NULL) == hrSuccess)
		return HrHandleFreebusy(lpIcalToMapi.get());
	return HrHandleMeeting(lpIcalToMapi.get());
}

/**
 * Handles the caldav clients's request to view freebusy information
 * of attendees
 *
 * @param[in]	lpIcalToMapi	The ical to mapi conversion object
 * @return		HRESULT
 */
HRESULT CalDAV::HrHandleFreebusy(ICalToMapi *lpIcalToMapi)
{
	HRESULT hr = hrSuccess;	
	object_ptr<ECFreeBusySupport> lpecFBSupport;
	object_ptr<IFreeBusySupport> lpFBSupport;
	std::unique_ptr<MapiToICal> lpMapiToIcal;
	time_t tStart  = 0;
	time_t tEnd = 0;
	std::list<std::string> *lstUsers = NULL;
	std::string strUID;
	WEBDAVFBINFO sWebFbInfo;
	SPropValuePtr ptrEmail;

	hr = lpIcalToMapi->GetFreeBusyInfo(&tStart, &tEnd, &strUID, &lstUsers);	
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleFreebusy GetFreeBusyInfo failed 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	CreateMapiToICal(m_lpAddrBook, "utf-8", &unique_tie(lpMapiToIcal));
	if (lpMapiToIcal == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	hr = ECFreeBusySupport::Create(&~lpecFBSupport);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleFreebusy ECFreeBusySupport::Create failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	hr = lpecFBSupport->QueryInterface(IID_IFreeBusySupport, &~lpFBSupport);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleFreebusy QueryInterface(IID_IFreeBusySupport) failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	hr = lpecFBSupport->Open(m_lpSession, m_lpDefStore, true);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleFreebusy open session failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	hr = HrGetOneProp(m_lpActiveUser, PR_SMTP_ADDRESS_A, &~ptrEmail);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleFreebusy get prop smtp address a failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	sWebFbInfo.strOrganiser = ptrEmail->Value.lpszA;
	sWebFbInfo.tStart = tStart;
	sWebFbInfo.tEnd = tEnd;
	sWebFbInfo.strUID = strUID;
	hr = HrGetFreebusy(lpMapiToIcal.get(), lpFBSupport, m_lpAddrBook, lstUsers, &sWebFbInfo);
	if (hr != hrSuccess) {
		// @todo, print which users?
		ec_log_err("Unable to get freebusy information for %zu users: 0x%08X", lstUsers->size(), hr);
		return hr;
	}
	
	hr = WebDav::HrPostFreeBusy(&sWebFbInfo);
	if (hr != hrSuccess)
		ec_log_debug("CalDAV::HrHandleFreebusy WebDav::HrPostFreeBusy failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
	return hr;
}

/**
 * Handles Mac ical.app clients request to send a meeting request
 * to the attendee.
 *
 * @param[in]	lpIcalToMapi	ical to mapi conversion object
 * @return		HRESULT
 */
HRESULT CalDAV::HrHandleMeeting(ICalToMapi *lpIcalToMapi)
{
	HRESULT hr = hrSuccess;	
	memory_ptr<SPropValue> lpsGetPropVal;
	object_ptr<IMAPIFolder> lpOutbox;
	object_ptr<IMessage> lpNewMsg;
	SPropValue lpsSetPropVals[2] = {{0}};
	ULONG cValues = 0;
	ULONG ulObjType = 0;
	time_t tModTime = 0;
	SBinary sbEid = {0};
	eIcalType etype = VEVENT;	
	static constexpr const SizedSPropTagArray(2, sPropTagArr) =
		{2, {PR_IPM_OUTBOX_ENTRYID, PR_IPM_SENTMAIL_ENTRYID}};

	hr = lpIcalToMapi->GetItemInfo( 0, &etype, &tModTime, &sbEid);
	if ( hr != hrSuccess || etype != VEVENT)
	{
		hr = hrSuccess; // skip VFREEBUSY
		goto exit;
	}
	hr = m_lpDefStore->GetProps(sPropTagArr, 0, &cValues, &~lpsGetPropVal);
	if (hr != hrSuccess && cValues != 2) {
		ec_log_debug("CalDAV::HrHandleMeeting GetProps failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}
	hr = m_lpDefStore->OpenEntry(lpsGetPropVal[0].Value.bin.cb, reinterpret_cast<ENTRYID *>(lpsGetPropVal[0].Value.bin.lpb), nullptr, MAPI_BEST_ACCESS, &ulObjType, &~lpOutbox);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleMeeting OpenEntry failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}
	hr = lpOutbox->CreateMessage(nullptr, MAPI_BEST_ACCESS, &~lpNewMsg);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleMeeting CreateMessage failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	hr = lpIcalToMapi->GetItem(0, IC2M_NO_ORGANIZER, lpNewMsg);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleMeeting GetItem failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	lpsSetPropVals[0].ulPropTag = PR_SENTMAIL_ENTRYID;
	lpsSetPropVals[0].Value.bin = lpsGetPropVal[1].Value.bin;

	lpsSetPropVals[1].ulPropTag = PR_DELETE_AFTER_SUBMIT;
	lpsSetPropVals[1].Value.b = false;

	hr = lpNewMsg->SetProps(2, lpsSetPropVals, NULL);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleMeeting SetProps failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	hr = lpNewMsg->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrHandleMeeting SaveChanges failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	hr = lpNewMsg->SubmitMessage(0);
	if (hr != hrSuccess) {
		ec_log_err("Unable to submit message: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

exit:
	if(hr == hrSuccess)
		m_lpRequest->HrResponseHeader(200, "Ok");
	else
		m_lpRequest->HrResponseHeader(400, "Bad Request");
	return hr;
}

/**
 * Converts the mapi message specified by EntryID to ical string.
 *
 * @param[in]	lpEid		EntryID of the mapi msg to be converted
 * @param[in]	lpMtIcal	mapi to ical conversion object
 * @param[in]	ulFlags		Flags used for mapi to ical conversion
 * @param[out]	strIcal		ical string output
 * @return		HRESULT
 */
HRESULT CalDAV::HrConvertToIcal(const SPropValue *lpEid, MapiToICal *lpMtIcal,
    ULONG ulFlags, std::string *lpstrIcal)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMessage> lpMessage;
	ULONG ulObjType = 0;

	hr = m_lpActiveStore->OpenEntry(lpEid->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpEid->Value.bin.lpb), nullptr, MAPI_BEST_ACCESS, &ulObjType, &~lpMessage);
	if (hr != hrSuccess && ulObjType == MAPI_MESSAGE)
	{
		ec_log_err("Error opening calendar entry, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	hr = lpMtIcal->AddMessage(lpMessage, m_strSrvTz, ulFlags);
	if (hr != hrSuccess)
	{
		ec_log_err("Error converting mapi message to ical, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	hr = lpMtIcal->Finalize(0, NULL, lpstrIcal);
	if (hr != hrSuccess)
	{
		ec_log_err("Error creating ical data, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	lpMtIcal->ResetObject();
	return hrSuccess;
}

/**
 * Set Values for properties requested by caldav client
 * 
 * @param[in]	lpObj			IMAPIProp object, same as lpProps comes from
 * @param[in]	lpProps			SpropValue array containing values of requested properties
 * @param[in]	ulPropCount		Count of propety values present in lpProps
 * @param[in]	lpMtIcal		mapi to ical conversion object pointer
 * @param[in]	ulFlags			Flags used during mapi to ical conversion (currently only censor flag)
 * @param[in]	bPropsFirst		first lpProps parameter, then defaults if true
 * @param[in]	lstDavProps		Pointer to structure containing properties requested by client
 * @param[out]	lpsResponse		Pointer to Response structure
 * @return		HRESULT
 * @retval		hrSuccess		Always returns hrSuccess
 */
// @todo cleanup this code, and fix url values
HRESULT CalDAV::HrMapValtoStruct(LPMAPIPROP lpObj, LPSPropValue lpProps, ULONG ulPropCount, MapiToICal *lpMtIcal, ULONG ulFlags, bool bPropsFirst, std::list<WEBDAVPROPERTY> *lstDavProps, WEBDAVRESPONSE *lpsResponse)
{
	HRESULT hr;
	WEBDAVPROPERTY sWebProperty;
	std::string strIcal;
	std::string strOwnerURL;
	std::string strCurrentUserURL;
	std::string strPrincipalURL;
	std::string strCalHome;
	WEBDAVPROP sWebProp;
	WEBDAVPROP sWebPropNotFound;
	WEBDAVPROPSTAT sPropStat;
	ULONG ulFolderType;
	SPropValuePtr ptrEmail;
	SPropValuePtr ptrFullname;

	auto lpFoundProp = PCpropFindProp(lpProps, ulPropCount, PR_CONTAINER_CLASS_A);
	if (lpFoundProp && !strncmp (lpFoundProp->Value.lpszA, "IPF.Appointment", strlen("IPF.Appointment")))
		ulFolderType = CALENDAR_FOLDER;
	else if (lpFoundProp && !strncmp (lpFoundProp->Value.lpszA, "IPF.Task", strlen("IPF.Task")))
		ulFolderType = TASKS_FOLDER;
	else
		ulFolderType = OTHER_FOLDER;
	HrGetOneProp(m_lpActiveUser, PR_SMTP_ADDRESS_A, &~ptrEmail);
	HrGetOneProp(m_lpActiveUser, PR_DISPLAY_NAME_W, &~ptrFullname);

	// owner is DAV namespace, the owner of the resource (url)
	strOwnerURL = "/caldav/" + urlEncode(m_wstrFldOwner, "utf-8") + "/";
	strCurrentUserURL = "/caldav/" + urlEncode(m_wstrUser, "utf-8") + "/";
	// principal always /caldav/m_wstrFldOwner/, except public: full url
	if (m_ulUrlFlag & REQ_PUBLIC) {
		m_lpRequest->HrGetRequestUrl(&strPrincipalURL);
		strCalHome = strPrincipalURL;
	} else {
		strPrincipalURL = "/caldav/" + urlEncode(m_wstrFldOwner, "utf-8") + "/";

		// @todo, displayname of default calendar if empty() ? but see todo in usage below also.
		if (!m_wstrFldName.empty()) {
			strCalHome = strPrincipalURL + urlEncode(m_wstrFldName, "utf-8") + "/";
		} else {
			lpFoundProp = PCpropFindProp(lpProps, ulPropCount, PR_DISPLAY_NAME_W);
			if (lpFoundProp)
				strCalHome = strPrincipalURL + urlEncode(lpFoundProp->Value.lpszW, "utf-8") + "/";
		}
	}

	HrSetDavPropName(&(sWebProp.sPropName), "prop", WEBDAVNS);
	HrSetDavPropName(&(sWebPropNotFound.sPropName), "prop", WEBDAVNS);
	for (const auto &iterprop : *lstDavProps) {
		WEBDAVVALUE sWebVal;

		sWebProperty.lstItems.clear();
		sWebProperty.lstValues.clear();
		
		sWebProperty = iterprop;
		const std::string &strProperty = sWebProperty.sPropName.strPropname;

		lpFoundProp = PCpropFindProp(lpProps, ulPropCount, GetPropIDForXMLProp(lpObj, sWebProperty.sPropName, m_converter));
		if (strProperty == "resourcetype") {
			
			// do not set resourcetype for REPORT request(ical data)
			if(!lpMtIcal){
				HrSetDavPropName(&(sWebVal.sPropName), "collection", WEBDAVNS);
				sWebProperty.lstValues.push_back(sWebVal);
			}

			if (lpFoundProp && (!strcmp(lpFoundProp->Value.lpszA ,"IPF.Appointment") || !strcmp(lpFoundProp->Value.lpszA , "IPF.Task"))) {
				HrSetDavPropName(&(sWebVal.sPropName), "calendar", CALDAVNS);
				sWebProperty.lstValues.push_back(sWebVal);
			} else if (m_wstrFldName == L"Inbox") {
				HrSetDavPropName(&(sWebVal.sPropName), "schedule-inbox", CALDAVNS);
				sWebProperty.lstValues.push_back(sWebVal);
			} else if (m_wstrFldName == L"Outbox") {
				HrSetDavPropName(&(sWebVal.sPropName), "schedule-outbox", CALDAVNS);
				sWebProperty.lstValues.push_back(sWebVal);
			}

		} else if (strProperty == "displayname" && (!bPropsFirst || lpFoundProp)) {
			// foldername from given properties (propfind command) username from properties (propsearch command) or fullname of user ("root" props)
			if (bPropsFirst)
				sWebProperty.strValue = SPropValToString(lpFoundProp);
			else
				sWebProperty.strValue = W2U(ptrFullname->Value.lpszW);
			
		} else if (strProperty == "calendar-user-address-set" && (m_ulUrlFlag & REQ_PUBLIC) == 0 && !!ptrEmail) {
			// rfc draft only: http://tools.ietf.org/html/draft-desruisseaux-caldav-sched-11
			HrSetDavPropName(&(sWebVal.sPropName), "href", WEBDAVNS);
			sWebVal.strValue = string("mailto:") + ptrEmail->Value.lpszA;
			sWebProperty.lstValues.push_back(sWebVal);

		} else if (strProperty == "acl" || strProperty == "current-user-privilege-set") {
			
			HrBuildACL(&sWebProperty);

		} else if (strProperty == "supported-report-set") {
			
			HrBuildReportSet(&sWebProperty);

		} else if (lpFoundProp && 
					(strProperty == "calendar-description" || 
					 strProperty == "last-name" || 
					 strProperty == "first-name")
					) {
			
			sWebProperty.strValue = SPropValToString(lpFoundProp);

		} else if (strProperty == "getctag" || strProperty == "getetag") {
			// ctag and etag should always be present
			if (lpFoundProp)
				sWebProperty.strValue = SPropValToString(lpFoundProp);
			else
				// this happens when a client (evolution) queries the getctag (local commit time max) on the IPM Subtree
				// (incorrectly configured client)
				sWebProperty.strValue = "0";
		} else if (strProperty == "email-address-set" && (!!ptrEmail || lpFoundProp)) {
			// email from properties (propsearch command) or fullname of user ("root" props)
			HrSetDavPropName(&(sWebVal.sPropName), "email-address", WEBDAVNS);
			sWebVal.strValue = lpFoundProp ? SPropValToString(lpFoundProp) : ptrEmail->Value.lpszA;
			sWebProperty.lstValues.push_back(sWebVal);

		} else if (strProperty == "schedule-inbox-URL" && (m_ulUrlFlag & REQ_PUBLIC) == 0) {
			HrSetDavPropName(&(sWebVal.sPropName), "href", WEBDAVNS);
			sWebVal.strValue = strCurrentUserURL + "Inbox/";
			sWebProperty.lstValues.push_back(sWebVal);

		} else if (strProperty == "schedule-outbox-URL" && (m_ulUrlFlag & REQ_PUBLIC) == 0) {
			HrSetDavPropName(&(sWebVal.sPropName), "href", WEBDAVNS);
			sWebVal.strValue = strCurrentUserURL + "Outbox/";
			sWebProperty.lstValues.push_back(sWebVal);

		} else if (strProperty == "supported-calendar-component-set") {
			
			if (ulFolderType == CALENDAR_FOLDER) {
				HrSetDavPropName(&(sWebVal.sPropName), "comp","name", "VEVENT", CALDAVNS);
				sWebProperty.lstValues.push_back(sWebVal);

				// actually even only for the standard calendar folder
				HrSetDavPropName(&(sWebVal.sPropName), "comp","name", "VFREEBUSY", CALDAVNS);
				sWebProperty.lstValues.push_back(sWebVal);
			}
			else if (ulFolderType == TASKS_FOLDER) {
				HrSetDavPropName(&(sWebVal.sPropName), "comp","name", "VTODO", CALDAVNS);
				sWebProperty.lstValues.push_back(sWebVal);
			}

			HrSetDavPropName(&(sWebVal.sPropName), "comp","name", "VTIMEZONE", CALDAVNS);
			sWebProperty.lstValues.push_back(sWebVal);

		} else if (lpFoundProp && lpMtIcal && strProperty == "calendar-data") {
			
			hr = HrConvertToIcal(lpFoundProp, lpMtIcal, ulFlags, &strIcal);
			sWebProperty.strValue = strIcal;
			if (hr != hrSuccess || sWebProperty.strValue.empty()){
				// ical data is empty so discard this calendar entry
				HrSetDavPropName(&(lpsResponse->sStatus.sPropName), "status", WEBDAVNS);
				lpsResponse->sStatus.strValue = "HTTP/1.1 404 Not Found";
				return hr;
			}

			strIcal.clear();

		} else if (strProperty == "calendar-order") {
			
			if (ulFolderType == CALENDAR_FOLDER) {
				lpFoundProp = PCpropFindProp(lpProps, ulPropCount, PR_ENTRYID);
				if (lpFoundProp)
					HrGetCalendarOrder(lpFoundProp->Value.bin, &sWebProperty.strValue);

			} else  {
				// @todo leave not found for messages?

				// set value to 2 for tasks and non default calendar
				// so that ical.app shows default calendar in the list first everytime
				// if this value is left empty, ical.app tries to reset the order
				sWebProperty.strValue = "2";
			}

		} else if (strProperty == "getcontenttype") {

			sWebProperty.strValue = "text/calendar";

		} else if (strProperty == "principal-collection-set") {
			// DAV:
			// This protected property of a resource contains a set of
			// URLs that identify the root collections that contain
			// the principals that are available on the server that
			// implements this resource.

			sWebProperty.strValue = "/caldav/";

		} else if (strProperty == "current-user-principal") {
			// webdav rfc5397
			// We should return the currently authenticated user principal url, but due to sharing, Mac iCal 10.8 gets confused
			// and will use this url as the actual store being accessed, therefor disabling sharing through a different url.
			// So we return the current accessed user principal url to continue in the correct store.
			HrSetDavPropName(&(sWebVal.sPropName), "href", WEBDAVNS);
			sWebVal.strValue = strPrincipalURL;
			sWebProperty.lstValues.push_back(sWebVal);

		} else if (strProperty == "owner") {

			HrSetDavPropName(&(sWebVal.sPropName), "href", WEBDAVNS);
			// always self
			sWebVal.strValue = strOwnerURL;
			sWebProperty.lstValues.push_back(sWebVal);

		} else if (strProperty == "principal-URL") {

			HrSetDavPropName(&(sWebVal.sPropName), "href", WEBDAVNS);
			// self or delegate
			sWebVal.strValue = strPrincipalURL;
			sWebProperty.lstValues.push_back(sWebVal);

		} else if (strProperty == "calendar-home-set" && !strCalHome.empty()) {
			// do not set on public, so thunderbird/lightning doesn't require calendar-user-address-set, schedule-inbox-URL and schedule-outbox-URL
			// public doesn't do meeting requests
			// check here, because lpFoundProp is set to display name and isn't binary
			if ((m_ulUrlFlag & REQ_PUBLIC) == 0 || strAgent.find("Lightning") == string::npos) {
				// Purpose: Identifies the URL of any WebDAV collections that contain
				//          calendar collections owned by the associated principal resource.
				// apple seems to use this as the root container where you have your calendars (and would create more)
				// MKCALENDAR would be called with this url as a base.
				HrSetDavPropName(&(sWebVal.sPropName), "href", WEBDAVNS);
				sWebVal.strValue = strPrincipalURL;
				sWebProperty.lstValues.push_back(sWebVal);
			}
		} else if (strProperty == "calendar-user-type") {
			if (SPropValToString(lpFoundProp) == "0")
				sWebProperty.strValue = "INDIVIDUAL";
		} else if (strProperty == "record-type"){

			sWebProperty.strValue = "users";
		} else if (lpFoundProp && lpFoundProp->ulPropTag != PR_NULL) {
			sWebProperty.strValue.assign((char*)lpFoundProp->Value.bin.lpb, lpFoundProp->Value.bin.cb);
		} else {
			sWebPropNotFound.lstProps.push_back(sWebProperty);
			continue;
		}
		sWebProp.lstProps.push_back(sWebProperty);
	}
	
	HrSetDavPropName(&(sPropStat.sPropName), "propstat", WEBDAVNS);
	HrSetDavPropName(&(sPropStat.sStatus.sPropName), "status", WEBDAVNS);

	if( !sWebProp.lstProps.empty()) {
		sPropStat.sStatus.strValue = "HTTP/1.1 200 OK";
		sPropStat.sProp = sWebProp;
		lpsResponse->lstsPropStat.push_back (sPropStat);
	}
	
	if( !sWebPropNotFound.lstProps.empty()) {
		sPropStat.sStatus.strValue = "HTTP/1.1 404 Not Found";
		sPropStat.sProp = sWebPropNotFound;
		lpsResponse->lstsPropStat.push_back (sPropStat);
	}

	return hrSuccess;
}

/**
 * Sets the Calendar Order to 1 for default calendar folder
 *
 * The function checks if the folder is the default calendar folder, if true sets
 * the calendar-order to 1 or else is set to 2
 *
 * The calendar-order property is set to show the default calendar first in 
 * the calendar list in ical.app(Mac). This makes the default kopano calendar default
 * in ical.app too.
 *
 * if the value is left empty ical.app tries to reset the order and sometimes sets
 * a tasks folder as default calendar
 *
 * @param[in]	sbEid				Entryid of the Folder to be checked
 * @param[out]	wstrCalendarOrder	string output in which the calendar order is set
 * @return		mapi error codes
 * @retval		MAPI_E_CALL_FAILED	the calendar-order is not set for this folder
 */
HRESULT CalDAV::HrGetCalendarOrder(SBinary sbEid, std::string *lpstrCalendarOrder)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMAPIFolder> lpRootCont;
	memory_ptr<SPropValue> lpProp;
	ULONG ulObjType = 0;
	ULONG ulResult = 0;

	lpstrCalendarOrder->assign("2");
	hr = m_lpActiveStore->OpenEntry(0, nullptr, nullptr, 0, &ulObjType, &~lpRootCont);
	if (hr != hrSuccess || ulObjType != MAPI_FOLDER) {
		ec_log_err("Error opening root Container of user %ls, error code: (0x%08X)", m_wstrUser.c_str(), hr);
		return hr;
	}

	// get default calendar folder entry id from root container
	hr = HrGetOneProp(lpRootCont, PR_IPM_APPOINTMENT_ENTRYID, &~lpProp);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrGetCalendarOrder getprop PR_IPM_APPOINTMENT_ENTRYID failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	hr = m_lpActiveStore->CompareEntryIDs(sbEid.cb, (LPENTRYID)sbEid.lpb, lpProp->Value.bin.cb, (LPENTRYID)lpProp->Value.bin.lpb, 0, &ulResult);
	if (hr == hrSuccess && ulResult == TRUE)
		lpstrCalendarOrder->assign("1");
	return hr;
}
/**
 * Handles the MOVE http request
 *
 * The request moves mapi message from one folder to another folder
 * The url request refers to the current location of the message and
 * the destination tag in http header specifies the destination folder.
 * The message guid value is same in both url and destination tag.
 *
 * @return mapi error codes
 *
 * @retval MAPI_E_DECLINE_COPY	The mapi message is not moved as etag values does not match
 * @retval MAPI_E_NOT_FOUND		The mapi message refered by guid is not found
 * @retval MAPI_E_NO_ACCESS		The user does not sufficient rights on the mapi message
 *
 */
HRESULT CalDAV::HrMove()
{
	HRESULT hr = hrSuccess;
	object_ptr<IMAPIFolder> lpDestFolder;
	std::string strDestination;
	std::string strDestFolder;
	std::string strGuid;
	
	hr = m_lpRequest->HrGetDestination(&strDestination);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrMove HrGetDestination failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	hr = HrParseURL(strDestination, NULL, NULL, &strDestFolder);
	if (hr != hrSuccess)
		goto exit;
	hr = HrFindFolder(m_lpActiveStore, m_lpIPMSubtree, m_lpNamedProps, U2W(strDestFolder), &~lpDestFolder);
	if (hr != hrSuccess) {
		ec_log_debug("CalDAV::HrMove HrFindFolder failed: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}
	
	strGuid = StripGuid(strDestination);

	hr = HrMoveEntry(strGuid, lpDestFolder);

exit:
	// @todo - set e-tag value for the new saved message, so ical.app does not send the GET request
	if (hr == hrSuccess)
		m_lpRequest->HrResponseHeader(200, "OK");
	else if (hr == MAPI_E_DECLINE_COPY)
		m_lpRequest->HrResponseHeader(412, "Precondition Failed"); // entry is modifid on server (sunbird & TB)
	else if( hr == MAPI_E_NOT_FOUND)
		m_lpRequest->HrResponseHeader(404, "Not Found");
	else if(hr == MAPI_E_NO_ACCESS)
		m_lpRequest->HrResponseHeader(403, "Forbidden");
	else
		m_lpRequest->HrResponseHeader(400, "Bad Request");
	return hr;
}
