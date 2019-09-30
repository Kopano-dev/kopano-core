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
	virtual HRESULT GetLastError(HRESULT, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT GetMemAllocRoutines(ALLOCATEBUFFER **, ALLOCATEMORE **, FREEBUFFER **) override;
	virtual HRESULT Subscribe(const NOTIFKEY *key, ULONG evt_mask, ULONG flags, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unsubscribe(unsigned int conn) override;
	virtual HRESULT Notify(const NOTIFKEY *key, ULONG nnotifs, NOTIFICATION *, ULONG *flags) override;
	virtual HRESULT ModifyStatusRow(ULONG nvals, const SPropValue *, ULONG flags) override;
	virtual HRESULT OpenProfileSection(const MAPIUID *uid, unsigned int flags, IProfSect **) override;
	virtual HRESULT RegisterPreprocessor(const MAPIUID *, const TCHAR *addrtype, const TCHAR *dllname, const char *preprocess, const char *remove_pp_info, ULONG flags) override;
	virtual HRESULT NewUID(MAPIUID *) override;
	virtual HRESULT MakeInvalid(unsigned int flags, void *obj, unsigned int refcnt, unsigned int nmeth) override;

	virtual HRESULT SpoolerYield(unsigned int flags) override;
	virtual HRESULT SpoolerNotify(unsigned int flags, void *data) override;
	virtual HRESULT CreateOneOff(const TCHAR *name, const TCHAR *addrtype, const TCHAR *addr, ULONG flags, ULONG *eid_size, ENTRYID **) override;
	virtual HRESULT SetProviderUID(const MAPIUID *, ULONG flags) override;
	virtual HRESULT CompareEntryIDs(unsigned int asize, const ENTRYID *a, unsigned int bsize, const ENTRYID *b, unsigned int cmp_flags, unsigned int *result) override;
	virtual HRESULT OpenTemplateID(ULONG tpl_size, const ENTRYID *tpl_eid, ULONG tpl_flags, IMAPIProp *propdata, const IID *intf, IMAPIProp **propnew, IMAPIProp *sibling) override;
	virtual HRESULT OpenEntry(unsigned int eid_size, const ENTRYID *eid, const IID *intf, unsigned int flags, unsigned int *obj_type, IUnknown **) override;
	virtual HRESULT GetOneOffTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT Address(unsigned int *ui_param, ADRPARM *, ADRLIST **) override;
	virtual HRESULT Details(ULONG_PTR *ui_param, DISMISSMODELESS *, void *dismiss_ctx, ULONG eid_size, const ENTRYID *, LPFNBUTTON callback, void *btn_ctx, const TCHAR *btn_text, ULONG flags) override;
	virtual HRESULT NewEntry(ULONG_PTR ui_param, ULONG flags, ULONG eid_size, const ENTRYID *eid_cont, ULONG tpl_size, const ENTRYID *tpl, ULONG *new_size, ENTRYID **new_eid) override;
	virtual HRESULT DoConfigPropsheet(ULONG_PTR ui_param, ULONG flags, const TCHAR *title, IMAPITable *disp_tbl, IMAPIProp *cfg_data, ULONG top_page) override;
	virtual HRESULT CopyMessages(const IID *src_intf, void *src_fld, const ENTRYLIST *msglist, const IID *dst_intf, void *dst_fld, ULONG_PTR ui_param, IMAPIProgress *, ULONG flags) override;
	virtual HRESULT CopyFolder(const IID *src_intf, void *src_fld, unsigned int eid_size, const ENTRYID *eid, const IID *dst_intf, void *dst_fld, const TCHAR *newname, ULONG_PTR ui_param, IMAPIProgress *, unsigned int flags) override;
	virtual HRESULT DoCopyTo(const IID *src_intf, void *src_obj, unsigned int nexcl, const IID *excl, const SPropTagArray *exclprop, unsigned int ui_param, IMAPIProgress *, const IID *dst_intf, void *dst_obj, unsigned int flags, SPropProblemArray **) override;
	virtual HRESULT DoCopyProps(const IID *src_intf, void *src_obj, const SPropTagArray *inclprop, unsigned int ui_param, IMAPIProgress *, const IID *dst_intf, void *dst_obj, unsigned int flags, SPropProblemArray **) override;
	virtual HRESULT DoProgressDialog(unsigned int ui_param, unsigned int flags, IMAPIProgress **) override;
	virtual HRESULT ReadReceipt(unsigned int flags, IMessage *rdmsg, IMessage **emptymsg) override;
	virtual HRESULT PrepareSubmit(IMessage *, unsigned int *flags) override;
	virtual HRESULT ExpandRecips(IMessage *, unsigned int *flags) override;
	virtual HRESULT UpdatePAB(unsigned int flags, IMessage *) override;
	virtual HRESULT DoSentMail(unsigned int flags, IMessage *) override;
	virtual HRESULT OpenAddressBook(const IID *intf, unsigned int flags, IAddrBook **) override;
	virtual HRESULT Preprocess(ULONG flags, ULONG eid_size, const ENTRYID *) override;
	virtual HRESULT CompleteMsg(ULONG flags, ULONG eid_size, const ENTRYID *) override;
	virtual HRESULT StoreLogoffTransports(unsigned int *flags) override;
	virtual HRESULT StatusRecips(IMessage *, const ADRLIST *recips) override;
	virtual HRESULT WrapStoreEntryID(unsigned int orig_size, const ENTRYID *orig_eid, unsigned int *wrap_size, ENTRYID **wrap_eid) override;
	virtual HRESULT ModifyProfile(unsigned int flags) override;

	virtual HRESULT IStorageFromStream(IUnknown *in, const IID *intf, unsigned int flags, IStorage **) override;
	virtual HRESULT GetSvcConfigSupportObj(unsigned int flags, IMAPISupport **) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
};


#endif
