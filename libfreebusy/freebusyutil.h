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

HRESULT GetFreeBusyFolder(IMsgStore* lpPublicStore, IMAPIFolder** lppFreeBusyFolder);
HRESULT GetFreeBusyMessage(IMAPISession* lpSession, IMsgStore* lpPublicStore, IMsgStore* lpUserStore, ULONG cbUserEntryID, LPENTRYID lpUserEntryID, BOOL bCreateIfNotExist, IMessage** lppMessage);
HRESULT ParseFBEvents(FBStatus fbSts, LPSPropValue lpMonth, LPSPropValue lpEvent, ECFBBlockList* lpfbBlockList);
HRESULT GetFreeBusyMessageData(IMessage* lpMessage, LONG* lprtmStart, LONG* lprtmEnd, ECFBBlockList	*lpfbBlockList);
HRESULT CreateFBProp(FBStatus fbStatus, ULONG ulMonths, ULONG ulPropMonths, ULONG ulPropEvents, ECFBBlockList* lpfbBlockList, LPSPropValue* lppPropFBDataArray);

BOOL leapyear(short year);
HRESULT getMaxMonthMinutes(short year, short month, short* minutes);
unsigned int DiffYearMonthToMonth( struct tm *tm1, struct tm *tm2);

std::string GetDebugFBBlock(LONG celt, FBBlock_1* pblk);
std::string GetFbStatus(FBStatus &fbstatus);

HRESULT HrCopyFBBlockSet(OccrInfo *lpDest, OccrInfo *lpSrc, ULONG ulcValues);
HRESULT	HrAddFBBlock(OccrInfo sOccrInfo, OccrInfo **lppsOccrInfo, ULONG *lpcValues);

#endif // ECFREEBUSYUTIL_H

/** @} */
