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

#ifndef USERUTIL_INCLUDED
#define USERUTIL_INCLUDED

#include <set>
#include <list>
#include <string>

#include <mapidefs.h>
#include <mapix.h>
class ECLogger;

HRESULT ValidateArchivedUserCount(ECLogger *lpLogger, IMAPISession *lpMapiSession, const char *lpSSLKey, const char *lpSSLPass, unsigned int *lpulArchivedUsers, unsigned int *lpulMaxUsers);
HRESULT GetArchivedUserCount(ECLogger *lpLogger, IMAPISession *lpMapiSession, const char *lpSSLKey, const char *lpSSLPass, unsigned int *lpulArchivedUsers);

HRESULT GetArchivedUserList(ECLogger *lpLogger, IMAPISession *lpMapiSession, const char *lpSSLKey, const char *lpSSLPass, std::list<std::string> *lplstUsers, bool bLocalOnly = false);
HRESULT GetArchivedUserList(ECLogger *lpLogger, IMAPISession *lpMapiSession, const char *lpSSLKey, const char *lpSSLPass, std::list<std::wstring> *lplstUsers, bool bLocalOnly = false);

class DataCollector
{
public:
	virtual HRESULT GetRequiredPropTags(LPMAPIPROP lpProp, LPSPropTagArray *lppPropTagArray) const;
	virtual HRESULT GetRestriction(LPMAPIPROP lpProp, LPSRestriction *lppRestriction);
	virtual HRESULT CollectData(LPMAPITABLE lpStoreTable) = 0;
};

HRESULT GetMailboxData(ECLogger *lpLogger, IMAPISession *lpMapiSession, const char *lpSSLKey, const char *lpSSLPass, bool bLocalOnly, DataCollector *lpCollector);

#endif
