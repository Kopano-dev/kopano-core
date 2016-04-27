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
#include "MAPIToICal.h"
#include <libical/ical.h>

#include "nameids.h"
#include "vconverter.h"
#include "vevent.h"
#include "vtodo.h"
#include "vtimezone.h"
#include "vfreebusy.h"
#include <mapiutil.h>
#include <mapicode.h>
#include <mapix.h>
#include <kopano/ecversion.h>

class MapiToICalImpl : public MapiToICal {
public:
	MapiToICalImpl(LPADRBOOK lpAdrBook, const std::string &strCharset);
	virtual ~MapiToICalImpl();

	HRESULT AddMessage(LPMESSAGE lpMessage, const std::string &strSrvTZ, ULONG ulFlags);
	HRESULT AddBlocks(FBBlock_1 *pblk, LONG ulblocks, time_t tStart, time_t tEnd, const std::string &strOrganiser, const std::string &strUser, const std::string &strUID);
	HRESULT Finalize(ULONG ulFlags, std::string *strMethod, std::string *strIcal);
	HRESULT ResetObject();
	
private:
	LPADRBOOK m_lpAdrBook;
	std::string m_strCharset;

	LPSPropTagArray m_lpNamedProps;
	/* since we don't want depending projects to add include paths for libical, this is only in the implementation version */
	icalcomponent *m_lpicCalender;
	icalproperty_method m_icMethod;
	timezone_map m_tzMap;			// contains all used timezones

	ULONG m_ulEvents;

	HRESULT HrInitializeVCal();
};

/** 
 * Create a class implementing the MapiToICal "interface".
 * 
 * @param[in]  lpAdrBook MAPI addressbook
 * @param[in]  strCharset charset of the ical returned by this class
 * @param[out] lppMapiToICal The conversion class
 */
HRESULT CreateMapiToICal(LPADRBOOK lpAdrBook, const std::string &strCharset, MapiToICal **lppMapiToICal)
{
	if (!lpAdrBook || !lppMapiToICal)
		return MAPI_E_INVALID_PARAMETER;
	try {
		*lppMapiToICal = new MapiToICalImpl(lpAdrBook, strCharset);
	} catch (...) {
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}
	return hrSuccess;
}

/** 
 * Init MapiToICal class
 * 
 * @param[in] lpAdrBook MAPI addressbook
 * @param[in] strCharset charset of the ical returned by this class
 */
MapiToICalImpl::MapiToICalImpl(LPADRBOOK lpAdrBook, const std::string &strCharset)
{
	m_lpAdrBook = lpAdrBook;
	m_strCharset = strCharset;

	m_lpNamedProps = NULL;
	m_lpicCalender = NULL;
	m_icMethod = ICAL_METHOD_NONE;
	m_ulEvents = 0;

	// initialize empty ical data
	HrInitializeVCal();
}

/** 
 * Frees all used memory in conversion
 * 
 */
MapiToICalImpl::~MapiToICalImpl()
{
	if (m_lpicCalender)
		icalcomponent_free(m_lpicCalender);
	MAPIFreeBuffer(m_lpNamedProps);
}
/**
 * Initialize ical component with basic ical info
 */
HRESULT MapiToICalImpl::HrInitializeVCal()
{
	m_lpicCalender = icalcomponent_new(ICAL_VCALENDAR_COMPONENT);
	icalcomponent_add_property(m_lpicCalender, icalproperty_new_version("2.0"));
	icalcomponent_add_property(m_lpicCalender, icalproperty_new_prodid("-//Kopano//" PROJECT_VERSION_DOT_STR "-" PROJECT_SVN_REV_STR "//EN"));
	icalcomponent_add_property(m_lpicCalender, icalproperty_new_calscale("GREGORIAN"));

	return hrSuccess;
}
/** 
 * Add a MAPI message to the ical VCALENDAR object.
 * 
 * @param[in] lpMessage Convert this MAPI message to ICal
 * @param[in] strSrvTZ Use this timezone, used for Tasks only (or so it seems)
 * @param[in] ulFlags Conversion flags:
 * @arg @c M2IC_CENSOR_PRIVATE Privacy sensitive data will not be present in the ICal
 * 
 * @return MAPI error code
 */
