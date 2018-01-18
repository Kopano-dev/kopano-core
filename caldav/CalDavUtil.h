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

#ifndef CALDAV_UTIL_H_
#define CALDAV_UTIL_H_

#include "WebDav.h"
#include <kopano/mapiext.h>
#include <kopano/mapiguidext.h>
#include <cstring>
#include <algorithm>
#include <kopano/namedprops.h>
#include "nameids.h"
#include "icaluid.h"
#include <edkguid.h>
#include "freebusy.h"
#include "ECFreeBusySupport.h"
#include "MAPIToICal.h"

#define FOLDER_PREFIX L"FLDPRFX_"

// folder type according to URL
#define SINGLE_FOLDER	0x01
#define DEFAULT_FOLDER	0x02
#define SHARED_FOLDER	0x04

// folder type according to content
#define OTHER_FOLDER	0x01
#define CALENDAR_FOLDER 0x02
#define TASKS_FOLDER	0x03

//Performs login to the Kopano server and returns Session.
extern HRESULT HrAuthenticate(const std::string &app_vers, const std::string &app_misc, const std::wstring &user, const std::wstring &pass, const std::string &path, IMAPISession **);

//Adds property FolderID to the folder if not present else returns it.
HRESULT HrAddProperty(IMsgStore *lpMsgStore, SBinary sbEid, ULONG ulPropertyId, bool bIsFldID, std::wstring *wstrProperty);

//Adds property FolderID && dispidApptTsRef to the folder & message respectively, if not present else returns it.
HRESULT HrAddProperty(IMAPIProp *lpMapiProp, ULONG ulPropertyId, bool bIsFldID, std::wstring *wstrProperty);

//Finds folder from hierarchy table refering to the Folder ID, entryid or folder name
extern HRESULT HrFindFolder(IMsgStore *, IMAPIFolder *root, SPropTagArray *lpNamedProps, const std::wstring &wstrFldId, IMAPIFolder **ufld);

//Adds data to structure for acl request.
HRESULT HrBuildACL(WEBDAVPROPERTY *lpsProperty);

// Generate supported report set.
HRESULT HrBuildReportSet(WEBDAVPROPERTY *lpsProperty);

//Retrieve the User's Email address.
HRESULT HrGetOwner(IMAPISession *lpSession, IMsgStore *lpDefStore, IMailUser **lppMailUser);

//Strip the input to get Guid Value
//eg input: caldav/Calendar/ai-43873034lakljk403-3245.ics
//return: ai-43873034lakljk403-3245
std::string StripGuid(const std::string &strInput);

//Returns Calendars of Folder and sorted by PR_ENTRY_ID.
HRESULT HrGetSubCalendars(IMAPISession *lpSession, IMAPIFolder *lpFolderIn, SBinary *lpsbEid, IMAPITable **lppTable);

// Checks for private message.
bool IsPrivate(LPMESSAGE lpMessage, ULONG ulPropIDPrivate);

bool HasDelegatePerm(IMsgStore *lpDefStore, IMsgStore *lpSharedStore);

HRESULT HrMakeRestriction(const std::string &strGuid, LPSPropTagArray lpNamedProps, LPSRestriction *lpsRectrict);
extern HRESULT HrFindAndGetMessage(const std::string &guid, IMAPIFolder *, SPropTagArray *props, IMessage **);
extern HRESULT HrGetFreebusy(KC::MapiToICal *, IFreeBusySupport *, IAddrBook *, std::list<std::string> *users, WEBDAVFBINFO *);

#endif
