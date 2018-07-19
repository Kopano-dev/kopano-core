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
#include <new>
#include <kopano/platform.h>
#include "MAPINotifSink.h"
#include <kopano/Util.h>
#include <mapi.h>
#include <mapix.h>

namespace KC {

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
static HRESULT MAPICopyMem(ULONG cb, const void *lpb, void *lpBase, ULONG *lpCb,
    void **lpDest)
{
    if(lpb == NULL) {
        *lpDest = NULL;
        *lpCb = 0;
		return hrSuccess;
    }
	auto hr = KAllocCopy(lpb, cb, lpDest, lpBase);
	if (hr != hrSuccess)
		return hr;
    *lpCb = cb;
	return hrSuccess;
}

static HRESULT MAPICopyString(const char *lpSrc, void *lpBase, char **lpDst)
{
	if (lpSrc != nullptr)
		return KAllocCopy(lpSrc, strlen(lpSrc) + 1, reinterpret_cast<void **>(lpDst), lpBase);
	*lpDst = NULL;
	return hrSuccess;
}

static HRESULT MAPICopyUnicode(const wchar_t *lpSrc, void *lpBase, wchar_t **lpDst)
{
	if (lpSrc != nullptr)
		return KAllocCopy(lpSrc, (wcslen(lpSrc) + 1) * sizeof(WCHAR), reinterpret_cast<void **>(lpDst), lpBase);
        *lpDst = NULL;
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
    MAPICopyUnicode(lpSrc->lpszError, lpBase, &lpDst->lpszError);
    MAPICopyUnicode(lpSrc->lpszComponent, lpBase, &lpDst->lpszComponent);
    lpDst->ulLowLevelError = lpSrc->ulLowLevelError;
    lpDst->ulContext = lpSrc->ulContext;
    
	*lppDst = lpDst;
	return hrSuccess;
}

static HRESULT CopyNotification(const NOTIFICATION *lpSrc, void *lpBase,
    NOTIFICATION *lpDst)
{
    memset(lpDst, 0, sizeof(NOTIFICATION));

    lpDst->ulEventType = lpSrc->ulEventType;
    
    switch(lpSrc->ulEventType) {
	case fnevCriticalError: {
		auto &src = lpSrc->info.err;
		auto &dst = lpDst->info.err;
		MAPICopyMem(src.cbEntryID, src.lpEntryID, lpBase, &dst.cbEntryID, reinterpret_cast<void **>(&dst.lpEntryID));
		dst.scode = src.scode;
		dst.ulFlags = src.ulFlags;
		CopyMAPIERROR(src.lpMAPIError, lpBase, &dst.lpMAPIError);
		break;
	}
	case fnevNewMail: {
		auto &src = lpSrc->info.newmail;
		auto &dst = lpDst->info.newmail;
		MAPICopyMem(src.cbEntryID,  src.lpEntryID,  lpBase, &dst.cbEntryID,  reinterpret_cast<void **>(&dst.lpEntryID));
		MAPICopyMem(src.cbParentID, src.lpParentID, lpBase, &dst.cbParentID, reinterpret_cast<void **>(&dst.lpParentID));
		dst.ulFlags = src.ulFlags;
		if (src.ulFlags & MAPI_UNICODE)
			MAPICopyUnicode(reinterpret_cast<const wchar_t *>(src.lpszMessageClass), lpBase, reinterpret_cast<wchar_t **>(&dst.lpszMessageClass));
		else
			MAPICopyString(reinterpret_cast<const char *>(src.lpszMessageClass), lpBase, reinterpret_cast<char **>(&dst.lpszMessageClass));
		dst.ulMessageFlags = src.ulMessageFlags;
		break;
	}
    case fnevObjectCreated:
    case fnevObjectDeleted:
    case fnevObjectModified:
    case fnevObjectMoved:
    case fnevObjectCopied:
	case fnevSearchComplete: {
		auto &src = lpSrc->info.obj;
		auto &dst = lpDst->info.obj;
		dst.ulObjType = src.ulObjType;
		MAPICopyMem(src.cbEntryID,     src.lpEntryID,     lpBase, &dst.cbEntryID,     reinterpret_cast<void **>(&dst.lpEntryID));
		MAPICopyMem(src.cbParentID,    src.lpParentID,    lpBase, &dst.cbParentID,    reinterpret_cast<void **>(&dst.lpParentID));
		MAPICopyMem(src.cbOldID,       src.lpOldID,       lpBase, &dst.cbOldID,       reinterpret_cast<void **>(&dst.lpOldID));
		MAPICopyMem(src.cbOldParentID, src.lpOldParentID, lpBase, &dst.cbOldParentID, reinterpret_cast<void **>(&dst.lpOldParentID));
		if (src.lpPropTagArray != nullptr)
			MAPICopyMem(CbSPropTagArray(src.lpPropTagArray), src.lpPropTagArray, lpBase, nullptr, reinterpret_cast<void **>(&dst.lpPropTagArray));
		break;
	}
	case fnevTableModified: {
		auto &src = lpSrc->info.tab;
		auto &dst = lpDst->info.tab;
		dst.ulTableEvent = src.ulTableEvent;
		dst.hResult = src.hResult;
		auto hr = Util::HrCopyProperty(&dst.propPrior, &src.propPrior, lpBase);
		if (hr != hrSuccess)
			return hr;
		hr = Util::HrCopyProperty(&dst.propIndex, &src.propIndex, lpBase);
		if (hr != hrSuccess)
			return hr;
		hr = MAPIAllocateMore(src.row.cValues * sizeof(SPropValue), lpBase, reinterpret_cast<void **>(&dst.row.lpProps));
		if (hr != hrSuccess)
			return hr;
		hr = Util::HrCopyPropertyArray(src.row.lpProps, src.row.cValues, dst.row.lpProps, lpBase);
		if (hr != hrSuccess)
			return hr;
		dst.row.cValues = src.row.cValues;
		break;
	}
	case fnevStatusObjectModified: {
		auto &src = lpSrc->info.statobj;
		auto &dst = lpDst->info.statobj;
		MAPICopyMem(src.cbEntryID, src.lpEntryID, lpBase, &dst.cbEntryID, reinterpret_cast<void **>(&dst.lpEntryID));
		auto hr = MAPIAllocateMore(src.cValues * sizeof(SPropValue), lpBase, reinterpret_cast<void **>(&dst.lpPropVals));
		if (hr != hrSuccess)
			return hr;
		hr = Util::HrCopyPropertyArray(src.lpPropVals, src.cValues, dst.lpPropVals, lpBase);
		if (hr != hrSuccess)
			return hr;
		dst.cValues = src.cValues;
		break;
	}
	}
	return hrSuccess;
}

HRESULT MAPINotifSink::Create(MAPINotifSink **lppSink)
{
	return alloc_wrap<MAPINotifSink>().put(lppSink);
}

MAPINotifSink::~MAPINotifSink() {
    m_bExit = true;
	m_hCond.notify_all();
}

// Add a notification to the queue; Normally called as notification sink
ULONG MAPINotifSink::OnNotify(ULONG cNotifications, LPNOTIFICATION lpNotifications)
{
	ULONG rc = 0;
	memory_ptr<NOTIFICATION> lpNotif;
	ulock_normal biglock(m_hMutex);
	for (unsigned int i = 0; i < cNotifications; ++i) {
		if (MAPIAllocateBuffer(sizeof(NOTIFICATION), &~lpNotif) != hrSuccess) {
			rc = 1;
			break;
		}

		if (CopyNotification(&lpNotifications[i], lpNotif, lpNotif) == 0)
			m_lstNotifs.emplace_back(std::move(lpNotif));
	}
	biglock.unlock();
	m_hCond.notify_all();
	return rc;
}

// Get All notifications off the queue
HRESULT MAPINotifSink::GetNotifications(ULONG *lpcNotif, LPNOTIFICATION *lppNotifications, BOOL fNonBlock, ULONG timeout)
{
    ULONG cNotifs = 0;
	auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);

