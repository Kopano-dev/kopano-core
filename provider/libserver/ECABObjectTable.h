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

#ifndef ECAB_OBJECTTABLE_H
#define ECAB_OBJECTTABLE_H

#include <kopano/zcdefs.h>
#include <kopano/Util.h>
#include "soapH.h"
#include "ECGenericObjectTable.h"
#include "ECUserManagement.h"

struct soap;

namespace KC {

// Objectdata for abprovider
struct ECODAB {
	unsigned int	ulABId;
	unsigned int	ulABType; // MAPI_ABCONT, MAPI_DISTLIST, MAPI_MAILUSER
	unsigned int 	ulABParentId;
	unsigned int	ulABParentType; // MAPI_ABCONT, MAPI_DISTLIST, MAPI_MAILUSER
};

#define AB_FILTER_SYSTEM		0x00000001
#define AB_FILTER_ADDRESSLIST	0x00000002
#define AB_FILTER_CONTACTS		0x00000004

class ECABObjectTable : public ECGenericObjectTable {
protected:
	ECABObjectTable(ECSession *lpSession, unsigned int ulABId, unsigned int ulABType, unsigned int ulABParentId, unsigned int ulABParentType, unsigned int ulFlags, const ECLocale &locale);
	virtual ~ECABObjectTable();
public:
	static ECRESULT Create(ECSession *lpSession, unsigned int ulABId, unsigned int ulABType, unsigned int ulABParentId, unsigned int ulABParentType, unsigned int ulFlags, const ECLocale &locale, ECABObjectTable **lppTable);

	//Overrides
	virtual ECRESULT GetColumnsAll(ECListInt *props) override;
	virtual ECRESULT Load() override;
	static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, const ECObjectTableList *, const struct propTagArray *, const void *priv, struct rowSet **, bool table_data, bool table_limit);

protected:
	/* Load hierarchy objects */
	ECRESULT LoadHierarchyAddressList(unsigned int obj_id, unsigned int flags, std::list<localobjectdetails_t> **objects);
	ECRESULT LoadHierarchyCompany(unsigned int obj_id, unsigned int flags, std::list<localobjectdetails_t> **objects);
	ECRESULT LoadHierarchyContainer(unsigned int obj_id, unsigned int flags, std::list<localobjectdetails_t> **objects);

	/* Load contents objects */
	ECRESULT LoadContentsAddressList(unsigned int obj_id, unsigned int flags, std::list<localobjectdetails_t> **objects);
	ECRESULT LoadContentsCompany(unsigned int obj_id, unsigned int flags, std::list<localobjectdetails_t> **objects);
	ECRESULT LoadContentsDistlist(unsigned int obj_id, unsigned int flags, std::list<localobjectdetails_t> **objects);

private:
	virtual ECRESULT GetMVRowCount(std::list<unsigned int> &&obj_ids, std::map<unsigned int, unsigned int> &count) override;
	virtual ECRESULT ReloadTableMVData(ECObjectTableList *rows, ECListInt *mvproptags) override;

protected:
	unsigned int m_ulUserManagementFlags;
	ALLOC_WRAP_FRIEND;
};

} /* namespace */

#endif
