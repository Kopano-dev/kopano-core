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

#include <kopano/platform.h>

#include <mapi.h>
#include <mapidefs.h>
#include <mapix.h>
#include <mapiutil.h>
#include <kopano/ECRestriction.h>
#include <kopano/memory.hpp>
#include "freebusyutil.h"
#include <kopano/stringutil.h>

#include "freebusytags.h"
#include <kopano/mapiext.h>
#include <edkmdb.h>

using namespace KCHL;

namespace KC {

/**
 * Defines a free/busy event block. This is one block of a array of FBEvent blocks.
 *
 * The event blocks are stored properties PR_FREEBUSY_*
 * TODO: rename sfbEvent to FBEvent
 *
 * @rtmStart:	The start time is the number of minutes between 00:00 UTC of
 * 		the first day of the month and the tsart time of the event
 * 		in UTC.
 * @rtmEnd:	The end time is the number of minutes between 00:00 UTC of the
 * 		first day of the month and the end time of the event in UTC.
 */
struct sfbEvent {
	short rtmStart, rtmEnd;
};

#define FB_DATE(yearmonth,daytime)	((static_cast<ULONG>(static_cast<unsigned short>(yearmonth)) << 16) | static_cast<ULONG>(static_cast<unsigned short>(daytime)))
#define FB_YEARMONTH(year, month)	(((static_cast<unsigned short>(year) << 4) & 0xFFF0) | static_cast<unsigned short>(month))
#define FB_YEAR(yearmonth)		(static_cast<unsigned short>(yearmonth) >> 4)
#define FB_MONTH(yearmonth)		(static_cast<unsigned short>(yearmonth) & 0x000F)

static bool leapyear(short year)
{
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); 
}

static HRESULT getMaxMonthMinutes(short year, short month, short *minutes)
{
	short days = 0;

	if(month < 0 || month >11 || year < 1601)
		return MAPI_E_INVALID_PARAMETER;

	switch(month+1)
	{
	case 1:
	case 3:
	case 5:
	case 7:
	case 8:
	case 10:
	case 12:
		days = 31;
		break;
	case 4:
	case 6:
	case 9:
	case 11:
		days = 30;
		break;
	case 2:
		days = 28;
		if(leapyear(year))
			++days;
		break;
	}

	*minutes = days * (24*60);
	return hrSuccess;
}

static HRESULT GetFreeBusyFolder(IMsgStore *lpPublicStore,
    IMAPIFolder **lppFreeBusyFolder)
{
	HRESULT			hr = S_OK;
	ULONG			cValuesFreeBusy = 0;
	memory_ptr<SPropValue> lpPropArrayFreeBusy;
	object_ptr<IMAPIFolder> lpMapiFolder;
	ULONG			ulObjType = 0;
	static constexpr const SizedSPropTagArray(1, sPropsFreeBusy) =
		{1, {PR_FREE_BUSY_FOR_LOCAL_SITE_ENTRYID}};
	enum eFreeBusyPos{ FBPOS_FREE_BUSY_FOR_LOCAL_SITE_ENTRYID};

	// Get freebusy properies
	hr = lpPublicStore->GetProps(sPropsFreeBusy, 0, &cValuesFreeBusy, &~lpPropArrayFreeBusy);
	if (FAILED(hr))
		return hr;
	if(lpPropArrayFreeBusy[FBPOS_FREE_BUSY_FOR_LOCAL_SITE_ENTRYID].ulPropTag != PR_FREE_BUSY_FOR_LOCAL_SITE_ENTRYID)
		return MAPI_E_NOT_FOUND;

	hr = lpPublicStore->OpenEntry(
	     lpPropArrayFreeBusy[FBPOS_FREE_BUSY_FOR_LOCAL_SITE_ENTRYID].Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(lpPropArrayFreeBusy[FBPOS_FREE_BUSY_FOR_LOCAL_SITE_ENTRYID].Value.bin.lpb),
	     &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpMapiFolder);
	if(hr != hrSuccess)
		return hr;
	return lpMapiFolder->QueryInterface(IID_IMAPIFolder, (void**)lppFreeBusyFolder);
}