	ulock_normal biglock(m_hMutex);
	if (!fNonBlock) {
		while (m_lstNotifs.empty() && !m_bExit && (timeout == 0 || std::chrono::steady_clock::now() < limit))
			if (timeout == 0)
				m_hCond.wait(biglock);
			else if (m_hCond.wait_for(biglock, std::chrono::milliseconds(timeout)) == std::cv_status::timeout)
				/* ignore status, we only wanted to wait */;
	}
    
	memory_ptr<NOTIFICATION> lpNotifications;
	auto hr = MAPIAllocateBuffer(sizeof(NOTIFICATION) * m_lstNotifs.size(), &~lpNotifications);
	if (hr == hrSuccess)
		for (auto const &n : m_lstNotifs)
			if (CopyNotification(n, lpNotifications, &lpNotifications[cNotifs]) == 0)
				++cNotifs;

	m_lstNotifs.clear();
	biglock.unlock();
	*lppNotifications = lpNotifications.release();
    *lpcNotif = cNotifs;

    return hr;
}

HRESULT MAPINotifSink::QueryInterface(REFIID iid, void **lpvoid) {
	if (iid != IID_IMAPIAdviseSink)
		return MAPI_E_INTERFACE_NOT_SUPPORTED;
	AddRef();
	*lpvoid = this;
	return hrSuccess;
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

} /* namespace */
