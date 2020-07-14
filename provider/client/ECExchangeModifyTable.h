/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <kopano/ECMemTable.h>
#include <kopano/memory.hpp>
#include <mapidefs.h>
#include <edkmdb.h>
#include <kopano/IECInterfaces.hpp>

class ECExchangeModifyTable KC_FINAL_OPG :
    public KC::ECUnknown, public KC::IECExchangeModifyTable {
public:
	ECExchangeModifyTable(ULONG unique_tag, KC::ECMemTable *table, ECMAPIProp *parent, ULONG start_rule, ULONG flags);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	virtual HRESULT GetLastError(HRESULT, unsigned int, MAPIERROR **) override;
	virtual HRESULT GetTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT ModifyTable(unsigned int flags, ROWLIST *mods) override;
	virtual HRESULT DisablePushToServer() override;

	/* static creates */
	static HRESULT CreateRulesTable(ECMAPIProp *lpParent, ULONG ulFlags, IExchangeModifyTable **);
	static HRESULT CreateACLTable(ECMAPIProp *lpParent, ULONG ulFlags, IExchangeModifyTable **);

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