HRESULT GetFreeBusyMessage(IMAPISession* lpSession, IMsgStore* lpPublicStore, IMsgStore* lpUserStore, ULONG cbUserEntryID, LPENTRYID lpUserEntryID, BOOL bCreateIfNotExist, IMessage** lppMessage)
{
	HRESULT			hr = S_OK;
	object_ptr<IMAPIFolder> lpFreeBusyFolder;
	object_ptr<IMAPITable> lpMapiTable;
	SPropValue		sPropUser;
	rowset_ptr lpRows;
	ULONG			ulObjType = 0;
	object_ptr<IMessage> lpMessage;
	ULONG			ulMvItems = 0;
	ULONG			i;
	memory_ptr<SPropValue> lpPropfbEntryids;
	memory_ptr<SPropValue> lpPropfbEntryidsNew, lpPropFBMessage;
	memory_ptr<SPropValue> lpPropName, lpPropEmail;
	ULONG			cbInBoxEntry = 0;
	memory_ptr<ENTRYID> lpInboxEntry;
	static constexpr const SizedSPropTagArray(1, sPropsFreebusyTable) = {1, {PR_ENTRYID}};
	enum eFreeBusyTablePos{ FBPOS_ENTRYID};

	if(lpSession == NULL || lpPublicStore == NULL || lppMessage == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if(cbUserEntryID == 0 || lpUserEntryID == nullptr)
		return MAPI_E_INVALID_ENTRYID;

	// GetFreeBusyFolder  
	hr = GetFreeBusyFolder(lpPublicStore, &~lpFreeBusyFolder);
 	if(hr != hrSuccess)
		return hr;
	hr = lpFreeBusyFolder->GetContentsTable(0, &~lpMapiTable);
 	if(hr != hrSuccess)
		return hr;

	sPropUser.ulPropTag = PR_ADDRESS_BOOK_ENTRYID;
	sPropUser.Value.bin.cb = cbUserEntryID;
	sPropUser.Value.bin.lpb = (LPBYTE)lpUserEntryID;
	hr = ECPropertyRestriction(RELOP_EQ, PR_ADDRESS_BOOK_ENTRYID, &sPropUser, ECRestriction::Cheap)
	     .RestrictTable(lpMapiTable, TBL_BATCH);
	if(hr != hrSuccess)
		return hr;
	hr = lpMapiTable->SetColumns(sPropsFreebusyTable, TBL_BATCH);
	if(hr != hrSuccess)
		return hr;
	hr = lpMapiTable->QueryRows(1, 0, &~lpRows);
 	if(hr != hrSuccess)
		return hr;

	if(lpRows->cRows == 1 && lpRows->aRow[0].lpProps[FBPOS_ENTRYID].ulPropTag == PR_ENTRYID)
	{
		// Open freebusy data
		hr = lpPublicStore->OpenEntry(lpRows->aRow[0].lpProps[FBPOS_ENTRYID].Value.bin.cb,
		     reinterpret_cast<ENTRYID *>(lpRows->aRow[0].lpProps[FBPOS_ENTRYID].Value.bin.lpb),
		     &IID_IMessage, MAPI_MODIFY, &ulObjType, &~lpMessage);
		if(hr != hrSuccess)
			return hr;
	}
	else if (bCreateIfNotExist == TRUE)
	{
		//Create new freebusymessage
		hr = lpFreeBusyFolder->CreateMessage(nullptr, 0, &~lpMessage);
		if(hr != hrSuccess)
			return hr;

		//Set the user entry id 
		hr = lpMessage->SetProps(1, &sPropUser, NULL);
		if(hr != hrSuccess)
			return hr;

		// Set the accountname in properties PR_DISPLAY_NAME and PR_SUBJECT
		object_ptr<IAddrBook> lpAdrBook;
		hr = lpSession->OpenAddressBook(0, NULL, AB_NO_DIALOG, &~lpAdrBook);
 		if(hr != hrSuccess)
			return hr;
		object_ptr<IMailUser> lpMailUser;
		hr = lpAdrBook->OpenEntry(cbUserEntryID, lpUserEntryID, &IID_IMailUser, MAPI_BEST_ACCESS, &ulObjType, &~lpMailUser);
 		if(hr != hrSuccess)
			return hr;
		hr = HrGetOneProp(lpMailUser, PR_ACCOUNT, &~lpPropName);
		if(hr != hrSuccess)
			return hr;
		hr = HrGetOneProp(lpMailUser, PR_EMAIL_ADDRESS, &~lpPropEmail);
		if(hr != hrSuccess)
			return hr;

		//Set the displayname with accountname 
		lpPropName->ulPropTag = PR_DISPLAY_NAME;
		hr = lpMessage->SetProps(1, lpPropName, NULL);
		if(hr != hrSuccess)
			return hr;

		//Set the subject with accountname 
		lpPropName->ulPropTag = PR_SUBJECT;
		hr = lpMessage->SetProps(1, lpPropName, NULL);
		if(hr != hrSuccess)
			return hr;

		//Set the PR_FREEBUSY_EMA with the email address
		lpPropEmail->ulPropTag = PR_FREEBUSY_EMAIL_ADDRESS;
		hr = lpMessage->SetProps(1, lpPropEmail, NULL);
		if(hr != hrSuccess)
			return hr;

		//Save message
		hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
		if(hr != hrSuccess)
			return hr;

		// Update the user freebusy entryid array

		if (lpUserStore) {
			// Get entryid
			hr = HrGetOneProp(lpMessage, PR_ENTRYID, &~lpPropFBMessage);
			if(hr != hrSuccess)
				return hr;

			//Open root folder
			object_ptr<IMAPIFolder> lpFolder;
			hr = lpUserStore->OpenEntry(0, NULL, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpFolder);
			if(hr != hrSuccess)
				return hr;

			ulMvItems = 4;
			// Get current freebusy entryid array
			if (HrGetOneProp(lpFolder, PR_FREEBUSY_ENTRYIDS, &~lpPropfbEntryids) == hrSuccess)
				ulMvItems = (lpPropfbEntryids->Value.MVbin.cValues>ulMvItems)?lpPropfbEntryids->Value.MVbin.cValues:ulMvItems;

			hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpPropfbEntryidsNew);
			if(hr != hrSuccess)
				return hr;

			lpPropfbEntryidsNew->Value.MVbin.cValues = ulMvItems;

			hr = MAPIAllocateMore(sizeof(SBinary)*lpPropfbEntryidsNew->Value.MVbin.cValues, lpPropfbEntryidsNew, (void**)&lpPropfbEntryidsNew->Value.MVbin.lpbin);
			if(hr != hrSuccess)
				return hr;

			memset(lpPropfbEntryidsNew->Value.MVbin.lpbin, 0, sizeof(SBinary)*lpPropfbEntryidsNew->Value.MVbin.cValues);

			// move the old entryids to the new array
			if(lpPropfbEntryids) {
				for (i = 0; i < lpPropfbEntryids->Value.MVbin.cValues; ++i) {
					lpPropfbEntryidsNew->Value.MVbin.lpbin[i].cb = lpPropfbEntryids->Value.MVbin.lpbin[i].cb;
					lpPropfbEntryidsNew->Value.MVbin.lpbin[i].lpb = lpPropfbEntryids->Value.MVbin.lpbin[i].lpb; //cheap copy
				}
			}
			// Add the new entryid on position 3
			lpPropfbEntryidsNew->Value.MVbin.lpbin[2].cb = lpPropFBMessage->Value.bin.cb;
			lpPropfbEntryidsNew->Value.MVbin.lpbin[2].lpb = lpPropFBMessage->Value.bin.lpb;

			lpPropfbEntryidsNew->ulPropTag = PR_FREEBUSY_ENTRYIDS;

			hr = lpFolder->SetProps(1, lpPropfbEntryidsNew, NULL);
			if(hr != hrSuccess)
				return hr;

			hr = lpFolder->SaveChanges(KEEP_OPEN_READONLY);
			if(hr != hrSuccess)
				return hr;

			// Get the inbox
			hr = lpUserStore->GetReceiveFolder(nullptr, 0, &cbInBoxEntry, &~lpInboxEntry, nullptr);
			if(hr != hrSuccess)
				return hr;

			// Open the inbox
			hr = lpUserStore->OpenEntry(cbInBoxEntry, lpInboxEntry, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpFolder);
			if(hr != hrSuccess)
				return hr;
			hr = lpFolder->SetProps(1, lpPropfbEntryidsNew, NULL);
			if(hr != hrSuccess)
				return hr;
			hr = lpFolder->SaveChanges(KEEP_OPEN_READONLY);
			if(hr != hrSuccess)
				return hr;
		}

	}
	else
	{
		return MAPI_E_NOT_FOUND;
	}

	return lpMessage->QueryInterface(IID_IMessage,
	       reinterpret_cast<void **>(lppMessage));
}

