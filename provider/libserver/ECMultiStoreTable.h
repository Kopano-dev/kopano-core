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

#ifndef EC_MULTISTORE_TABLE_H
#define EC_MULTISTORE_TABLE_H

#include <kopano/zcdefs.h>
#include "ECStoreObjectTable.h"

class _kc_export_dycast ECMultiStoreTable _kc_final :
    public ECStoreObjectTable {
	protected:
	_kc_hidden ECMultiStoreTable(ECSession *, unsigned int obj_type, unsigned int flags, const ECLocale &);

public:
	_kc_hidden static ECRESULT Create(ECSession *, unsigned int obj_type, unsigned int flags, const ECLocale &, ECMultiStoreTable **ret);
	_kc_hidden virtual ECRESULT SetEntryIDs(ECListInt *obj_list);
	_kc_hidden virtual ECRESULT Load(void);
private:
    std::list<unsigned int> m_lstObjects;
    
};

#endif
