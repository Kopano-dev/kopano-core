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

#ifndef ECSECURITYOFFLINE
#define ECSECURITYOFFLINE

#include <kopano/zcdefs.h>
#include "ECSecurity.h"

class ECSecurityOffline _zcp_final : public ECSecurity {
public:
	ECSecurityOffline(ECSession *lpSession, ECConfig *lpConfig);
	virtual ~ECSecurityOffline(void);

	virtual int GetAdminLevel();
	virtual ECRESULT IsAdminOverUserObject(unsigned int ulUserObjectId);
	virtual ECRESULT IsAdminOverOwnerOfObject(unsigned int ulObjectId);
	virtual ECRESULT IsUserObjectVisible(unsigned int ulUserObjectId);
	virtual ECRESULT GetViewableCompanyIds(std::list<localobjectdetails_t> **lppObjects, unsigned int ulFlags = 0);
	virtual ECRESULT GetUserQuota(unsigned int ulUserId, quotadetails_t *lpDetails);
};

#endif
