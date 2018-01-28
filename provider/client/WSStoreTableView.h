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

#ifndef WSSTORETABLEVIEW_H
#define WSSTORETABLEVIEW_H

#include <mutex>
#include <kopano/zcdefs.h>
#include <kopano/Util.h>
#include "WSTableView.h"

using namespace KC;

class WSStoreTableView : public WSTableView {
protected:
	WSStoreTableView(ULONG type, ULONG flags, ECSESSIONID, ULONG eid_size, const ENTRYID *eid, ECMsgStore *, WSTransport *);
	virtual ~WSStoreTableView(void) = default;
public:
	static HRESULT Create(ULONG type, ULONG flags, ECSESSIONID, ULONG eid_size, const ENTRYID *eid, ECMsgStore *, WSTransport *, WSTableView **);
	virtual	HRESULT	QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	ALLOC_WRAP_FRIEND;
};

class WSTableOutGoingQueue _kc_final : public WSStoreTableView {
protected:
	WSTableOutGoingQueue(ECSESSIONID, ULONG eid_size, const ENTRYID *, ECMsgStore *, WSTransport *);

public:
	static HRESULT Create(ECSESSIONID, ULONG eid_size, const ENTRYID *, ECMsgStore *, WSTransport *, WSTableOutGoingQueue **);
	virtual	HRESULT	QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT HrOpenTable() override;
	ALLOC_WRAP_FRIEND;
};

class WSTableMultiStore _kc_final : public WSStoreTableView {
protected:
	WSTableMultiStore(ULONG flags, ECSESSIONID, ULONG eid_size, const ENTRYID *eid, ECMsgStore *, WSTransport *);
    virtual ~WSTableMultiStore();

public:
	static HRESULT Create(ULONG flags, ECSESSIONID, ULONG eid_size, const ENTRYID *eid, ECMsgStore *, WSTransport *, WSTableMultiStore **);
	virtual HRESULT HrOpenTable();
	virtual HRESULT HrSetEntryIDs(const ENTRYLIST *msglist);
private:
    struct entryList m_sEntryList;
	ALLOC_WRAP_FRIEND;
};

/* not really store tables, but the code is the same.. */
class WSTableMisc _kc_final : public WSStoreTableView {
protected:
	WSTableMisc(ULONG type, ULONG flags, ECSESSIONID, ULONG eid_size, const ENTRYID *eid, ECMsgStore *, WSTransport *);

public:
	static HRESULT Create(ULONG type, ULONG flags, ECSESSIONID, ULONG eid_size, const ENTRYID *eid, ECMsgStore *, WSTransport *, WSTableMisc **);
	virtual HRESULT HrOpenTable();

private:
	ULONG m_ulTableType;
	ALLOC_WRAP_FRIEND;
};

/**
 * MailBox table which shows all the stores
 */
class WSTableMailBox _kc_final : public WSStoreTableView {
protected:
	WSTableMailBox(ULONG ulFlags, ECSESSIONID, ECMsgStore *, WSTransport *);

public:
	static HRESULT Create(ULONG ulFlags, ECSESSIONID, ECMsgStore *, WSTransport *, WSTableMailBox **);
	ALLOC_WRAP_FRIEND;
};
#endif