static HRESULT ParseFBEvents(FBStatus fbSts, LPSPropValue lpMonth,
    LPSPropValue lpEvent, ECFBBlockList *lpfbBlockList)
{
	ULONG		cEvents;
	sfbEvent*	lpfbEvents = NULL;
	struct tm	tmTmp;
	time_t		tmUnix;
	LONG		rtmStart;
	LONG		rtmEnd;
	bool		bMerge;
	FBBlock_1	fbBlock;

	// Check varibales
	if(lpEvent == NULL || lpMonth == NULL || lpfbBlockList == NULL ||
		lpEvent->Value.MVbin.cValues != lpMonth->Value.MVl.cValues)
		return MAPI_E_INVALID_PARAMETER;

	memset(&fbBlock, 0, sizeof(fbBlock));

	for (ULONG i = 0; i < lpEvent->Value.MVbin.cValues; ++i) {
		if(lpEvent->Value.MVbin.lpbin[i].cb == 0) // notting to do
			continue;

		cEvents = lpEvent->Value.MVbin.lpbin[i].cb / sizeof(sfbEvent);
		lpfbEvents = (sfbEvent*)lpEvent->Value.MVbin.lpbin[i].lpb;

		for (ULONG j = 0; j < cEvents; ++j) {
			memset(&tmTmp, 0, sizeof(struct tm));
			tmTmp.tm_year = FB_YEAR(lpMonth->Value.MVl.lpl[i]) - 1900;
			tmTmp.tm_mon = FB_MONTH(lpMonth->Value.MVl.lpl[i])-1;
			tmTmp.tm_mday = 1;
			tmTmp.tm_min = (int)(unsigned short)lpfbEvents[j].rtmStart;
			tmTmp.tm_isdst = -1;

			tmUnix = timegm(&tmTmp);
			UnixTimeToRTime(tmUnix, &rtmStart);

			memset(&tmTmp, 0, sizeof(struct tm));
			tmTmp.tm_year = FB_YEAR(lpMonth->Value.MVl.lpl[i]) - 1900;
			tmTmp.tm_mon = FB_MONTH(lpMonth->Value.MVl.lpl[i])-1;
			tmTmp.tm_mday = 1;
			tmTmp.tm_min = (int)(unsigned short)lpfbEvents[j].rtmEnd;
			tmTmp.tm_isdst = -1;

			tmUnix = timegm(&tmTmp);
			UnixTimeToRTime(tmUnix, &rtmEnd);
			
			// Don't reset fbBlock.m_tmEnd
			bMerge = fbBlock.m_tmEnd == rtmStart;
			fbBlock.m_fbstatus = fbSts;
			fbBlock.m_tmStart = rtmStart;
			fbBlock.m_tmEnd = rtmEnd;

			if (bMerge)
				lpfbBlockList->Merge(&fbBlock);
			else
				lpfbBlockList->Add(&fbBlock);
		}
	}
	return S_OK;
}

