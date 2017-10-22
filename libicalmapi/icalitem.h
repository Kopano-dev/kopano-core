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
	ULONG ulRecipientType;
	/* tentative, canceled */
	ULONG ulTrackStatus;
	std::wstring strEmail;
	std::wstring strName;
	ULONG cbEntryID;
	LPENTRYID lpEntryID;		/* realloced to icalitem.base !! */
};

struct icalitem {
	KCHL::memory_ptr<char> base; /* pointer on which we use MAPIAllocateMore, to only need to free this pointer */
	eIcalType eType;
	time_t tLastModified;
	SPropValue sBinGuid;
	TIMEZONE_STRUCT tTZinfo;
	ULONG ulFbStatus;
	std::unique_ptr<recurrence> lpRecurrence;
	std::list<SPropValue> lstMsgProps; /* all objects are allocated more on icalitem pointer */
	std::list<ULONG> lstDelPropTags; /* properties to delete from message */
	std::list<icalrecip> lstRecips;	/* list of all recipients */	
	struct exception {
		time_t tBaseDate;
		time_t tStartDate;
		std::list<SPropValue> lstAttachProps;
		std::list<SPropValue> lstMsgProps;
		std::list<icalrecip> lstRecips;
	};
	std::list<exception> lstExceptionAttachments;
};

} /* namespace */

#endif
