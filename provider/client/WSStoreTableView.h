/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/Util.h>
#include <kopano/zcdefs.h>
#include "WSTableView.h"

class WSStoreTableView : public WSTableView {
protected:
	WSStoreTableView(ULONG type, ULONG flags, KC::ECSESSIONID, ULONG eid_size, const ENTRYID *eid, ECMsgStore *, WSTransport *);
	virtual ~WSStoreTableView(void) = default;
public:
	static HRESULT Create(ULONG type, ULONG flags, KC::ECSESSIONID, ULONG eid_size, const ENTRYID *eid, ECMsgStore *, WSTransport *, WSTableView **);
	ALLOC_WRAP_FRIEND;
};

class WSTableOutGoingQueue final : public WSStoreTableView {
protected:
	WSTableOutGoingQueue(KC::ECSESSIONID, ULONG eid_size, const ENTRYID *, ECMsgStore *, WSTransport *);

public:
	static HRESULT Create(KC::ECSESSIONID, ULONG eid_size, const ENTRYID *, ECMsgStore *, WSTransport *, WSTableOutGoingQueue **);
	virtual	HRESULT	QueryInterface(const IID &, void **) override;
	virtual HRESULT HrOpenTable() override;
	ALLOC_WRAP_FRIEND;
};

/* not really store tables, but the code is the same.. */
class WSTableMisc KC_FINAL_OPG : public WSStoreTableView {
protected:
	WSTableMisc(ULONG type, ULONG flags, KC::ECSESSIONID, ULONG eid_size, const ENTRYID *eid, ECMsgStore *, WSTransport *);

public:
	static HRESULT Create(ULONG type, ULONG flags, KC::ECSESSIONID, ULONG eid_size, const ENTRYID *eid, ECMsgStore *, WSTransport *, WSTableMisc **);
	virtual HRESULT HrOpenTable();

private:
	ULONG m_ulTableType;
	ALLOC_WRAP_FRIEND;
};

/**
 * MailBox table which shows all the stores
 */
class WSTableMailBox KC_FINAL_OPG : public WSStoreTableView {
protected:
	WSTableMailBox(ULONG ulFlags, KC::ECSESSIONID, ECMsgStore *, WSTransport *);

public:
	static HRESULT Create(ULONG ulFlags, KC::ECSESSIONID, ECMsgStore *, WSTransport *, WSTableMailBox **);
	ALLOC_WRAP_FRIEND;
};
