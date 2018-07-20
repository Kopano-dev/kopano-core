/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/**
 * @file
 * Free/busy helper functions
 *
 * @addtogroup libfreebusy
 * @{
 */

#ifndef ECFREEBUSYUTIL_H
#define ECFREEBUSYUTIL_H

#include <string>
#include "freebusy.h"
#include "ECFBBlockList.h"

namespace KC {

HRESULT GetFreeBusyMessage(IMAPISession* lpSession, IMsgStore* lpPublicStore, IMsgStore* lpUserStore, ULONG cbUserEntryID, LPENTRYID lpUserEntryID, BOOL bCreateIfNotExist, IMessage** lppMessage);
HRESULT GetFreeBusyMessageData(IMessage* lpMessage, LONG* lprtmStart, LONG* lprtmEnd, ECFBBlockList	*lpfbBlockList);
HRESULT CreateFBProp(FBStatus fbStatus, ULONG ulMonths, ULONG ulPropMonths, ULONG ulPropEvents, ECFBBlockList* lpfbBlockList, LPSPropValue* lppPropFBDataArray);
unsigned int DiffYearMonthToMonth( struct tm *tm1, struct tm *tm2);
extern HRESULT HrAddFBBlock(const OccrInfo &sOccrInfo, OccrInfo **lppsOccrInfo, ULONG *lpcValues);

} /* namespace */

#endif // ECFREEBUSYUTIL_H

/** @} */
