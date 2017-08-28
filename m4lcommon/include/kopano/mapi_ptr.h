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

#include <kopano/IECServiceAdmin.h>
#include <kopano/IECSecurity.h>
#include <kopano/IECSingleInstance.h>
#include <kopano/ECGuid.h>
#include <kopano/mapiguidext.h>

namespace KC {

typedef KCHL::object_ptr<IABContainer, IID_IABContainer> ABContainerPtr;
typedef KCHL::object_ptr<IAddrBook, IID_IAddrBook> AddrBookPtr;
typedef KCHL::object_ptr<IDistList, IID_IDistList> DistListPtr;
typedef KCHL::object_ptr<IECSecurity, IID_IECSecurity> ECSecurityPtr;
typedef KCHL::object_ptr<IECServiceAdmin, IID_IECServiceAdmin> ECServiceAdminPtr;
typedef KCHL::object_ptr<IECSingleInstance, IID_IECSingleInstance> ECSingleInstancePtr;
typedef KCHL::object_ptr<IExchangeManageStore, IID_IExchangeManageStore> ExchangeManageStorePtr;
typedef KCHL::object_ptr<IExchangeModifyTable, IID_IExchangeModifyTable> ExchangeModifyTablePtr;
typedef KCHL::object_ptr<IExchangeExportChanges, IID_IExchangeExportChanges> ExchangeExportChangesPtr;
typedef KCHL::object_ptr<IMAPIAdviseSink, IID_IMAPIAdviseSink> MAPIAdviseSinkPtr;
typedef KCHL::object_ptr<IMAPIContainer, IID_IMAPIContainer> MAPIContainerPtr;
typedef KCHL::object_ptr<IMAPIFolder, IID_IMAPIFolder> MAPIFolderPtr;
typedef KCHL::object_ptr<IMAPIProp, IID_IMAPIProp> MAPIPropPtr;
typedef KCHL::object_ptr<IMAPISession, IID_IMAPISession> MAPISessionPtr;
typedef KCHL::object_ptr<IMAPITable, IID_IMAPITable> MAPITablePtr;
typedef KCHL::object_ptr<IMailUser, IID_IMailUser> MailUserPtr;
typedef KCHL::object_ptr<IMessage, IID_IMessage> MessagePtr;
typedef KCHL::object_ptr<IMsgServiceAdmin, IID_IMsgServiceAdmin> MsgServiceAdminPtr;
typedef KCHL::object_ptr<IMsgStore, IID_IMsgStore> MsgStorePtr;
typedef KCHL::object_ptr<IProfAdmin, IID_IProfAdmin> ProfAdminPtr;
typedef KCHL::object_ptr<IProfSect, IID_IProfSect> ProfSectPtr;
typedef KCHL::object_ptr<IProviderAdmin, IID_IProviderAdmin> ProviderAdminPtr;
typedef KCHL::object_ptr<IUnknown, IID_IUnknown> UnknownPtr;
typedef KCHL::object_ptr<IStream, IID_IStream> StreamPtr;
typedef KCHL::object_ptr<IAttach, IID_IAttachment> AttachPtr;
typedef KCHL::object_ptr<IMAPIGetSession, IID_IMAPIGetSession> MAPIGetSessionPtr;

typedef KCHL::memory_ptr<ECPERMISSION> ECPermissionPtr;
typedef KCHL::memory_ptr<ENTRYID> EntryIdPtr;
typedef KCHL::memory_ptr<ENTRYLIST> EntryListPtr;
typedef KCHL::memory_ptr<MAPIERROR> MAPIErrorPtr;
typedef KCHL::memory_ptr<ROWLIST> RowListPtr;
typedef KCHL::memory_ptr<SPropProblemArray> SPropProblemArrayPtr;
typedef KCHL::memory_ptr<SPropValue> SPropValuePtr;
typedef KCHL::memory_ptr<SPropTagArray> SPropTagArrayPtr;
typedef KCHL::memory_ptr<SRestriction> SRestrictionPtr;
typedef KCHL::memory_ptr<SRow> SRowPtr;
typedef KCHL::memory_ptr<SSortOrderSet> SSortOrderSetPtr;
typedef KCHL::memory_ptr<char> StringPtr;
typedef KCHL::memory_ptr<WCHAR> WStringPtr;
typedef KCHL::memory_ptr<FlagList> FlagListPtr;
typedef KCHL::memory_ptr<SBinary> SBinaryPtr;
typedef KCHL::memory_ptr<BYTE> BytePtr;
typedef KCHL::memory_ptr<MAPINAMEID> MAPINameIdPtr;

typedef KCHL::memory_ptr<ECPERMISSION> ECPermissionArrayPtr;
typedef KCHL::memory_ptr<SPropValue> SPropArrayPtr;

class SRowSetPtr : public KCHL::memory_ptr<SRowSet, KCHL::rowset_delete> {
	public:
	typedef unsigned int size_type;
	SRowSetPtr(void) = default;
	SRowSetPtr(SRowSet *p) : KCHL::rowset_ptr(p) {}
	SRowSet **operator&(void) { return &~*this; }
	size_type size(void) const { return (*this)->cRows; }
	const SRow &operator[](size_t i) const { return (*this)->aRow[i]; }
	bool empty(void) const { return (*this) == nullptr || (*this)->cRows == 0; }
};

} /* namespace */

#endif // ndef mapi_ptr_INCLUDED
