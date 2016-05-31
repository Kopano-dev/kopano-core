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

#include "WSTableView.h"

class WSStoreTableView : public WSTableView
{
protected:
    WSStoreTableView(ULONG ulType, ULONG ulFlags, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, ULONG cbEntryId, LPENTRYID lpEntryId, ECMsgStore *lpMsgStore, WSTransport *lpTransport);
	virtual ~WSStoreTableView(void) {}

public:
	static HRESULT Create(ULONG ulType, ULONG ulFlags, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, ULONG cbEntryId, LPENTRYID lpEntryId, ECMsgStore *lpMsgStore, WSTransport *lpTransport, WSTableView **lppTableView);
	virtual	HRESULT	QueryInterface(REFIID refiid, void **lppInterface);

};

class WSTableOutGoingQueue : public WSStoreTableView
{
protected:
    WSTableOutGoingQueue(KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, ULONG cbEntryId, LPENTRYID lpEntryId, ECMsgStore *lpMsgStore, WSTransport *lpTransport);

public:
	static HRESULT Create(KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, ULONG cbEntryId, LPENTRYID lpEntryId, ECMsgStore *lpMsgStore, WSTransport *lpTransport, WSTableOutGoingQueue **lppTableOutGoingQueue);
	virtual	HRESULT	QueryInterface(REFIID refiid, void **lppInterface);

public:
	virtual HRESULT HrOpenTable();
};

class WSTableMultiStore : public WSStoreTableView
{
protected:
    WSTableMultiStore(ULONG ulFlags, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, ULONG cbEntryId, LPENTRYID lpEntryId, ECMsgStore *lpMsgStore, WSTransport *lpTransport);
    virtual ~WSTableMultiStore();

public:
	static HRESULT Create(ULONG ulFlags, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, ULONG cbEntryId, LPENTRYID lpEntryId, ECMsgStore *lpMsgStore, WSTransport *lpTransport, WSTableMultiStore **lppTableMultiStore);

	virtual HRESULT HrOpenTable();

	virtual HRESULT HrSetEntryIDs(LPENTRYLIST lpMsgList);
private:
    struct entryList m_sEntryList;
};

/* not really store tables, but the code is the same.. */
class WSTableMisc _kc_final : public WSStoreTableView {
protected:
	WSTableMisc(ULONG ulTableType, ULONG ulFlags, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, ULONG cbEntryId, LPENTRYID lpEntryId, ECMsgStore *lpMsgStore, WSTransport *lpTransport);

public:
	static HRESULT Create(ULONG ulTableType, ULONG ulFlags, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, ULONG cbEntryId, LPENTRYID lpEntryId, ECMsgStore *lpMsgStore, WSTransport *lpTransport, WSTableMisc **lppTableMisc);

	virtual HRESULT HrOpenTable();

private:
	ULONG m_ulTableType;
};

/**
 * MailBox table which shows all the stores
 */
class WSTableMailBox _kc_final : public WSStoreTableView {
protected:
	WSTableMailBox(ULONG ulFlags, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, ECMsgStore *lpMsgStore, WSTransport *lpTransport);

public:
	static HRESULT Create(ULONG ulFlags, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, ECMsgStore *lpMsgStore, WSTransport *lpTransport, WSTableMailBox **lppTableMisc);
};
#endif
