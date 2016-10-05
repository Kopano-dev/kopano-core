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

#include <chrono>
#include <kopano/platform.h>
#include <kopano/lockhelper.hpp>
#include "MAPINotifSink.h"

#include <kopano/Util.h>

#include <mapi.h>
#include <mapix.h>

/**
 * This is a special advisesink so that we can do notifications in Perl. What it does
 * is simply catch all notifications and store them. The notifications can then be requested
 * by calling GetNotifications() with or with fNonBlock. The only difference is that with the fNonBlock
 * flag function is non-blocking.
 *
 * This basically makes notifications single-threaded, requiring a Perl process to request the
 * notifications. However, the user can choose to use GetNotifications() from within a PERL thread,
 * which makes it possible to do real threaded notifications in Perl, without touching data in Perl
 * from a thread that it did not create.
 *
 * The reason we need to do this is that Perl has an interpreter-per-thread architecture, so if we
 * were to start fiddling in Perl structures from our own thread, this would very probably cause
 * segmentation faults.
 */
static HRESULT MAPICopyMem(ULONG cb, void *lpb, void *lpBase, ULONG *lpCb,
    void **lpDest)
{
    if(lpb == NULL) {
        *lpDest = NULL;
        *lpCb = 0;
		return hrSuccess;
    }
    
	HRESULT hr = MAPIAllocateMore(cb, lpBase, lpDest);
	if (hr != hrSuccess)
		return hr;
        
    memcpy(*lpDest, lpb, cb);
    *lpCb = cb;
	return hrSuccess;
}

HRESULT MAPICopyString(char *lpSrc, void *lpBase, char **lpDst)
{
    if(lpSrc == NULL) {
        *lpDst = NULL;
		return hrSuccess;
	}
    
	HRESULT hr = MAPIAllocateMore(strlen(lpSrc) + 1, lpBase,
	             reinterpret_cast<void **>(lpDst));
	if (hr != hrSuccess)
		return hr;
	strcpy(*lpDst, lpSrc);
	return hrSuccess;
}

HRESULT MAPICopyUnicode(WCHAR *lpSrc, void *lpBase, WCHAR **lpDst)
{
    if(lpSrc == NULL) {
        *lpDst = NULL;
		return hrSuccess;
    }
    
	HRESULT hr = MAPIAllocateMore(wcslen(lpSrc) * sizeof(WCHAR) +
	             sizeof(WCHAR), lpBase, reinterpret_cast<void **>(lpDst));
	if (hr != hrSuccess)
		return hr;
	wcscpy(*lpDst, lpSrc);
	return hrSuccess;
}

static HRESULT CopyMAPIERROR(const MAPIERROR *lpSrc, void *lpBase,
    MAPIERROR **lppDst)
{
    MAPIERROR *lpDst = NULL;
    
	HRESULT hr = MAPIAllocateMore(sizeof(MAPIERROR), lpBase,
	             reinterpret_cast<void **>(&lpDst));
	if (hr != hrSuccess)
		return hr;

    lpDst->ulVersion = lpSrc->ulVersion;
	// @todo we don't know if the strings were create with unicode anymore
#ifdef UNICODE
    MAPICopyUnicode(lpSrc->lpszError, lpBase, &lpDst->lpszError);
    MAPICopyUnicode(lpSrc->lpszComponent, lpBase, &lpDst->lpszComponent);
#else
    MAPICopyString(lpSrc->lpszError, lpBase, &lpDst->lpszError);
    MAPICopyString(lpSrc->lpszComponent, lpBase, &lpDst->lpszComponent);
#endif
    lpDst->ulLowLevelError = lpSrc->ulLowLevelError;
    lpDst->ulContext = lpSrc->ulContext;
    
	*lppDst = lpDst;
	return hrSuccess;
}

