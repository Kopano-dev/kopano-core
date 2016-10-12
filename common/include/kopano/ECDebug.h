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

#ifndef ECDEBUGTOOLS
#define ECDEBUGTOOLS

#include <string>
#include <sstream>
#include <mapi.h>
#include <mapix.h>
#include <mapicode.h>
#include <edkmdb.h>

#include <kopano/ECDefs.h>

#ifndef DEBUGBUFSIZE
#define DEBUGBUFSIZE	1024
#endif

struct MAPIResultCodes {
	HRESULT		hResult;
	const char* error;
};

struct INFOGUID {
	int		ulType; //0=mapi,1=exchange,2=new,3=kopano,4=windows/other, 10=ontdekte
	GUID	*guid;
	const char *szguidname;
};

std::string GetMAPIErrorDescription( HRESULT hResult );

std::string DBGGUIDToString(REFIID iid);
std::string MapiNameIdListToString(ULONG cNames, const MAPINAMEID *const *ppNames, const SPropTagArray *pptaga = NULL);
std::string MapiNameIdToString(const MAPINAMEID *pNameId);

std::string PropNameFromPropTagArray(const SPropTagArray *lpPropTagArray);
std::string PropNameFromPropArray(ULONG cValues, const SPropValue *lpPropArray);
std::string PropNameFromPropTag(ULONG ulPropTag);
std::string RestrictionToString(const SRestriction *lpRestriction, unsigned int indent=0);
std::string RowToString(const SRow *lpRow);
std::string RowSetToString(const SRowSet *lpRows);
std::string AdrRowSetToString(const ADRLIST *lpAdrList, const FlagList *lpFlagList);
std::string RowEntryToString(const ROWENTRY *lpRowEntry);
std::string RowListToString(const ROWLIST *lprowList);
const char *ActionToString(const ACTION *);

std::string SortOrderToString(const SSortOrder *lpSort);
std::string SortOrderSetToString(const SSortOrderSet *lpSortCriteria);

std::string NotificationToString(ULONG cNotification, const NOTIFICATION *lpNotification);

std::string ProblemArrayToString(const SPropProblemArray *lpProblemArray);

const char *MsgServiceContextToString(ULONG ulContext);
const char *ResourceTypeToString(ULONG ulResourceType);

//Internal used only
const char *RelationalOperatorToString(ULONG relop);
std::string FuzzyLevelToString(ULONG ulFuzzyLevel);
std::string PropValueToString(const SPropValue *lpPropValue);
std::string EntryListToString(const ENTRYLIST *lpMsgList);
std::string PermissionRulesToString(ULONG cPermissions, const ECPERMISSION *lpECPermissions);

#endif
