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

struct soap;

namespace KC {

class ECConvenientDepthABObjectTable _kc_final : public ECABObjectTable {
protected:
	ECConvenientDepthABObjectTable(ECSession *lpSession, unsigned int ulABId, unsigned int ulABType, unsigned int ulABParentId, unsigned int ulABParentType, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *, unsigned int ab_id, unsigned int ab_type, unsigned int ab_parent_id, unsigned int ab_parent_type, unsigned int flags, const ECLocale &, ECABObjectTable **);
	virtual ECRESULT Load();
	static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, ECObjectTableList *, struct propTagArray *, const void *priv, struct rowSet **, bool table_data, bool table_limit);

private:
	struct CONTAINERINFO {
		unsigned int ulId;
		unsigned int ulDepth;
		std::string strPath;
	};
	std::map<unsigned int, unsigned int> m_mapDepth;
	std::map<unsigned int, std::string> m_mapPath;
};

} /* namespace */

#endif
