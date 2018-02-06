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

#ifndef EC_EXCHANGE_MODIFY_TABLE_H
#define EC_EXCHANGE_MODIFY_TABLE_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <kopano/ECMemTable.h>
#include <kopano/memory.hpp>
#include <mapidefs.h>
#include <edkmdb.h>
#include <kopano/IECInterfaces.hpp>

using namespace KC;

class ECExchangeModifyTable _kc_final :
    public ECUnknown, public IECExchangeModifyTable {
public:
	ECExchangeModifyTable(ULONG ulUniqueTag, ECMemTable *table, ECMAPIProp *lpParent, ULONG ulStartRuleId, ULONG ulFlags);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT GetTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT ModifyTable(ULONG ulFlags, LPROWLIST lpMods);
	virtual HRESULT DisablePushToServer();

	/* static creates */
	static HRESULT CreateRulesTable(ECMAPIProp *lpParent, ULONG ulFlags, LPEXCHANGEMODIFYTABLE *lppObj);
	static HRESULT CreateACLTable(ECMAPIProp *lpParent, ULONG ulFlags, LPEXCHANGEMODIFYTABLE *lppObj);

private:
	static HRESULT HrSerializeTable(ECMemTable *lpTable, char **lppSerialized);
	static HRESULT HrDeserializeTable(char *lpSerialized, ECMemTable *lpTable, ULONG *ulRuleId);

	static HRESULT OpenACLS(ECMAPIProp *lpecMapiProp, ULONG ulFlags, ECMemTable *lpTable, ULONG *lpulUniqueID);
	static HRESULT SaveACLS(ECMAPIProp *lpecMapiProp, ECMemTable *lpTable);

	ULONG	m_ulUniqueId;
	ULONG	m_ulUniqueTag;
	ULONG	m_ulFlags;
	KC::object_ptr<ECMAPIProp> m_lpParent;
	KC::object_ptr<ECMemTable> m_ecTable;
	bool m_bPushToServer = true;
};

class ECExchangeRuleAction _kc_final :
    public ECUnknown, public IExchangeRuleAction {
public:
	HRESULT ActionCount(ULONG *lpcActions);
	HRESULT GetAction(ULONG ulActionNumber, LARGE_INTEGER *lpruleid, LPACTION *lppAction);
};

#endif
