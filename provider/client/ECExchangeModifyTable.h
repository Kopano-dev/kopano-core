/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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

class ECExchangeModifyTable final :
    public KC::ECUnknown, public KC::IECExchangeModifyTable {
public:
	ECExchangeModifyTable(ULONG unique_tag, KC::ECMemTable *table, ECMAPIProp *parent, ULONG start_rule, ULONG flags);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT GetTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT ModifyTable(ULONG ulFlags, LPROWLIST lpMods);
	virtual HRESULT DisablePushToServer();

	/* static creates */
	static HRESULT CreateRulesTable(ECMAPIProp *lpParent, ULONG ulFlags, LPEXCHANGEMODIFYTABLE *lppObj);
	static HRESULT CreateACLTable(ECMAPIProp *lpParent, ULONG ulFlags, LPEXCHANGEMODIFYTABLE *lppObj);

private:
	static HRESULT HrSerializeTable(KC::ECMemTable *, char **serout);
	static HRESULT HrDeserializeTable(char *ser, KC::ECMemTable *, ULONG *rule_id);
	static HRESULT OpenACLS(ECMAPIProp *, ULONG flags, KC::ECMemTable *, ULONG *uniq_id);
	static HRESULT SaveACLS(ECMAPIProp *, KC::ECMemTable *);

	unsigned int m_ulUniqueId, m_ulUniqueTag, m_ulFlags;
	KC::object_ptr<ECMAPIProp> m_lpParent;
	KC::object_ptr<KC::ECMemTable> m_ecTable;
	bool m_bPushToServer = true;
};

class ECExchangeRuleAction final :
    public KC::ECUnknown, public IExchangeRuleAction {
public:
	HRESULT ActionCount(ULONG *lpcActions);
	HRESULT GetAction(ULONG ulActionNumber, LARGE_INTEGER *lpruleid, LPACTION *lppAction);
};

#endif