HRESULT GetFreeBusyMessageData(IMessage* lpMessage, LONG* lprtmStart, LONG* lprtmEnd, ECFBBlockList	*lpfbBlockList)
{
	HRESULT hr = S_OK;

	ULONG			cValuesFBData = 0;
	memory_ptr<SPropValue> lpPropArrayFBData;
	static constexpr const SizedSPropTagArray(9, sPropsFreeBusyData) = {
		9,
		{
			//PR_FREEBUSY_ALL_EVENTS,
			//PR_FREEBUSY_ALL_MONTH,
			PR_FREEBUSY_START_RANGE,
			PR_FREEBUSY_END_RANGE,
			PR_FREEBUSY_BUSY_EVENTS,
			PR_FREEBUSY_BUSY_MONTHS,
			PR_FREEBUSY_OOF_EVENTS,
			PR_FREEBUSY_OOF_MONTHS,
			PR_FREEBUSY_TENTATIVE_EVENTS,
			PR_FREEBUSY_TENTATIVE_MONTHS,
			PR_FREEBUSY_NUM_MONTHS

		}
	};

	enum eFreeBusyData {FBDATA_START_RANGE, FBDATA_END_RANGE,
						FBDATA_BUSY_EVENTS, FBDATA_BUSY_MONTHS,
						FBDATA_OOF_EVENTS, FBDATA_OOF_MONTHS,
						FBDATA_TENTATIVE_EVENTS, FBDATA_TENTATIVE_MONTHS,
						FBDATA_NUM_MONTHS
					   };

	if(lpMessage == NULL || lprtmStart == NULL || lprtmEnd == NULL || lpfbBlockList == NULL)
		return MAPI_E_INVALID_PARAMETER;
	hr = lpMessage->GetProps(sPropsFreeBusyData, 0, &cValuesFBData, &~lpPropArrayFBData);
	if(FAILED(hr))
		return hr;

	// Get busy data
	if(lpPropArrayFBData[FBDATA_BUSY_EVENTS].ulPropTag == PR_FREEBUSY_BUSY_EVENTS &&
		lpPropArrayFBData[FBDATA_BUSY_MONTHS].ulPropTag == PR_FREEBUSY_BUSY_MONTHS)
	{
		hr = ParseFBEvents(fbBusy, &lpPropArrayFBData[FBDATA_BUSY_MONTHS], &lpPropArrayFBData[FBDATA_BUSY_EVENTS], lpfbBlockList);
		if(hr != hrSuccess)
			return hr;
	}

	// Get Tentative data
	if(lpPropArrayFBData[FBDATA_TENTATIVE_EVENTS].ulPropTag == PR_FREEBUSY_TENTATIVE_EVENTS &&
		lpPropArrayFBData[FBDATA_TENTATIVE_MONTHS].ulPropTag == PR_FREEBUSY_TENTATIVE_MONTHS)
	{
		hr = ParseFBEvents(fbTentative, &lpPropArrayFBData[FBDATA_TENTATIVE_MONTHS], &lpPropArrayFBData[FBDATA_TENTATIVE_EVENTS], lpfbBlockList);
		if(hr != hrSuccess)
			return hr;
	}

		// Get OutOfOffice data
	if(lpPropArrayFBData[FBDATA_OOF_EVENTS].ulPropTag == PR_FREEBUSY_OOF_EVENTS &&
		lpPropArrayFBData[FBDATA_OOF_MONTHS].ulPropTag == PR_FREEBUSY_OOF_MONTHS)
	{
		hr = ParseFBEvents(fbOutOfOffice, &lpPropArrayFBData[FBDATA_OOF_MONTHS], &lpPropArrayFBData[FBDATA_OOF_EVENTS], lpfbBlockList);
		if(hr != hrSuccess)
			return hr;
	}

	if (lpPropArrayFBData[FBDATA_START_RANGE].ulPropTag == PR_FREEBUSY_START_RANGE)
		*lprtmStart = lpPropArrayFBData[FBDATA_START_RANGE].Value.ul;
	else 
		*lprtmStart = 0;

	if (lpPropArrayFBData[FBDATA_END_RANGE].ulPropTag == PR_FREEBUSY_END_RANGE)
		*lprtmEnd = lpPropArrayFBData[FBDATA_END_RANGE].Value.ul;
	else 
		*lprtmEnd = 0;
	return hr;
}

