/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef __M4L_MAPISPI_IMPL_H
#define __M4L_MAPISPI_IMPL_H

#include <kopano/memory.hpp>
#include <map>
#include <memory>
#include <mutex>
#include "m4l.common.h"
#include "m4l.mapisvc.h"
#include <mapispi.h>
#include <mapix.h>
#include <edkmdb.h>

struct M4LSUPPORTADVISE {
	M4LSUPPORTADVISE(NOTIFKEY *k, ULONG m, ULONG f, IMAPIAdviseSink *s) :
		lpKey(k), ulEventMask(m), ulFlags(f), lpAdviseSink(s)
	{}

	LPNOTIFKEY lpKey;
	ULONG ulEventMask;
	ULONG ulFlags;
	LPMAPIADVISESINK lpAdviseSink;
};

typedef std::map<ULONG, M4LSUPPORTADVISE> M4LSUPPORTADVISES;

class M4LMAPIGetSession : public M4LUnknown, public IMAPIGetSession {
private:
	KC::object_ptr<IMAPISession> session;

public:
	M4LMAPIGetSession(LPMAPISESSION new_session);

	// IMAPIGetSession
	virtual HRESULT GetMAPISession(IUnknown **ses) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
};

class M4LMAPISupport : public M4LUnknown, public IMAPISupport {
private:
	LPMAPISESSION		session;
	std::unique_ptr<MAPIUID> lpsProviderUID;
	SVCService*			service;
	std::mutex m_advises_mutex;
	M4LSUPPORTADVISES	m_advises;
	ULONG m_connections = 0;

public:
	M4LMAPISupport(LPMAPISESSION new_session, LPMAPIUID sProviderUID, SVCService* lpService);
	virtual ~M4LMAPISupport();
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT GetMemAllocRoutines(LPALLOCATEBUFFER *lpAllocateBuffer, LPALLOCATEMORE *lpAllocateMore, LPFREEBUFFER *lpFreeBuffer);
	virtual HRESULT Subscribe(const NOTIFKEY *key, ULONG evt_mask, ULONG flags, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unsubscribe(ULONG ulConnection);
	virtual HRESULT Notify(const NOTIFKEY *key, ULONG nnotifs, NOTIFICATION *, ULONG *flags) override;
	virtual HRESULT ModifyStatusRow(ULONG nvals, const SPropValue *, ULONG flags) override;
	virtual HRESULT OpenProfileSection(const MAPIUID *uid, ULONG flags, IProfSect **);
	virtual HRESULT RegisterPreprocessor(const MAPIUID *, const TCHAR *addrtype, const TCHAR *dllname, const char *preprocess, const char *remove_pp_info, ULONG flags) override;
	virtual HRESULT NewUID(LPMAPIUID lpMuid);
	virtual HRESULT MakeInvalid(ULONG ulFlags, LPVOID lpObject, ULONG ulRefCount, ULONG cMethods);

	virtual HRESULT SpoolerYield(ULONG ulFlags);
	virtual HRESULT SpoolerNotify(ULONG ulFlags, LPVOID lpvData);
	virtual HRESULT CreateOneOff(const TCHAR *name, const TCHAR *addrtype, const TCHAR *addr, ULONG flags, ULONG *eid_size, ENTRYID **) override;
	virtual HRESULT SetProviderUID(const MAPIUID *, ULONG flags) override;
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result);
	virtual HRESULT OpenTemplateID(ULONG tpl_size, const ENTRYID *tpl_eid, ULONG tpl_flags, IMAPIProp *propdata, const IID *intf, IMAPIProp **propnew, IMAPIProp *sibling) override;
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	virtual HRESULT GetOneOffTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT Address(ULONG *lpulUIParam, LPADRPARM lpAdrParms, LPADRLIST *lppAdrList);
	virtual HRESULT Details(ULONG_PTR *ui_param, DISMISSMODELESS *, void *dismiss_ctx, ULONG eid_size, const ENTRYID *, LPFNBUTTON callback, void *btn_ctx, const TCHAR *btn_text, ULONG flags) override;
	virtual HRESULT NewEntry(ULONG_PTR ui_param, ULONG flags, ULONG eid_size, const ENTRYID *eid_cont, ULONG tpl_size, const ENTRYID *tpl, ULONG *new_size, ENTRYID **new_eid) override;
	virtual HRESULT DoConfigPropsheet(ULONG_PTR ui_param, ULONG flags, const TCHAR *title, IMAPITable *disp_tbl, IMAPIProp *cfg_data, ULONG top_page) override;
	virtual HRESULT CopyMessages(const IID *src_intf, void *src_fld, const ENTRYLIST *msglist, const IID *dst_intf, void *dst_fld, ULONG_PTR ui_param, IMAPIProgress *, ULONG flags) override;
	virtual HRESULT CopyFolder(const IID *src_intf, void *src_fld, ULONG eid_size, const ENTRYID *eid, const IID *dst_intf, void *dst_fld, const TCHAR *newname, ULONG_PTR ui_param, IMAPIProgress *, ULONG flags);
	virtual HRESULT DoCopyTo(LPCIID lpSrcInterface, LPVOID lpSrcObj, ULONG ciidExclude, LPCIID rgiidExclude, const SPropTagArray *lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpDestInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
	virtual HRESULT DoCopyProps(LPCIID lpSrcInterface, LPVOID lpSrcObj, const SPropTagArray *lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpDestInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
	virtual HRESULT DoProgressDialog(ULONG ulUIParam, ULONG ulFlags, LPMAPIPROGRESS *lppProgress);
	virtual HRESULT ReadReceipt(ULONG ulFlags, LPMESSAGE lpReadMessage, LPMESSAGE *lppEmptyMessage);
	virtual HRESULT PrepareSubmit(LPMESSAGE lpMessage, ULONG *lpulFlags);
	virtual HRESULT ExpandRecips(LPMESSAGE lpMessage, ULONG *lpulFlags);
	virtual HRESULT UpdatePAB(ULONG ulFlags, LPMESSAGE lpMessage);
	virtual HRESULT DoSentMail(ULONG ulFlags, LPMESSAGE lpMessage);
	virtual HRESULT OpenAddressBook(LPCIID lpInterface, ULONG ulFlags, LPADRBOOK *lppAdrBook);
	virtual HRESULT Preprocess(ULONG flags, ULONG eid_size, const ENTRYID *) override;
	virtual HRESULT CompleteMsg(ULONG flags, ULONG eid_size, const ENTRYID *) override;
	virtual HRESULT StoreLogoffTransports(ULONG *lpulFlags);
	virtual HRESULT StatusRecips(IMessage *, const ADRLIST *recips) override;
	virtual HRESULT WrapStoreEntryID(ULONG cbOrigEntry, const ENTRYID *lpOrigEntry, ULONG *lpcbWrappedEntry, ENTRYID **lppWrappedEntry);
	virtual HRESULT ModifyProfile(ULONG ulFlags);

	virtual HRESULT IStorageFromStream(LPUNKNOWN lpUnkIn, LPCIID lpInterface, ULONG ulFlags, LPSTORAGE *lppStorageOut);
	virtual HRESULT GetSvcConfigSupportObj(ULONG ulFlags, LPMAPISUP *lppSvcSupport);
	virtual HRESULT QueryInterface(const IID &, void **) override;
};


#endif
