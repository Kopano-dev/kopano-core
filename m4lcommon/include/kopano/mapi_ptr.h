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
#include <kopano/mapi_ptr/mapi_object_ptr.h>
#include <kopano/mapi_ptr/mapi_rowset_ptr.h>

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

typedef mapi_object_ptr<IABContainer, IID_IABContainer> ABContainerPtr;
typedef mapi_object_ptr<IAddrBook, IID_IAddrBook> AddrBookPtr;
typedef mapi_object_ptr<IDistList, IID_IDistList> DistListPtr;
typedef mapi_object_ptr<IECSecurity, IID_IECSecurity> ECSecurityPtr;
typedef mapi_object_ptr<IECServiceAdmin, IID_IECServiceAdmin> ECServiceAdminPtr;
typedef mapi_object_ptr<IECSingleInstance, IID_IECSingleInstance> ECSingleInstancePtr;
typedef mapi_object_ptr<IExchangeManageStore, IID_IExchangeManageStore> ExchangeManageStorePtr;
typedef mapi_object_ptr<IExchangeModifyTable, IID_IExchangeModifyTable> ExchangeModifyTablePtr;
typedef mapi_object_ptr<IExchangeExportChanges, IID_IExchangeExportChanges> ExchangeExportChangesPtr;
typedef mapi_object_ptr<IMAPIAdviseSink, IID_IMAPIAdviseSink> MAPIAdviseSinkPtr;
typedef mapi_object_ptr<IMAPIContainer, IID_IMAPIContainer> MAPIContainerPtr;
typedef mapi_object_ptr<IMAPIFolder, IID_IMAPIFolder> MAPIFolderPtr;
typedef mapi_object_ptr<IMAPIProp, IID_IMAPIProp> MAPIPropPtr;
typedef mapi_object_ptr<IMAPISession, IID_IMAPISession> MAPISessionPtr;
typedef mapi_object_ptr<IMAPITable, IID_IMAPITable> MAPITablePtr;
typedef mapi_object_ptr<IMailUser, IID_IMailUser> MailUserPtr;
typedef mapi_object_ptr<IMessage, IID_IMessage> MessagePtr;
typedef mapi_object_ptr<IMsgServiceAdmin, IID_IMsgServiceAdmin> MsgServiceAdminPtr;
typedef mapi_object_ptr<IMsgStore, IID_IMsgStore> MsgStorePtr;
typedef mapi_object_ptr<IProfAdmin, IID_IProfAdmin> ProfAdminPtr;
typedef mapi_object_ptr<IProfSect, IID_IProfSect> ProfSectPtr;
typedef mapi_object_ptr<IProviderAdmin, IID_IProviderAdmin> ProviderAdminPtr;
typedef mapi_object_ptr<IUnknown, IID_IUnknown> UnknownPtr;
typedef mapi_object_ptr<IStream, IID_IStream> StreamPtr;
typedef mapi_object_ptr<IAttach, IID_IAttachment> AttachPtr;
typedef mapi_object_ptr<IMAPIGetSession, IID_IMAPIGetSession> MAPIGetSessionPtr;

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

typedef mapi_rowset_ptr<SRow> SRowSetPtr;
typedef mapi_rowset_ptr<ADRENTRY> AdrListPtr;

} /* namespace */

#endif // ndef mapi_ptr_INCLUDED