unsigned int DiffYearMonthToMonth( struct tm *tm1, struct tm *tm2)
{
	unsigned int months = 0;

	if(tm1->tm_year == tm2->tm_year)
		months = tm2->tm_mon - tm1->tm_mon;
	else if(tm2->tm_year > tm1->tm_year && tm2->tm_mon >= tm1->tm_mon)
		months = (12 * (tm2->tm_year - tm1->tm_year)) + tm2->tm_mon - tm1->tm_mon;
	else if(tm2->tm_year > tm1->tm_year && tm2->tm_mon < tm1->tm_mon)
		months = (12 * (tm2->tm_year - tm1->tm_year -1)) + (tm2->tm_mon+1) + (11-tm1->tm_mon);
	else
		months = 0;

	return months;
}

HRESULT CreateFBProp(FBStatus fbStatus, ULONG ulMonths, ULONG ulPropMonths, ULONG ulPropEvents, ECFBBlockList* lpfbBlockList, LPSPropValue* lppPropFBDataArray)
{
	HRESULT			hr = hrSuccess;
	ULONG			ulMaxItemDataSize = 0;
	int				i = 0;
	int				ulDiffMonths = 0;
	struct tm		tmStart;
	struct tm		tmEnd;
	struct tm		tmTmp;
	int				ulLastMonth = 0;
	int				ulLastYear = 0;
	LONG			iMonth = -1;
	sfbEvent		fbEvent;
	FBBlock_1		fbBlk;
	bool			bFound;
	memory_ptr<SPropValue> lpPropFBDataArray;
	time_t			tmUnixStart = 0;
	time_t			tmUnixEnd = 0;

	//Check of propertys are mv
	if(lpfbBlockList == NULL || lppPropFBDataArray == NULL)
		return MAPI_E_INVALID_PARAMETER;

	// Set the list on the begin
	lpfbBlockList->Reset();
	ulMaxItemDataSize = (lpfbBlockList->Size() + 1 ) * sizeof(sfbEvent); // +1 block, for free/busy in two months

	/*
		First item is Months
		Second item is the Freebusy data
	*/
	hr = MAPIAllocateBuffer(2 * sizeof(SPropValue), &~lpPropFBDataArray);
	if (hr != hrSuccess)
		return hr;
	
	lpPropFBDataArray[0].Value.MVl.cValues = 0;
	lpPropFBDataArray[1].Value.MVbin.cValues = 0;

	if ((hr = MAPIAllocateMore((ulMonths+1) * sizeof(ULONG), lpPropFBDataArray, (void**)&lpPropFBDataArray[0].Value.MVl.lpl)) != hrSuccess)	 // +1 for free/busy in two months
		return hr;
	if ((hr = MAPIAllocateMore((ulMonths+1) * sizeof(SBinary), lpPropFBDataArray, (void**)&lpPropFBDataArray[1].Value.MVbin.lpbin)) != hrSuccess) // +1 for free/busy in two months
		return hr;

	//memset(&lpPropFBDataArray[1].Value.MVbin.lpbin, 0, ulArrayItems);

	lpPropFBDataArray[0].ulPropTag = ulPropMonths;
	lpPropFBDataArray[1].ulPropTag = ulPropEvents;

	iMonth = -1;

	bFound = false;

	while (lpfbBlockList->Next(&fbBlk) == hrSuccess &&
	       iMonth < static_cast<LONG>(ulMonths))
	{

		if(fbBlk.m_fbstatus == fbStatus || fbStatus == fbKopanoAllBusy)
		{
			RTimeToUnixTime(fbBlk.m_tmStart, &tmUnixStart);
			RTimeToUnixTime(fbBlk.m_tmEnd, &tmUnixEnd);

			gmtime_safe(&tmUnixStart, &tmStart);
			gmtime_safe(&tmUnixEnd, &tmEnd);
			
			if(tmStart.tm_year > ulLastYear || tmStart.tm_mon > ulLastMonth)
			{
				++iMonth;
				lpPropFBDataArray[0].Value.MVl.lpl[iMonth] =  FB_YEARMONTH((tmStart.tm_year+1900), (tmStart.tm_mon+1));
				++lpPropFBDataArray[0].Value.MVl.cValues;
				++lpPropFBDataArray[1].Value.MVbin.cValues;
				if ((hr = MAPIAllocateMore(ulMaxItemDataSize, lpPropFBDataArray, (void**)&lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].lpb)) != hrSuccess)
					return hr;
				lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].cb = 0;
				
			}

			//Different months in a block
			if(tmEnd.tm_year > tmStart.tm_year || tmEnd.tm_mon > tmStart.tm_mon)
			{
				fbEvent.rtmStart = (short)( ((tmStart.tm_mday-1)*24*60) + (tmStart.tm_hour*60) + tmStart.tm_min);
				getMaxMonthMinutes((short)tmStart.tm_year+1900, (short)tmStart.tm_mon, (short*)&fbEvent.rtmEnd);

				// Add item to struct
				memcpy(lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].lpb+lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].cb, &fbEvent, sizeof(sfbEvent));
				lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].cb += sizeof(sfbEvent);
				assert(lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].cb <= ulMaxItemDataSize);

				ulDiffMonths = DiffYearMonthToMonth(&tmStart, &tmEnd);

				tmTmp = tmStart;

				// Set the day on the begin of the month because: if mday is 31 and the next month is 30 then you get the wrong month
				tmTmp.tm_mday = 1;
				
				for (i = 1; i < ulDiffMonths && lpPropFBDataArray[0].Value.MVl.cValues < ulMonths; ++i) {
					++iMonth;
					tmTmp.tm_isdst = -1;
					++tmTmp.tm_mon;
					mktime(&tmTmp);
					

					lpPropFBDataArray[0].Value.MVl.lpl[iMonth] = FB_YEARMONTH((tmTmp.tm_year+1900), (tmTmp.tm_mon+1));
					++lpPropFBDataArray[0].Value.MVl.cValues;
					++lpPropFBDataArray[1].Value.MVbin.cValues;

					if ((hr = MAPIAllocateMore(ulMaxItemDataSize, lpPropFBDataArray, (void**)&lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].lpb)) != hrSuccess)
						return hr;
					lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].cb = 0;
				
					fbEvent.rtmStart = 0;					
					getMaxMonthMinutes((short)tmTmp.tm_year+1900, (short)tmTmp.tm_mon, (short*)&fbEvent.rtmEnd);

					// Add item to struct
					memcpy(lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].lpb+lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].cb, &fbEvent, sizeof(sfbEvent));
					lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].cb += sizeof(sfbEvent);
					assert(lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].cb <= ulMaxItemDataSize);
				}

				++iMonth;
				++tmTmp.tm_mon;
				tmTmp.tm_isdst = -1;
				mktime(&tmTmp);

				lpPropFBDataArray[0].Value.MVl.lpl[iMonth] = FB_YEARMONTH((tmTmp.tm_year+1900), (tmTmp.tm_mon+1));
				++lpPropFBDataArray[0].Value.MVl.cValues;
				++lpPropFBDataArray[1].Value.MVbin.cValues;

				if ((hr = MAPIAllocateMore(ulMaxItemDataSize, lpPropFBDataArray, (void**)&lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].lpb)) != hrSuccess)
					return hr;
				lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].cb = 0;

				fbEvent.rtmStart = 0;
				fbEvent.rtmEnd = (short)( ((tmEnd.tm_mday-1)*24*60) + (tmEnd.tm_hour*60) + tmEnd.tm_min);
			} else {
				fbEvent.rtmStart = (short)( ((tmStart.tm_mday-1)*24*60) + (tmStart.tm_hour*60) + tmStart.tm_min);
				fbEvent.rtmEnd = (short)( ((tmEnd.tm_mday-1)*24*60) + (tmEnd.tm_hour*60) + tmEnd.tm_min);
			}

			// Add item to struct
			memcpy(lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].lpb+lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].cb, &fbEvent, sizeof(sfbEvent));
			lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].cb += sizeof(sfbEvent);

			ulLastYear = tmEnd.tm_year;
			ulLastMonth = tmEnd.tm_mon;

			bFound = true;
			assert(lpPropFBDataArray[1].Value.MVbin.lpbin[iMonth].cb <= ulMaxItemDataSize);
		}
		assert(iMonth == -1 || (iMonth >= 0 && static_cast<ULONG>(iMonth) < ulMonths + 1));
		assert(lpPropFBDataArray[1].Value.MVbin.cValues <= ulMonths + 1);
		assert(lpPropFBDataArray[0].Value.MVl.cValues <= ulMonths + 1);
	}

	if(bFound == false)
		return MAPI_E_NOT_FOUND;
	*lppPropFBDataArray = lpPropFBDataArray.release();
	return hr;
}

