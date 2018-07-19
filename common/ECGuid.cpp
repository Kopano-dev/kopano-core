/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#include <kopano/platform.h>
#include <initguid.h>

/*
 * All the IID_* symbols are created here
 */

#define USES_IID_IMSProvider
#define USES_IID_IXPProvider
#define USES_IID_IABProvider
#define USES_IID_IMsgStore
#define USES_IID_IMSLogon
#define USES_IID_IXPLogon
#define USES_IID_IABLogon
#define USES_IID_IMAPIFolder
#define USES_IID_IMessage
#define USES_IID_IExchangeManageStore
#define USES_IID_IAttachment
#define USES_IID_IMAPIContainer
#define USES_IID_IMAPIProp
#define USES_IID_IMAPITable
#define USES_IID_IMAPITableData
#define USES_IID_ISequentialStream
#define USES_IID_IUnknown
#define USES_IID_IStream
#define USES_IID_IStorage
#define USES_IID_IMessageRaw

// quick linux hack
#define ECDEBUGCLIENT_USES_UIDS

//Trace info
#define USES_IID_IMAPISession
#define USES_IID_IMAPIAdviseSink
#define USES_IID_IProfSect
#define USES_IID_IMAPIStatus
#define USES_IID_IAddrBook
#define USES_IID_IMailUser
#define USES_IID_IMAPIContainer
#define USES_IID_IABContainer
#define USES_IID_IDistList
#define USES_IID_IMAPISup
#define USES_IID_IMAPITableData
#define USES_IID_IMAPISpoolerInit
#define USES_IID_IMAPISpoolerSession
#define USES_IID_ITNEF
#define USES_IID_IMAPIPropData
#define USES_IID_IMAPIControl
#define USES_IID_IProfAdmin
#define USES_IID_IMsgServiceAdmin
#define USES_IID_IMAPISpoolerService
#define USES_IID_IMAPIProgress
#define USES_IID_ISpoolerHook
#define USES_IID_IMAPIViewContext
#define USES_IID_IMAPIFormMgr
#define USES_IID_IProviderAdmin
#define USES_IID_IMAPIForm
#define USES_PS_MAPI
#define USES_PS_PUBLIC_STRINGS
#define USES_IID_IPersistMessage
#define USES_IID_IMAPIViewAdviseSink
#define USES_IID_IStreamDocfile
#define USES_IID_IMAPIFormProp
#define USES_IID_IMAPIFormContainer
#define USES_IID_IMAPIFormAdviseSink
#define USES_IID_IStreamTnef
#define USES_IID_IMAPIFormFactory
#define USES_IID_IMAPIMessageSite
#define USES_PS_ROUTING_EMAIL_ADDRESSES
#define USES_PS_ROUTING_ADDRTYPE
#define USES_PS_ROUTING_DISPLAY_NAME
#define USES_PS_ROUTING_ENTRYID
#define USES_PS_ROUTING_SEARCH_KEY
#define USES_MUID_PROFILE_INSTANCE
#define USES_IID_IMAPIFormInfo
#define USES_IID_IEnumMAPIFormProp
#define USES_IID_IExchangeModifyTable
//#endif

// mapiguidext.h
#define USES_muidStoreWrap
#define USES_PS_INTERNET_HEADERS
#define USES_PSETID_Appointment
#define USES_PSETID_Task
#define USES_PSETID_Address
#define USES_PSETID_Common
#define USES_PSETID_Log
#define USES_PSETID_Meeting
#define USES_PSETID_Sharing
#define USES_PSETID_PostRss
#define USES_PSETID_UnifiedMessaging
#define USES_PSETID_AirSync
#define USES_PSETID_Note
#define USES_PSETID_CONTACT_FOLDER_RECIPIENT
#define USES_PSETID_Kopano_CalDav	//used in caldav
#define USES_PSETID_Archive
#define USES_PSETID_CalendarAssistant
#define USES_PSETID_KC
#define USES_GUID_Dilkie
#define USES_IID_IMAPIClientShutdown
#define USES_IID_IMAPIProviderShutdown
#define USES_IID_ISharedFolderEntryId
#define USES_WAB_GUID

#define USES_IID_IPRProvider
#define USES_IID_IMAPIProfile

#define USES_PSETID_ZMT

#define USES_IID_IMAPIWrappedObject
#define USES_IID_IMAPISessionUnknown
#define USES_IID_IMAPISupportUnknown
#define USES_IID_IMsgServiceAdmin2
#define USES_IID_IAddrBookSession
#define USES_IID_CAPONE_PROF
#define USES_IID_IMAPISync
#define USES_IID_IMAPISyncProgressCallback
#define USES_IID_IMAPISecureMessage
#define USES_IID_IMAPIGetSession

// freebusy guids
#define USES_IID_IEnumFBBlock
#define USES_IID_IFreeBusyData
#define USES_IID_IFreeBusySupport
#define USES_IID_IFreeBusyUpdate
#define USES_pbGlobalProfileSectionGuid

#include <mapiguid.h>
#include <edkguid.h>
#include <kopano/ECGuid.h>
#include "freebusyguid.h"
#include <kopano/mapiguidext.h>
