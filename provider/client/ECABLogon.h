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

#ifndef ECABLOGON_H
#define ECABLOGON_H

#include <kopano/zcdefs.h>
#include <mapispi.h>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include "ECNotifyClient.h"
#include "WSTransport.h"

using namespace KC;
class WSTransport;

class ECABLogon _kc_final : public ECUnknown, public IABLogon {
protected:
	ECABLogon(IMAPISupport *, WSTransport *, ULONG profile_flags, const GUID *);
	virtual ~ECABLogon();

public:
	static  HRESULT Create(IMAPISupport *, WSTransport *, ULONG profile_flags, const GUID *, ECABLogon **);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT Logoff(ULONG ulFlags);
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result);
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unadvise(ULONG ulConnection);
	virtual HRESULT OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPMAPISTATUS * lppMAPIStatus);
	virtual HRESULT OpenTemplateID(ULONG tpl_size, const ENTRYID *tpl_eid, ULONG tpl_flags, IMAPIProp *propdata, const IID *intf, IMAPIProp **propnew, IMAPIProp *sibling) override;
	virtual HRESULT GetOneOffTable(ULONG ulFlags, LPMAPITABLE * lppTable);
	virtual HRESULT PrepareRecips(ULONG ulFlags, const SPropTagArray *lpPropTagArray, LPADRLIST lpRecipList);

	KC::object_ptr<IMAPISupport> m_lpMAPISup;
	KC::object_ptr<WSTransport> m_lpTransport;
	KC::object_ptr<ECNotifyClient> m_lpNotifyClient;
	GUID m_guid, m_ABPGuid;
	ALLOC_WRAP_FRIEND;
};

#endif // #ifndef ECABLOGON
