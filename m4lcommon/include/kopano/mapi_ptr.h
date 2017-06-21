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

#ifndef mapi_ptr_INCLUDED
#define mapi_ptr_INCLUDED

#include <kopano/memory.hpp>
#include <mapix.h>
#include <mapispi.h>
#include <edkmdb.h>
#include <edkguid.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/ECGuid.h>
#include <kopano/mapiguidext.h>

namespace KC {

typedef KCHL::object_ptr<IABContainer> ABContainerPtr;
typedef KCHL::object_ptr<IAddrBook> AddrBookPtr;
typedef KCHL::object_ptr<IECServiceAdmin> ECServiceAdminPtr;
typedef KCHL::object_ptr<IExchangeManageStore> ExchangeManageStorePtr;
typedef KCHL::object_ptr<IMAPIFolder> MAPIFolderPtr;
typedef KCHL::object_ptr<IMAPIProp> MAPIPropPtr;
typedef KCHL::object_ptr<IMAPISession> MAPISessionPtr;
typedef KCHL::object_ptr<IMAPITable> MAPITablePtr;
typedef KCHL::object_ptr<IMailUser> MailUserPtr;
typedef KCHL::object_ptr<IMessage> MessagePtr;
typedef KCHL::object_ptr<IMsgStore> MsgStorePtr;
typedef KCHL::object_ptr<IStream> StreamPtr;
typedef KCHL::object_ptr<IAttach> AttachPtr;

typedef KCHL::memory_ptr<ENTRYID> EntryIdPtr;
typedef KCHL::memory_ptr<ENTRYLIST> EntryListPtr;
typedef KCHL::memory_ptr<SPropValue> SPropValuePtr;
typedef KCHL::memory_ptr<SPropTagArray> SPropTagArrayPtr;
typedef KCHL::memory_ptr<SRestriction> SRestrictionPtr;

typedef KCHL::memory_ptr<SPropValue> SPropArrayPtr;

class SRowSetPtr : public KCHL::memory_ptr<SRowSet, KCHL::rowset_delete> {
	public:
	typedef unsigned int size_type;
	SRowSetPtr(void) = default;
	SRowSetPtr(SRowSet *p) : KCHL::rowset_ptr(p) {}
	SRowSet **operator&(void) { return &~*this; }
	size_type size(void) const { return (*this)->cRows; }
	const SRow &operator[](size_t i) const { return (*this)->aRow[i]; }
	bool empty(void) const { return (*this)->cRows == 0; }
};

} /* namespace */

#endif // ndef mapi_ptr_INCLUDED
