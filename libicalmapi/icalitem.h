/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ICALMAPI_ICALITEM_H
#define ICALMAPI_ICALITEM_H

#include <list>
#include <memory>
#include <string>
#include <mapidefs.h>
#include <kopano/memory.hpp>
#include "recurrence.h"

namespace KC {

enum eIcalType { VEVENT, VTODO, VJOURNAL };

struct icalrecip {
	/* recipient type (From==organizer, To==attendee, CC==opt attendee ?)) */
	unsigned int ulRecipientType = 0;
	/* tentative, canceled */
	unsigned int ulTrackStatus = 0;
	std::wstring strEmail;
	std::wstring strName;
	unsigned int cbEntryID = 0;
	ENTRYID *lpEntryID = nullptr; /* realloced to icalitem.base !! */
};

struct icalitem {
	memory_ptr<char> base; /* pointer on which we use MAPIAllocateMore, to only need to free this pointer */
	eIcalType eType = VEVENT;
	time_t tLastModified = 0;
	SPropValue sBinGuid{};
	TIMEZONE_STRUCT tTZinfo{};
	unsigned int ulFbStatus = 0;
	std::unique_ptr<recurrence> lpRecurrence;
	std::list<SPropValue> lstMsgProps; /* all objects are allocated more on icalitem pointer */
	std::list<ULONG> lstDelPropTags; /* properties to delete from message */
	std::list<icalrecip> lstRecips;	/* list of all recipients */	
	struct exception {
		time_t tBaseDate = 0, tStartDate = 0;
		std::list<SPropValue> lstAttachProps;
		std::list<SPropValue> lstMsgProps;
		std::list<icalrecip> lstRecips;
	};
	std::list<exception> lstExceptionAttachments;
};

} /* namespace */

#endif