static HRESULT CopyNotification(const NOTIFICATION *lpSrc, void *lpBase,
    NOTIFICATION *lpDst)
{
    HRESULT hr;

    memset(lpDst, 0, sizeof(NOTIFICATION));

    lpDst->ulEventType = lpSrc->ulEventType;
    
    switch(lpSrc->ulEventType) {
        case fnevCriticalError:
            MAPICopyMem(lpSrc->info.err.cbEntryID, 		lpSrc->info.err.lpEntryID, 		lpBase, &lpDst->info.err.cbEntryID, 	(void**)&lpDst->info.err.lpEntryID);
            
            lpDst->info.err.scode =  lpSrc->info.err.scode;
            lpDst->info.err.ulFlags = lpSrc->info.err.ulFlags;
            
            CopyMAPIERROR(lpSrc->info.err.lpMAPIError, lpBase, &lpDst->info.err.lpMAPIError);
            
            break;
        case fnevNewMail:
            MAPICopyMem(lpSrc->info.newmail.cbEntryID, 		lpSrc->info.newmail.lpEntryID, 		lpBase, &lpDst->info.newmail.cbEntryID, 	(void**)&lpDst->info.newmail.lpEntryID);
            MAPICopyMem(lpSrc->info.newmail.cbParentID, 	lpSrc->info.newmail.lpParentID, 	lpBase, &lpDst->info.newmail.cbParentID, 	(void**)&lpDst->info.newmail.lpParentID);
            
            lpDst->info.newmail.ulFlags = lpSrc->info.newmail.ulFlags;
			if (lpSrc->info.newmail.ulFlags&MAPI_UNICODE)
				MAPICopyUnicode((LPWSTR)lpSrc->info.newmail.lpszMessageClass, lpBase, (LPWSTR*)&lpDst->info.newmail.lpszMessageClass);
			else
				MAPICopyString((char*)lpSrc->info.newmail.lpszMessageClass, lpBase, (char**)&lpDst->info.newmail.lpszMessageClass);
			
            lpDst->info.newmail.ulMessageFlags = lpSrc->info.newmail.ulMessageFlags;
            
            break;
        case fnevObjectCreated:
        case fnevObjectDeleted:
        case fnevObjectModified:
        case fnevObjectMoved:
        case fnevObjectCopied:
        case fnevSearchComplete:
            lpDst->info.obj.ulObjType = lpSrc->info.obj.ulObjType;
            
            MAPICopyMem(lpSrc->info.obj.cbEntryID, 		lpSrc->info.obj.lpEntryID, 		lpBase, &lpDst->info.obj.cbEntryID, 	(void**)&lpDst->info.obj.lpEntryID);
            MAPICopyMem(lpSrc->info.obj.cbParentID, 	lpSrc->info.obj.lpParentID, 	lpBase, &lpDst->info.obj.cbParentID, 	(void**)&lpDst->info.obj.lpParentID);
            MAPICopyMem(lpSrc->info.obj.cbOldID, 		lpSrc->info.obj.lpOldID, 		lpBase, &lpDst->info.obj.cbOldID, 		(void**)&lpDst->info.obj.lpOldID);
            MAPICopyMem(lpSrc->info.obj.cbOldParentID, 	lpSrc->info.obj.lpOldParentID, 	lpBase, &lpDst->info.obj.cbOldParentID, (void**)&lpDst->info.obj.lpOldParentID);

            if(lpSrc->info.obj.lpPropTagArray)           
                MAPICopyMem(CbSPropTagArray(lpSrc->info.obj.lpPropTagArray), lpSrc->info.obj.lpPropTagArray, lpBase, NULL, (void**)&lpDst->info.obj.lpPropTagArray);
            break;
        case fnevTableModified:
            lpDst->info.tab.ulTableEvent = lpSrc->info.tab.ulTableEvent;
            lpDst->info.tab.hResult = lpSrc->info.tab.hResult;
            hr = Util::HrCopyProperty(&lpDst->info.tab.propPrior, &lpSrc->info.tab.propPrior, lpBase);
            if (hr != hrSuccess)
			return hr;
            hr = Util::HrCopyProperty(&lpDst->info.tab.propIndex, &lpSrc->info.tab.propIndex, lpBase);
            if (hr != hrSuccess)
			return hr;
            if ((hr = MAPIAllocateMore(lpSrc->info.tab.row.cValues * sizeof(SPropValue), lpBase, (void **)&lpDst->info.tab.row.lpProps)) != hrSuccess)
			return hr;
            hr = Util::HrCopyPropertyArray(lpSrc->info.tab.row.lpProps, lpSrc->info.tab.row.cValues, lpDst->info.tab.row.lpProps, lpBase);
            if (hr != hrSuccess)
			return hr;
            lpDst->info.tab.row.cValues = lpSrc->info.tab.row.cValues;
            break;
        case fnevStatusObjectModified:
            MAPICopyMem(lpSrc->info.statobj.cbEntryID, 		lpSrc->info.statobj.lpEntryID, 		lpBase, &lpDst->info.statobj.cbEntryID, 	(void**)&lpDst->info.statobj.lpEntryID);
            if ((hr = MAPIAllocateMore(lpSrc->info.statobj.cValues * sizeof(SPropValue), lpBase, (void **)&lpDst->info.statobj.lpPropVals)) != hrSuccess)
			return hr;
            hr = Util::HrCopyPropertyArray(lpSrc->info.statobj.lpPropVals, lpSrc->info.statobj.cValues, lpDst->info.statobj.lpPropVals, lpBase);
            if (hr != hrSuccess)
			return hr;
            lpDst->info.statobj.cValues = lpSrc->info.statobj.cValues;
            break;
	}
	return hrSuccess;
}

