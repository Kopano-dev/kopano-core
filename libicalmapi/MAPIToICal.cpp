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
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <memory>
#include <new>
#include <kopano/memory.hpp>
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
#include "icalmem.hpp"

namespace KC {

class MapiToICalImpl _kc_final : public MapiToICal {
public:
	MapiToICalImpl(LPADRBOOK lpAdrBook, const std::string &strCharset);
	HRESULT AddMessage(LPMESSAGE lpMessage, const std::string &strSrvTZ, ULONG ulFlags) _kc_override;
	HRESULT AddBlocks(FBBlock_1 *pblk, LONG ulblocks, time_t tStart, time_t tEnd, const std::string &strOrganiser, const std::string &strUser, const std::string &strUID) _kc_override;
	HRESULT Finalize(ULONG ulFlags, std::string *strMethod, std::string *strIcal) _kc_override;
	HRESULT ResetObject(void) _kc_override;
	
private:
	LPADRBOOK m_lpAdrBook;
	std::string m_strCharset;
	memory_ptr<SPropTagArray> m_lpNamedProps;
	/* since we don't want depending projects to add include paths for libical, this is only in the implementation version */
	icalcomp_ptr m_lpicCalender;
	icalproperty_method m_icMethod = ICAL_METHOD_NONE;
	timezone_map m_tzMap;			// contains all used timezones
	ULONG m_ulEvents = 0;

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
	*lppMapiToICal = new(std::nothrow) MapiToICalImpl(lpAdrBook, strCharset);
	if (*lppMapiToICal == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	return hrSuccess;
}

/** 
 * Init MapiToICal class
 * 
 * @param[in] lpAdrBook MAPI addressbook
 * @param[in] strCharset charset of the ical returned by this class
 */
MapiToICalImpl::MapiToICalImpl(LPADRBOOK lpAdrBook,
    const std::string &strCharset) :
	m_lpAdrBook(lpAdrBook), m_strCharset(strCharset)
{
	// initialize empty ical data
	HrInitializeVCal();
}

/**
 * Initialize ical component with basic ical info
 */
HRESULT MapiToICalImpl::HrInitializeVCal()
{
	m_lpicCalender.reset(icalcomponent_new(ICAL_VCALENDAR_COMPONENT));
	icalcomponent_add_property(m_lpicCalender.get(), icalproperty_new_version("2.0"));
	icalcomponent_add_property(m_lpicCalender.get(), icalproperty_new_prodid("-//Kopano//" PROJECT_VERSION "//EN"));
	icalcomponent_add_property(m_lpicCalender.get(), icalproperty_new_calscale("GREGORIAN"));
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
	std::unique_ptr<VConverter> lpVEC;
	std::list<icalcomponent*> lstEvents;
	icalproperty_method icMethod = ICAL_METHOD_NONE;
	memory_ptr<SPropValue> lpMessageClass;
	TIMEZONE_STRUCT ttTZinfo = {0};
	bool blCensor = false;

	if(ulFlags & M2IC_CENSOR_PRIVATE)
		blCensor = true;

	if (lpMessage == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (m_lpNamedProps == NULL) {
		auto hr = HrLookupNames(lpMessage, &~m_lpNamedProps);
		if (hr != hrSuccess)
			return hr;
	}
	auto hr = HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &~lpMessageClass);
	if (hr != hrSuccess)
		return hr;
	if (strcasecmp(lpMessageClass->Value.lpszA, "IPM.Task") == 0) {
		hr = HrGetTzStruct(strSrvTZ, &ttTZinfo);
		if(hr == hrSuccess)
			m_tzMap[strSrvTZ] = ttTZinfo;	// keep timezone pointer for tasks
	}

	if (strcasecmp(lpMessageClass->Value.lpszA, "IPM.Task") == 0)
		lpVEC.reset(new VTodoConverter(m_lpAdrBook, &m_tzMap, m_lpNamedProps, m_strCharset, blCensor, false, NULL));
	else if (strcasecmp(lpMessageClass->Value.lpszA, "IPM.Appointment") == 0 || strncasecmp(lpMessageClass->Value.lpszA, "IPM.Schedule", strlen("IPM.Schedule")) == 0)
		lpVEC.reset(new VEventConverter(m_lpAdrBook, &m_tzMap, m_lpNamedProps, m_strCharset, blCensor, false, NULL));
	else
		return MAPI_E_TYPE_NO_SUPPORT;

	// converts item to ical item (e.g. IPM.Appointment to VEVENT)
	hr = lpVEC->HrMAPI2ICal(lpMessage, &icMethod, &lstEvents);
	if (hr != hrSuccess)
		return hr;
	for (auto ev : lstEvents) {
		++m_ulEvents;
		icalcomponent_add_component(m_lpicCalender.get(), ev);
	}

	if (m_icMethod != ICAL_METHOD_NONE && m_icMethod != icMethod)
		m_icMethod = ICAL_METHOD_PUBLISH;
	else
		m_icMethod = icMethod;
	return hrSuccess;
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
	icalcomponent *icFbComponent = NULL;

	if (m_lpicCalender == NULL) {
		m_lpicCalender.reset(icalcomponent_new(ICAL_VCALENDAR_COMPONENT));
		icalcomponent_add_property(m_lpicCalender.get(), icalproperty_new_version("2.0"));
		icalcomponent_add_property(m_lpicCalender.get(), icalproperty_new_prodid("-//Kopano//" PROJECT_VERSION "//EN"));
	}
	
	HRESULT hr = HrFbBlock2ICal(lpsFbblk, ulBlocks, tStart, tEnd,
	             strOrganiser, strUser, strUID, &icFbComponent);
	if (hr != hrSuccess)
		return hr;

	m_icMethod = ICAL_METHOD_PUBLISH;
	icalcomponent_add_component(m_lpicCalender.get(), icFbComponent);
	return hrSuccess;
}

/** 
 * Create the actual ICal data.
 * 
 * @param[in]  ulFlags Conversion flags
 * @arg @c M2IC_NO_VTIMEZONE Skip the VTIMEZONE parts in the output
 * @param[out] strMethod ICal method (e.g. PUBLISH)
 * @param[out] strIcal The ICal data in 8-bit string, charset given in constructor
 * 
 * @return MAPI error code
 */
HRESULT MapiToICalImpl::Finalize(ULONG ulFlags, std::string *strMethod, std::string *strIcal)
{
	icalmem_ptr ics;
	icalcomponent *lpVTZComp = NULL;

	if (strMethod == nullptr && strIcal == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	// TODO: make flags force a publish method
	if (m_icMethod != ICAL_METHOD_NONE)
		icalcomponent_add_property(m_lpicCalender.get(), icalproperty_new_method(m_icMethod));
	
	// no timezone block in VFREEBUSY data.
	if ((ulFlags & M2IC_NO_VTIMEZONE) == 0)
	{
		for (auto &tzp : m_tzMap) {
			auto hr = HrCreateVTimeZone(tzp.first, tzp.second, &lpVTZComp);
			if (hr == hrSuccess)
				icalcomponent_add_component(m_lpicCalender.get(), lpVTZComp);
		}
	}

	ics.reset(icalcomponent_as_ical_string_r(m_lpicCalender.get()));
	if (ics == nullptr)
		return MAPI_E_CALL_FAILED;
	if (strMethod)
		*strMethod = icalproperty_method_to_string(m_icMethod);

	if (strIcal)
		*strIcal = ics.get();
	return hrSuccess;
}

/** 
 * Reset this class to be used for starting a new series of conversions.
 * 
 * @return always hrSuccess
 */
HRESULT MapiToICalImpl::ResetObject()
{
	// no need to remove named properties
	m_lpicCalender.reset();
	m_icMethod = ICAL_METHOD_NONE;
	m_tzMap.clear();
	m_ulEvents = 0;

	// reset the ical data with emtpy calendar
	HrInitializeVCal();

	return hrSuccess;
}

} /* namespace */
