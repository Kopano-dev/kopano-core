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

#ifndef ECCONVENIENTDEPTHOBJECTTABLE_H
#define ECCONVENIENTDEPTHOBJECTTABLE_H

#include <kopano/zcdefs.h>
#include "ECStoreObjectTable.h"

struct soap;

namespace KC {

class ECConvenientDepthObjectTable _kc_final : public ECStoreObjectTable {
protected:
	ECConvenientDepthObjectTable(ECSession *lpSession, unsigned int ulStoreId, GUID *lpGuid, unsigned int ulFolderId, unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale);
public:
	static ECRESULT Create(ECSession *lpSession, unsigned int ulStoreId, GUID *lpGuid, unsigned int ulFolderId, unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale, ECConvenientDepthObjectTable **lpTable);

    virtual ECRESULT Load();
	virtual ECRESULT GetComputedDepth(struct soap *soap, ECSession *lpSession, unsigned int ulObjId, struct propVal *lpProp);
private:
    unsigned int m_ulFolderId;
};

} /* namespace */

#endif