HRESULT MapiToICalImpl::AddMessage(LPMESSAGE lpMessage, const std::string &strSrvTZ, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	VConverter *lpVEC = NULL;
	std::list<icalcomponent*> lstEvents;
	std::list<icalcomponent *>::const_iterator iEvents;
	icalproperty_method icMethod = ICAL_METHOD_NONE;
	LPSPropValue lpMessageClass = NULL;
	TIMEZONE_STRUCT ttTZinfo = {0};
	bool blCensor = false;

	if(ulFlags & M2IC_CENSOR_PRIVATE)
		blCensor = true;

	if (lpMessage == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if (m_lpNamedProps == NULL) {
		hr = HrLookupNames(lpMessage, &m_lpNamedProps);
		if (hr != hrSuccess)
			goto exit;
	}

	hr = HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &lpMessageClass);
	if (hr != hrSuccess)
		goto exit;

	if (stricmp(lpMessageClass->Value.lpszA, "IPM.Task") == 0) {
		hr = HrGetTzStruct(strSrvTZ, &ttTZinfo);
		if(hr == hrSuccess)
			m_tzMap[strSrvTZ] = ttTZinfo;	// keep timezone pointer for tasks
	}

	if (stricmp(lpMessageClass->Value.lpszA, "IPM.Task") == 0) {
		lpVEC = new VTodoConverter(m_lpAdrBook, &m_tzMap, m_lpNamedProps, m_strCharset, blCensor, false, NULL);
	} else if (stricmp(lpMessageClass->Value.lpszA, "IPM.Appointment") == 0 || strnicmp(lpMessageClass->Value.lpszA, "IPM.Schedule", strlen("IPM.Schedule")) == 0) {
		lpVEC = new VEventConverter(m_lpAdrBook, &m_tzMap, m_lpNamedProps, m_strCharset, blCensor, false, NULL);
	} else {
		hr = MAPI_E_TYPE_NO_SUPPORT;
		goto exit;
	}

	// converts item to ical item (eg. IPM.Appointment to VEVENT)
	hr = lpVEC->HrMAPI2ICal(lpMessage, &icMethod, &lstEvents);
	if (hr != hrSuccess)
		goto exit;

	for (iEvents = lstEvents.begin(); iEvents != lstEvents.end(); ++iEvents) {
		++m_ulEvents;
		icalcomponent_add_component(m_lpicCalender, *iEvents);
	}

	if (m_icMethod != ICAL_METHOD_NONE && m_icMethod != icMethod)
		m_icMethod = ICAL_METHOD_PUBLISH;
	else
		m_icMethod = icMethod;

exit:
	MAPIFreeBuffer(lpMessageClass);
	delete lpVEC;
	return hr;
}

/** 
 * Add MAPI freebusy blocks in a VFREEBUSY part.
 * 
 * @param[in] lpsFbblk MAPI freebusy blocks
 * @param[in] ulBlocks Number of blocks present in lpsFbblk
 * @param[in] tStart Start time of the freebusy data
 * @param[in] tEnd End time of the freebusy data
 * @param[in] strOrganiser Email address of the organiser
 * @param[in] strUser Email address of an attendee
 * @param[in] strUID UID for the VFREEBUSY part
 * 
 * @return MAPI error code
 */
HRESULT MapiToICalImpl::AddBlocks(FBBlock_1 *lpsFbblk, LONG ulBlocks, time_t tStart, time_t tEnd, const std::string &strOrganiser, const std::string &strUser, const std::string &strUID)
{
	HRESULT hr = hrSuccess;
	icalcomponent *icFbComponent = NULL;

	if (m_lpicCalender == NULL) {
		m_lpicCalender = icalcomponent_new(ICAL_VCALENDAR_COMPONENT);

		icalcomponent_add_property(m_lpicCalender, icalproperty_new_version("2.0"));
		icalcomponent_add_property(m_lpicCalender, icalproperty_new_prodid("-//Kopano//" PROJECT_VERSION_DOT_STR "-" PROJECT_SVN_REV_STR "//EN"));		
	}
	
	hr  = HrFbBlock2ICal(lpsFbblk, ulBlocks, tStart, tEnd, strOrganiser, strUser, strUID, &icFbComponent);
	if (hr != hrSuccess)
		goto exit;

	m_icMethod = ICAL_METHOD_PUBLISH;
	icalcomponent_add_component(m_lpicCalender,icFbComponent);

exit:
	return hr;
}

/** 
 * Create the actual ICal data.
 * 
 * @param[in]  ulFlags Conversion flags
 * @arg @c M2IC_NO_VTIMEZONE Skip the VTIMEZONE parts in the output
 * @param[out] strMethod ICal method (eg. PUBLISH)
 * @param[out] strIcal The ICal data in 8bit string, charset given in constructor
 * 
 * @return MAPI error code
 */
HRESULT MapiToICalImpl::Finalize(ULONG ulFlags, std::string *strMethod, std::string *strIcal)
{
	HRESULT hr = hrSuccess;
	char *ics = NULL;
	timezone_map_iterator iTZMap;
	icalcomponent *lpVTZComp = NULL;

	if (strMethod == NULL && strIcal == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// TODO: make flags force a publish method
	if (m_icMethod != ICAL_METHOD_NONE)
		icalcomponent_add_property(m_lpicCalender, icalproperty_new_method(m_icMethod));
	
	// no timezone block in VFREEBUSY data.
	if ((ulFlags & M2IC_NO_VTIMEZONE) == 0)
	{
		for (iTZMap = m_tzMap.begin(); iTZMap != m_tzMap.end(); ++iTZMap) {
			hr = HrCreateVTimeZone(iTZMap->first, iTZMap->second, &lpVTZComp);
			if (hr == hrSuccess)
				icalcomponent_add_component(m_lpicCalender, lpVTZComp);
		}
		hr = hrSuccess;
	}

	ics = icalcomponent_as_ical_string_r(m_lpicCalender);
	if (!ics) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	if (strMethod)
		*strMethod = icalproperty_method_to_string(m_icMethod);

	if (strIcal)
		*strIcal = ics;

exit:
	if (ics)
		icalmemory_free_buffer(ics);

	return hr;
}

/** 
 * Reset this class to be used for starting a new series of conversions.
 * 
 * @return always hrSuccess
 */
HRESULT MapiToICalImpl::ResetObject()
{
	// no need to remove named properties
	
	if (m_lpicCalender)
		icalcomponent_free(m_lpicCalender);
	m_lpicCalender = NULL;

	m_icMethod = ICAL_METHOD_NONE;
	m_tzMap.clear();
	m_ulEvents = 0;

	// reset the ical data with emtpy calendar
	HrInitializeVCal();

	return hrSuccess;
}
