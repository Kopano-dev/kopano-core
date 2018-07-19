/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef MAPIGUIDEXT_H
#define MAPIGUIDEXT_H

//Place undefined mapi guids here


// MAPI Wrapped Message Store Provider identifier
#if !defined(INITGUID) || defined(USES_muidStoreWrap)
DEFINE_GUID(muidStoreWrap, 0x10BBA138, 0xE505,0x1A10,0xA1,0xBB,0x08,0x00,0x2B,0x2A,0x56,0xC2);
#endif

/*  The name of the set of internet headers  */
#if !defined(INITGUID) || defined(USES_PS_INTERNET_HEADERS)
DEFINE_OLEGUID(PS_INTERNET_HEADERS,   0x00020386, 0, 0);
#endif

/*  The name of the set of appointments  */
#if !defined(INITGUID) || defined(USES_PSETID_Appointment)
DEFINE_OLEGUID(PSETID_Appointment,   0x00062002, 0, 0);
#endif

/*  The name of the set of tasks  */
#if !defined(INITGUID) || defined(USES_PSETID_Task)
DEFINE_OLEGUID(PSETID_Task,   0x00062003, 0, 0);
#endif

/*  The name of the set of addresses  */
#if !defined(INITGUID) || defined(USES_PSETID_Address)
DEFINE_OLEGUID(PSETID_Address,   0x00062004, 0, 0);
#endif

/*  The name of the set of commons  */
#if !defined(INITGUID) || defined(USES_PSETID_Common)
DEFINE_OLEGUID(PSETID_Common,   0x00062008, 0, 0);
#endif

/*  The name of the set of logs  */
#if !defined(INITGUID) || defined(USES_PSETID_Log)
DEFINE_OLEGUID(PSETID_Log,   0x0006200A, 0, 0);
#endif

/*  The name of the set of sticky notes  */
#if !defined(INITGUID) || defined(USES_PSETID_Note)
DEFINE_OLEGUID(PSETID_Note,   0x0006200E, 0, 0);
#endif

/*  The name of the set of Sharing  */
#if !defined(INITGUID) || defined(USES_PSETID_Sharing)
DEFINE_OLEGUID(PSETID_Sharing,   0x00062040, 0, 0);
#endif

/*  The name of the set of RSS feeds  */
#if !defined(INITGUID) || defined(USES_PSETID_PostRss)
DEFINE_OLEGUID(PSETID_PostRss,   0x00062041, 0, 0);
#endif

/*  The name of the set of unified messaging  */
#if !defined(INITGUID) || defined(USES_PSETID_UnifiedMessaging)
DEFINE_GUID (PSETID_UnifiedMessaging, 0x4442858E, 0xA9E3, 0x4E80,0xB9, 0x00, 0x31, 0x7A, 0x21, 0x0C, 0xC1, 0x5B);
#endif

/*  The name of the set of meetings  */
#if !defined(INITGUID) || defined(USES_PSETID_Meeting)
DEFINE_GUID (PSETID_Meeting, 0x6ED8DA90, 0x450B, 0x101B,0x98, 0xDA, 0x00, 0xAA, 0x00, 0x3F, 0x13, 0x05);
#endif

/*  The name of the set of Syncs  */
#if !defined(INITGUID) || defined(USES_PSETID_AirSync)
DEFINE_GUID (PSETID_AirSync, 0x71035549, 0x0739, 0x4DCB, 0x91, 0x63, 0x00, 0xF0, 0x58, 0x0D, 0xBB, 0xDF);
#endif

/* Unnamed GUID which is used by Outlook in recipients of a message
   which are from your (a?) contact folder */
#if !defined(INITGUID) || defined(USES_PSETID_CONTACT_FOLDER_RECIPIENT)
DEFINE_GUID (PSETID_CONTACT_FOLDER_RECIPIENT, 0x0AAA42FE, 0xC718, 0x101A, 0xE8, 0x85, 0x0B, 0x65, 0x1C, 0x24, 0x00, 0x00);
#endif

#if !defined(INITGUID) || defined(USES_PSETID_Kopano_CalDav)
// {77536087-CB81-4dc9-9958-EA4C51BE3486}
DEFINE_GUID(PSETID_Kopano_CalDav, 0x77536087, 0xcb81, 0x4dc9, 0x99, 0x58, 0xea, 0x4c, 0x51, 0xbe, 0x34, 0x86);
#endif

#if !defined(INITGUID) || defined(USES_PSETID_Archive)
DEFINE_GUID(PSETID_Archive, 0x72e98ebc, 0x57d2, 0x4ab5, 0xb0, 0xaa, 0xd5, 0x0a, 0x7b, 0x53, 0x1c, 0xb9);
#endif

/*  The entry id of the original migrated message */
#if !defined(INITGUID) || defined(USES_PSETID_ZMT)
// {8ACDBF85-4738-4dc4-94A9-D489A83E5C41}
DEFINE_GUID(PSETID_ZMT,0x8acdbf85, 0x4738, 0x4dc4, 0x94, 0xa9, 0xd4, 0x89, 0xa8, 0x3e, 0x5c, 0x41);
#endif

