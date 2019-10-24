/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <utility>
#include "vfreebusy.h"
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include <kopano/timeutil.hpp>
#include "icalmem.hpp"
#include "nameids.h"

namespace KC {

/** 
 * Converts a VFREEBUSY object to separate parts
 * 
 * @param[in]  lpFbcomp The ical component containing the VFREEBUSY
 * @param[out] lptStart Returns the DTSTART property
 * @param[out] lptEnd Returns the DTEND property
 * @param[out] lpstrUID Returns the UID property
 * @param[out] lstUsers Appends any email addresses of ATTENDEEs listed
 * 
 * @return Always hrSuccess
 */
HRESULT HrGetFbInfo(icalcomponent *lpFbcomp, time_t *lptStart, time_t *lptEnd, std::string *lpstrUID, std::list<std::string> *lstUsers)
{
	std::string strEmail;

	// DTSTART
	auto lpicProp = icalcomponent_get_first_property(lpFbcomp, ICAL_DTSTART_PROPERTY);
	if (lpicProp)
		*lptStart = icaltime_as_timet (icalproperty_get_dtstart (lpicProp));
	// DTEND
	lpicProp = icalcomponent_get_first_property(lpFbcomp, ICAL_DTEND_PROPERTY);
	if (lpicProp)
		*lptEnd = icaltime_as_timet (icalproperty_get_dtend (lpicProp));
	// UID
	lpicProp = icalcomponent_get_first_property(lpFbcomp, ICAL_UID_PROPERTY);
	if (lpicProp)
		*lpstrUID = icalproperty_get_uid(lpicProp);

	// ATTENDEE
	for (lpicProp = icalcomponent_get_first_property(lpFbcomp, ICAL_ATTENDEE_PROPERTY);
	     lpicProp != nullptr;
	     lpicProp = icalcomponent_get_next_property(lpFbcomp, ICAL_ATTENDEE_PROPERTY))
	{
		strEmail = icalproperty_get_attendee(lpicProp);
		if (strncasecmp(strEmail.c_str(), "mailto:", 7) == 0)
			strEmail.erase(0, 7);
		lstUsers->emplace_back(std::move(strEmail));
	}
	return hrSuccess;
}

/** 
 * Converts a MAPI freebusy block to a VFREEBUSY ical component.
 * 
 * @param[in]  lpsFbblk MAPI freebusy info blocks
 * @param[in]  ulBlocks Number of blocks in lpsFbblk
 * @param[in]  tDtStart Unix timestamp with the start date
 * @param[in]  tDtEnd Unix timestamp with the end date
 * @param[in]  strOrganiser The email address of the organiser
 * @param[in]  strUser The email address of an attendee
 * @param[in]  strUID UID of the freebusy data
 * @param[out] lpicFbComponent new VFREEBUSY ical component
 * 
 * @return MAPI error code
 */
HRESULT HrFbBlock2ICal(FBBlock_1 *lpsFbblk, LONG ulBlocks, time_t tDtStart, time_t tDtEnd, const std::string &strOrganiser, const std::string &strUser, const std::string &strUID, icalcomponent **lpicFbComponent)
{
	icalperiodtype icalPeriod;
	icalcomp_ptr lpFbComp(icalcomponent_new(ICAL_VFREEBUSY_COMPONENT));
	if (lpFbComp == NULL)
		return MAPI_E_INVALID_PARAMETER;
	
	//DTSTART
	auto ittStamp = icaltime_from_timet_with_zone(tDtStart, false, icaltimezone_get_utc_timezone());
	icalprop_ptr lpicProp(icalproperty_new(ICAL_DTSTART_PROPERTY));
	if (lpicProp == NULL)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	icalproperty_set_value(lpicProp.get(), icalvalue_new_datetime(ittStamp));
	icalcomponent_add_property(lpFbComp.get(), lpicProp.release());

	//DTEND
	ittStamp = icaltime_from_timet_with_zone(tDtEnd, false, icaltimezone_get_utc_timezone());	
	lpicProp.reset(icalproperty_new(ICAL_DTEND_PROPERTY));
	if (lpicProp == NULL)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	icalproperty_set_value(lpicProp.get(), icalvalue_new_datetime(ittStamp));
	icalcomponent_add_property(lpFbComp.get(), lpicProp.release());

	//DTSTAMP
	ittStamp = icaltime_from_timet_with_zone(time(NULL), false, icaltimezone_get_utc_timezone());	
	lpicProp.reset(icalproperty_new(ICAL_DTSTAMP_PROPERTY));
	if (lpicProp == NULL)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	icalproperty_set_value(lpicProp.get(), icalvalue_new_datetime(ittStamp));
	icalcomponent_add_property(lpFbComp.get(), lpicProp.release());
	
	//UID
	if (strUID.size() > 0) {
		lpicProp.reset(icalproperty_new(ICAL_UID_PROPERTY));
		if (lpicProp == NULL)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		icalproperty_set_uid(lpicProp.get(), strUID.c_str());
		icalcomponent_add_property(lpFbComp.get(), lpicProp.release());
	}

	//ORGANIZER
	lpicProp.reset(icalproperty_new(ICAL_ORGANIZER_PROPERTY));
	if (lpicProp == NULL)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	icalproperty_set_organizer(lpicProp.get(), strOrganiser.c_str());
	icalcomponent_add_property(lpFbComp.get(), lpicProp.release());
	
	//ATTENDEE
	auto strEmail = "mailto:" + strUser;
	lpicProp.reset(icalproperty_new_attendee(strEmail.c_str()));
	if (lpicProp == NULL)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	
	// param PARTSTAT
	icalparam_ptr icalParam(icalparameter_new_partstat(ICAL_PARTSTAT_ACCEPTED));
	icalproperty_add_parameter(lpicProp.get(), icalParam.release());
	
	// param CUTYPE
	icalParam.reset(icalparameter_new_cutype(ICAL_CUTYPE_INDIVIDUAL));
	icalproperty_add_parameter(lpicProp.get(), icalParam.release());

	icalcomponent_add_property(lpFbComp.get(), lpicProp.release());

	// add all freebusy blocks
	for (int i = 0; i < ulBlocks; ++i) {
		// FREEBUSY
		lpicProp.reset(icalproperty_new(ICAL_FREEBUSY_PROPERTY));
		if (lpicProp == NULL)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		icalPeriod.start = icaltime_from_timet_with_zone(RTimeToUnixTime(lpsFbblk[i].m_tmStart), false, icaltimezone_get_utc_timezone());
		icalPeriod.end = icaltime_from_timet_with_zone(RTimeToUnixTime(lpsFbblk[i].m_tmEnd), false, icaltimezone_get_utc_timezone());
		icalproperty_set_freebusy(lpicProp.get(), icalPeriod);

		switch (lpsFbblk[i].m_fbstatus)
		{	
		case fbBusy:
			icalParam.reset(icalparameter_new_fbtype(ICAL_FBTYPE_BUSY));
			break;
		case fbTentative:
			icalParam.reset(icalparameter_new_fbtype(ICAL_FBTYPE_BUSYTENTATIVE));
			break;
		case fbOutOfOffice:
			icalParam.reset(icalparameter_new_fbtype (ICAL_FBTYPE_BUSYUNAVAILABLE));
			break;
		default:
			icalParam.reset(icalparameter_new_fbtype(ICAL_FBTYPE_FREE));
			break;
		}
		
		icalproperty_add_parameter(lpicProp.get(), icalParam.release());
		icalcomponent_add_property(lpFbComp.get(), lpicProp.release());
	}
	
	icalcomponent_end_component(lpFbComp.get(), ICAL_VFREEBUSY_COMPONENT);
	*lpicFbComponent = lpFbComp.release();
	return hrSuccess;
}

} /* namespace */
