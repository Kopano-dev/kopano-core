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

#ifndef ECCONVENIENTDEPTHABOBJECTTABLE_H
#define ECCONVENIENTDEPTHABOBJECTTABLE_H

#include <kopano/zcdefs.h>
#include "ECABObjectTable.h"

class ECConvenientDepthABObjectTable _kc_final : public ECABObjectTable {
protected:
	ECConvenientDepthABObjectTable(ECSession *lpSession, unsigned int ulABId, unsigned int ulABType, unsigned int ulABParentId, unsigned int ulABParentType, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *lpSession, unsigned int ulABId, unsigned int ulABType, unsigned int ulABParentId, unsigned int ulABParentType, unsigned int ulFlags, const ECLocale &locale, ECConvenientDepthABObjectTable **lppTable);

	virtual ECRESULT Load();

	static ECRESULT QueryRowData(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSession, ECObjectTableList* lpRowList, struct propTagArray *lpsPropTagArray, void* lpObjectData, struct rowSet **lppRowSet, bool bTableData,bool bTableLimit);

private:
	struct CONTAINERINFO {
		unsigned int ulId;
		unsigned int ulDepth;
		std::string strPath;
	};
	std::map<unsigned int, unsigned int> m_mapDepth;
	std::map<unsigned int, std::string> m_mapPath;
};

#endif
