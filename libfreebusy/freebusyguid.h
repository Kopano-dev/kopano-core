/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/**
 * @file
 * Free/busy Interface identifiers
 *
 * @addtogroup libfreebusy
 * @{
 */

#ifndef FREEBUSYGUID_INCLUDED
#define FREEBUSYGUID_INCLUDED

// Interface identifiers for the Free/Busy API

#if !defined(INITGUID) || defined(USES_IID_IEnumFBBlock)
/** IEnumFBBlock interface identifier. GUID: {00067064-0000-0000-C000-000000000046} */
DEFINE_GUID(IID_IEnumFBBlock, 0x00067064, 0x0, 0x0, 0xc0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46);
#endif
#if !defined(INITGUID) || defined(USES_IID_IFreeBusyUpdate)
/** IFreeBusyUpdate interface identifier. GUID: {00067065-0000-0000-C000-000000000046} */
DEFINE_GUID(IID_IFreeBusyUpdate, 0x00067065, 0x0, 0x0, 0xc0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46);
#endif
#if !defined(INITGUID) || defined(USES_IID_IFreeBusyData)
/** IFreeBusyData interface identifier. GUID: {00067066-0000-0000-C000-000000000046} */
DEFINE_GUID(IID_IFreeBusyData, 0x00067066, 0x0, 0x0, 0xc0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46);
#endif
#if !defined(INITGUID) || defined(USES_IID_IFreeBusySupport)
/** IFreeBusySupport interface identifier. GUID: {00067067-0000-0000-C000-000000000046} */
DEFINE_GUID(IID_IFreeBusySupport, 0x00067067, 0x0, 0x0, 0xc0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46);
#endif

// Unknown Freebusy guids
// DEFINE_GUID(IID_I??, 0x00067068, 0x0, 0x0, 0xc0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46);//{00067068-0000-0000-C000-000000000046}
// DEFINE_GUID(IID_I??, 0x00067069, 0x0, 0x0, 0xc0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46);//{00067069-0000-0000-C000-000000000046}

#endif // FREEBUSYGUID_INCLUDED

/** @} */