/**
 * Copies a array of occurrence to another array
 * @param[out]	lpDest		destination array 
 * @param[in]	lpSrc		source array
 * @param[in]	ulcValues	number of occurrence in source array
 *
 * @return		HRESULT
 */
static HRESULT HrCopyFBBlockSet(OccrInfo *lpDest, const OccrInfo *lpSrc,
    ULONG ulcValues)
{
	HRESULT hr = hrSuccess;	
	ULONG i = 0;

	for (i = 0; i < ulcValues; ++i)
		lpDest[i] = lpSrc[i];
	return hr;
}

/**
 * Adds a occurrence to the occurrence array
 * @param[in]		sOccrInfo		occurrence to be added to array
 * @param[in,out]	lppsOccrInfo	array to which occurrence is added
 * @param[out]		lpcValues		number of occurrences in array
 * @return			HRESULT
 */
HRESULT HrAddFBBlock(const OccrInfo &sOccrInfo, OccrInfo **lppsOccrInfo,
    ULONG *lpcValues)
{
	memory_ptr<OccrInfo> lpsNewOccrInfo;
	OccrInfo *lpsInputOccrInfo = *lppsOccrInfo;
	ULONG ulModVal = lpcValues != NULL ? *lpcValues + 1 : 1;
	HRESULT hr = MAPIAllocateBuffer(sizeof(sOccrInfo) * ulModVal, &~lpsNewOccrInfo);
	if (hr != hrSuccess)
		return hr;
	
	if(lpsInputOccrInfo)
		hr = HrCopyFBBlockSet(lpsNewOccrInfo, lpsInputOccrInfo, ulModVal);
	if (hr != hrSuccess)
		return hr;
	if (lpcValues != NULL)
		*lpcValues = ulModVal;
	lpsNewOccrInfo[ulModVal -1] = sOccrInfo;
	*lppsOccrInfo = lpsNewOccrInfo.release();
	MAPIFreeBuffer(lpsInputOccrInfo);
	return hrSuccess;
}

} /* namespace */