HRESULT MAPINotifSink::Create(MAPINotifSink **lppSink)
{
    MAPINotifSink *lpSink = new MAPINotifSink();

    lpSink->AddRef();
    
    *lppSink = lpSink;
    
    return hrSuccess;
}

MAPINotifSink::MAPINotifSink() {
    m_bExit = false;
    m_cRef = 0;
}

MAPINotifSink::~MAPINotifSink() {
    m_bExit = true;
	m_hCond.notify_all();
	for (auto n : m_lstNotifs)
		MAPIFreeBuffer(n);
	m_lstNotifs.clear();
}

// Add a notification to the queue; Normally called as notification sink
ULONG MAPINotifSink::OnNotify(ULONG cNotifications, LPNOTIFICATION lpNotifications)
{
	ULONG rc = 0;
	LPNOTIFICATION lpNotif;
	ulock_normal biglock(m_hMutex);
	for (unsigned int i = 0; i < cNotifications; ++i) {
		if (MAPIAllocateBuffer(sizeof(NOTIFICATION), (LPVOID*)&lpNotif) != hrSuccess) {
			rc = 1;
			break;
		}

		if (CopyNotification(&lpNotifications[i], lpNotif, lpNotif) == 0)
			m_lstNotifs.push_back(lpNotif);
	}
	biglock.unlock();
	m_hCond.notify_all();
	return rc;
}

// Get All notifications off the queue
HRESULT MAPINotifSink::GetNotifications(ULONG *lpcNotif, LPNOTIFICATION *lppNotifications, BOOL fNonBlock, ULONG timeout)
{
    HRESULT hr = hrSuccess;
    ULONG cNotifs = 0;
    struct timespec t;
    
    double now = GetTimeOfDay();
    now += (float)timeout / 1000;

    t.tv_sec = now;
    t.tv_nsec = (now-t.tv_sec) * 1000000000.0;

	ulock_normal biglock(m_hMutex);
	if (!fNonBlock) {
		while(m_lstNotifs.empty() && !m_bExit && (timeout == 0 || GetTimeOfDay() < now)) {
			if (timeout > 0)
				m_hCond.wait_for(biglock, std::chrono::milliseconds(timeout));
			else
				m_hCond.wait(biglock);
		}
	}
    
	LPNOTIFICATION lpNotifications = NULL;
    
	if ((hr = MAPIAllocateBuffer(sizeof(NOTIFICATION) * m_lstNotifs.size(), (void **) &lpNotifications)) == hrSuccess) {
		for (auto n : m_lstNotifs) {
			if (CopyNotification(n, lpNotifications, &lpNotifications[cNotifs]) == 0)
				++cNotifs;
			MAPIFreeBuffer(n);
		}
	}

	m_lstNotifs.clear();
	biglock.unlock();
    *lppNotifications = lpNotifications;
    *lpcNotif = cNotifs;

    return hr;
}

HRESULT MAPINotifSink::QueryInterface(REFIID iid, void **lpvoid) {
	if (iid == IID_IMAPIAdviseSink) {
	    AddRef();
		*lpvoid = (LPVOID)this;
		return hrSuccess;
	}
    return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

ULONG MAPINotifSink::AddRef()
{
    return ++m_cRef;
}

ULONG MAPINotifSink::Release()
{
    ULONG ref = --m_cRef;
    
    if(ref == 0)
        delete this;
        
    return ref;
}
