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

	virtual HRESULT QueryInterface(REFIID refiid, void** lppInterface);

	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT __stdcall GetTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT __stdcall ModifyTable(ULONG ulFlags, LPROWLIST lpMods);

	virtual HRESULT __stdcall DisablePushToServer();

	/* static creates */
	static HRESULT __stdcall CreateRulesTable(ECMAPIProp *lpParent, ULONG ulFlags, LPEXCHANGEMODIFYTABLE *lppObj);
	static HRESULT __stdcall CreateACLTable(ECMAPIProp *lpParent, ULONG ulFlags, LPEXCHANGEMODIFYTABLE *lppObj);

	class xExchangeModifyTable _zcp_final : public IExchangeModifyTable {
	public:
		// From IUnknown
		virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;
		virtual ULONG __stdcall AddRef(void) _zcp_override;
		virtual ULONG __stdcall Release(void) _zcp_override;

		// From IExchangeModifyTable
		virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
		virtual HRESULT __stdcall GetTable(ULONG ulFlags, LPMAPITABLE *lppTable);
		virtual HRESULT __stdcall ModifyTable(ULONG ulFlags, LPROWLIST lpMods);
	} m_xExchangeModifyTable;

	class xECExchangeModifyTable _zcp_final : public IECExchangeModifyTable {
		// From IUnknown
		virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;
		virtual ULONG __stdcall AddRef(void) _zcp_override;
		virtual ULONG __stdcall Release(void) _zcp_override;

		// From IExchangeModifyTable
		virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _zcp_override;
		virtual HRESULT __stdcall GetTable(ULONG ulFlags, LPMAPITABLE *lppTable) _zcp_override;
		virtual HRESULT __stdcall ModifyTable(ULONG ulFlags, LPROWLIST lpMods) _zcp_override;

		// From IECExchangeModifyTable
		virtual HRESULT __stdcall DisablePushToServer(void) _zcp_override;
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
	bool	m_bPushToServer;
};

class ECExchangeRuleAction _kc_final : public ECUnknown {
public:
	HRESULT __stdcall ActionCount(ULONG *lpcActions);
	HRESULT __stdcall GetAction(ULONG ulActionNumber, LARGE_INTEGER *lpruleid, LPACTION *lppAction);

	class xExchangeRuleAction _zcp_final : public IExchangeRuleAction {
	public:
		// From IUnknown
		virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;
		virtual ULONG __stdcall AddRef(void) _zcp_override;
		virtual ULONG __stdcall Release(void) _zcp_override;

		virtual HRESULT __stdcall ActionCount(ULONG *lpcActions);
		virtual HRESULT __stdcall GetAction(ULONG ulActionNumber, LARGE_INTEGER *lpruleid, LPACTION *lppAction);

	} m_xExchangeRuleAction;
};

#endif