#if !defined(INITGUID) || defined(USES_PSETID_CalendarAssistant)
DEFINE_GUID(PSETID_CalendarAssistant,0x11000E07, 0xB51B, 0x40D6, 0xAF, 0x21, 0xCA,0xA8, 0x5E, 0xDA, 0xB1, 0xD0);
#endif

#if !defined(INITGUID) || defined(USES_PSETID_KC)
// {63aed8c8-4049-4b75-bc88-96df9d723f2f}
DEFINE_GUID(PSETID_KC, 0x63aed8c8, 0x4049, 0x4b75, 0xbc, 0x88, 0x96, 0xdf, 0x9d, 0x72, 0x3f, 0x2f);
#endif

// http://support.microsoft.com/kb/312900
// Profile section, where the security settings are stored
#if !defined(INITGUID) || defined(USES_GUID_Dilkie)
DEFINE_GUID(GUID_Dilkie, 0x53BC2EC0, 0xD953, 0x11CD, 0x97, 0x52, 0x00, 0xAA, 0x00, 0x4A, 0xE4, 0x0E);
#endif

#if !defined(INITGUID) || defined(USES_IID_IMAPIClientShutdown)
DEFINE_OLEGUID(IID_IMAPIClientShutdown, 0x00020397, 0, 0);
#endif

#if !defined(INITGUID) || defined(USES_IID_IMAPIProviderShutdown)
DEFINE_OLEGUID(IID_IMAPIProviderShutdown, 0x00020398, 0, 0);
#endif

// Used for debug
#if !defined(INITGUID) || defined(USES_IID_ISharedFolderEntryId)
DEFINE_GUID(IID_ISharedFolderEntryId, 0xE9C1D90B, 0x6430, 0xCE42, 0xA5, 0x6E, 0xDB, 0x2C, 0x1E, 0x4A, 0xB6, 0xE6);
#endif


#if !defined(INITGUID) || defined(USES_IID_IPRProvider)
DEFINE_OLEGUID(IID_IPRProvider, 0x000203F6, 0, 0);
#endif

#if !defined(INITGUID) || defined(USES_IID_IMAPIProfile)
DEFINE_OLEGUID(IID_IMAPIProfile, 0x000203F7, 0, 0);
#endif

#if !defined(INITGUID) || defined(USES_IID_CAPONE_PROF)
// Capone profile section
// {00020D0A-0000-0000-C000-000000000046}
DEFINE_OLEGUID(IID_CAPONE_PROF, 0x00020d0a, 0, 0);
#endif

#if !defined(INITGUID) || defined(USES_IID_IMAPIWrappedObject)
// MapiWrapped object guid, Unknown what the real name is.
// THis is used to wrap the provider object and the internal mapi objects. Unknown how this is working.
//{02813F9B-751C-4F59-8C7D-D93842DB05E0}
DEFINE_GUID(IID_IMAPIWrappedObject,0x02813F9B,0x751C,0x4F59, 0x8C, 0x7D,0xD9,0x38,0x42,0xDB,0x05,0xE0);
#endif

#if !defined(INITGUID) || defined(USES_IID_IMAPISessionUnknown)
// Get Mapi session
DEFINE_OLEGUID(IID_IMAPISessionUnknown,0x00020399,0,0);
#endif

#if !defined(INITGUID) || defined(USES_IID_IMAPISupportUnknown)
DEFINE_OLEGUID(IID_IMAPISupportUnknown, 0x00020331,0,0);
#endif


#if !defined(INITGUID) || defined(USES_IID_IMAPISync)
DEFINE_GUID(IID_IMAPISync, 0x5024a385, 0x2d44, 0x486a,  0x81, 0xa8, 0x8f, 0xe, 0xcb, 0x60, 0x71, 0xdd);
#endif

#if !defined(INITGUID) || defined(USES_IID_IMAPISyncProgressCallback)
DEFINE_GUID(IID_IMAPISyncProgressCallback, 0x5024a386, 0x2d44, 0x486a,  0x81, 0xa8, 0x8f, 0xe, 0xcb, 0x60, 0x71, 0xdd);
#endif

#if !defined(INITGUID) || defined(USES_IID_IMAPISecureMessage)
DEFINE_GUID(IID_IMAPISecureMessage, 0x253cc320, 0xeab6, 0x11d0, 0x82, 0x22, 0, 0x60, 0x97, 0x93, 0x87, 0xea);
#endif

#if !defined(INITGUID) || defined(USES_IID_IMAPIGetSession)
DEFINE_GUID(IID_IMAPIGetSession, 0x614ab435, 0x491d, 0x4f5b, 0xa8, 0xb4, 0x60, 0xeb, 0x3, 0x10, 0x30, 0xc6);
#endif

#if !defined(INITGUID) || defined(USES_IID_IAddrBookSession)
// Looks like the MAPI's internal IAddrBook object
DEFINE_OLEGUID(IID_IAddrBookSession,0x000203A1,0,0);
#endif

// Contact Address Book Wrapped Entry ID, found in PidLidDistributionListMembers
#if !defined(INITGUID) || defined(USES_WAB_GUID)
DEFINE_GUID(WAB_GUID, 0xD3AD91C0, 0x9D51, 0x11CF, 0xA4, 0xA9, 0x00, 0xAA, 0x00, 0x47, 0xFA, 0xA4);
#endif

#endif
