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
#include <mapidefs.h>
#include <edkmdb.h>
#include "IECExchangeModifyTable.h"

class ECExchangeModifyTable _kc_final : public ECUnknown {
public:
	ECExchangeModifyTable(ULONG ulUniqueTag, ECMemTable *table, ECMAPIProp *lpParent, ULONG ulStartRuleId, ULONG ulFlags);
	virtual ~ECExchangeModifyTable();
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT __stdcall GetTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT __stdcall ModifyTable(ULONG ulFlags, LPROWLIST lpMods);

	virtual HRESULT __stdcall DisablePushToServer();

	/* static creates */
	static HRESULT __stdcall CreateRulesTable(ECMAPIProp *lpParent, ULONG ulFlags, LPEXCHANGEMODIFYTABLE *lppObj);
	static HRESULT __stdcall CreateACLTable(ECMAPIProp *lpParent, ULONG ulFlags, LPEXCHANGEMODIFYTABLE *lppObj);

	class xExchangeModifyTable _kc_final : public IExchangeModifyTable {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IExchangeModifyTable.hpp>
	} m_xExchangeModifyTable;

	class xECExchangeModifyTable _kc_final :
	    public IECExchangeModifyTable {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IExchangeModifyTable.hpp>
		// <kopano/xclsfrag/IECExchangeModifyTable.hpp>
		virtual HRESULT __stdcall DisablePushToServer(void) _kc_override;
	} m_xECExchangeModifyTable;

private:
	static HRESULT HrSerializeTable(ECMemTable *lpTable, char **lppSerialized);
	static HRESULT HrDeserializeTable(char *lpSerialized, ECMemTable *lpTable, ULONG *ulRuleId);

	static HRESULT OpenACLS(ECMAPIProp *lpecMapiProp, ULONG ulFlags, ECMemTable *lpTable, ULONG *lpulUniqueID);
	static HRESULT SaveACLS(ECMAPIProp *lpecMapiProp, ECMemTable *lpTable);

	ULONG	m_ulUniqueId;
	ULONG	m_ulUniqueTag;
	ULONG	m_ulFlags;
	ECMAPIProp *m_lpParent;
	ECMemTable *m_ecTable;
	bool m_bPushToServer = true;
};

class ECExchangeRuleAction _kc_final : public ECUnknown {
public:
	HRESULT __stdcall ActionCount(ULONG *lpcActions);
	HRESULT __stdcall GetAction(ULONG ulActionNumber, LARGE_INTEGER *lpruleid, LPACTION *lppAction);

	class xExchangeRuleAction _kc_final : public IExchangeRuleAction {
		#include <kopano/xclsfrag/IUnknown.hpp>
		// <kopano/xclsfrag/IExchangeRuleAction.hpp>
		virtual HRESULT __stdcall ActionCount(ULONG *lpcActions) _kc_override;
		virtual HRESULT __stdcall GetAction(ULONG ulActionNumber, LARGE_INTEGER *lpruleid, LPACTION *lppAction) _kc_override;
	} m_xExchangeRuleAction;
};

#endif
