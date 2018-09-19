/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
#include "recurrence.h"

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

#define FB_YEARMONTH(year, month)	(((static_cast<unsigned short>(year) << 4) & 0xFFF0) | static_cast<unsigned short>(month))
#define FB_YEAR(yearmonth)		(static_cast<unsigned short>(yearmonth) >> 4)
#define FB_MONTH(yearmonth)		(static_cast<unsigned short>(yearmonth) & 0x000F)

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
		if (recurrence::isLeapYear(year))
			++days;
		break;
	}

	*minutes = days * (24*60);
	return hrSuccess;
}

static HRESULT GetFreeBusyFolder(IMsgStore *lpPublicStore,
    IMAPIFolder **lppFreeBusyFolder)
{
	ULONG			cValuesFreeBusy = 0;
	memory_ptr<SPropValue> lpPropArrayFreeBusy;
	object_ptr<IMAPIFolder> lpMapiFolder;
	ULONG			ulObjType = 0;
	static constexpr const SizedSPropTagArray(1, sPropsFreeBusy) =
		{1, {PR_FREE_BUSY_FOR_LOCAL_SITE_ENTRYID}};
	enum eFreeBusyPos{ FBPOS_FREE_BUSY_FOR_LOCAL_SITE_ENTRYID};

	// Get freebusy properies
	auto hr = lpPublicStore->GetProps(sPropsFreeBusy, 0, &cValuesFreeBusy, &~lpPropArrayFreeBusy);
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
	object_ptr<IMAPIFolder> lpFreeBusyFolder;
	object_ptr<IMAPITable> lpMapiTable;
	SPropValue		sPropUser;
	rowset_ptr lpRows;
	ULONG ulObjType = 0, cbInBoxEntry = 0, i;
	object_ptr<IMessage> lpMessage;
	memory_ptr<SPropValue> lpPropfbEntryids;
	memory_ptr<SPropValue> lpPropfbEntryidsNew, lpPropFBMessage;
	memory_ptr<SPropValue> lpPropName, lpPropEmail;
	memory_ptr<ENTRYID> lpInboxEntry;
	static constexpr const SizedSPropTagArray(1, sPropsFreebusyTable) = {1, {PR_ENTRYID}};
	enum eFreeBusyTablePos{ FBPOS_ENTRYID};

	if(lpSession == NULL || lpPublicStore == NULL || lppMessage == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if(cbUserEntryID == 0 || lpUserEntryID == nullptr)
		return MAPI_E_INVALID_ENTRYID;

	// GetFreeBusyFolder  
	auto hr = GetFreeBusyFolder(lpPublicStore, &~lpFreeBusyFolder);
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

	if (lpRows->cRows == 1 && lpRows[0].lpProps[FBPOS_ENTRYID].ulPropTag == PR_ENTRYID) {
		// Open freebusy data
		hr = lpPublicStore->OpenEntry(lpRows[0].lpProps[FBPOS_ENTRYID].Value.bin.cb,
		     reinterpret_cast<ENTRYID *>(lpRows[0].lpProps[FBPOS_ENTRYID].Value.bin.lpb),
		     &IID_IMessage, MAPI_MODIFY, &ulObjType, &~lpMessage);
		if(hr != hrSuccess)
			return hr;
		return lpMessage->QueryInterface(IID_IMessage,
		       reinterpret_cast<void **>(lppMessage));
	}
	if (!bCreateIfNotExist)
		return MAPI_E_NOT_FOUND;

	// Create new freebusymessage
	hr = lpFreeBusyFolder->CreateMessage(nullptr, 0, &~lpMessage);
	if (hr != hrSuccess)
		return hr;

	// Set the user entry id
	hr = lpMessage->SetProps(1, &sPropUser, NULL);
	if (hr != hrSuccess)
		return hr;

	// Set the accountname in properties PR_DISPLAY_NAME and PR_SUBJECT
	object_ptr<IAddrBook> lpAdrBook;
	hr = lpSession->OpenAddressBook(0, NULL, AB_NO_DIALOG, &~lpAdrBook);
	if (hr != hrSuccess)
		return hr;
	object_ptr<IMailUser> lpMailUser;
	hr = lpAdrBook->OpenEntry(cbUserEntryID, lpUserEntryID, &IID_IMailUser, MAPI_BEST_ACCESS, &ulObjType, &~lpMailUser);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(lpMailUser, PR_ACCOUNT, &~lpPropName);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(lpMailUser, PR_EMAIL_ADDRESS, &~lpPropEmail);
	if (hr != hrSuccess)
		return hr;

	// Set the displayname with accountname
	lpPropName->ulPropTag = PR_DISPLAY_NAME;
	hr = lpMessage->SetProps(1, lpPropName, NULL);
	if (hr != hrSuccess)
		return hr;

	// Set the subject with accountname
	lpPropName->ulPropTag = PR_SUBJECT;
	hr = lpMessage->SetProps(1, lpPropName, NULL);
	if (hr != hrSuccess)
		return hr;

	// Set the PR_FREEBUSY_EMA with the email address
	lpPropEmail->ulPropTag = PR_FREEBUSY_EMAIL_ADDRESS;
	hr = lpMessage->SetProps(1, lpPropEmail, NULL);
	if (hr != hrSuccess)
		return hr;

	// Save message
	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		return hr;

	// Update the user freebusy entryid array
	if (lpUserStore == nullptr)
		return lpMessage->QueryInterface(IID_IMessage,
		       reinterpret_cast<void **>(lppMessage));

	// Get entryid
	hr = HrGetOneProp(lpMessage, PR_ENTRYID, &~lpPropFBMessage);
	if (hr != hrSuccess)
		return hr;

	// Open root folder
	object_ptr<IMAPIFolder> lpFolder;
	hr = lpUserStore->OpenEntry(0, NULL, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpFolder);
	if (hr != hrSuccess)
		return hr;

	ULONG ulMvItems = 4;
	// Get current freebusy entryid array
	if (HrGetOneProp(lpFolder, PR_FREEBUSY_ENTRYIDS, &~lpPropfbEntryids) == hrSuccess)
		ulMvItems = (lpPropfbEntryids->Value.MVbin.cValues > ulMvItems) ? lpPropfbEntryids->Value.MVbin.cValues : ulMvItems;
	hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpPropfbEntryidsNew);
	if (hr != hrSuccess)
		return hr;

	lpPropfbEntryidsNew->Value.MVbin.cValues = ulMvItems;
	hr = MAPIAllocateMore(sizeof(SBinary) * lpPropfbEntryidsNew->Value.MVbin.cValues, lpPropfbEntryidsNew, (void **)&lpPropfbEntryidsNew->Value.MVbin.lpbin);
	if (hr != hrSuccess)
		return hr;
	memset(lpPropfbEntryidsNew->Value.MVbin.lpbin, 0, sizeof(SBinary) * lpPropfbEntryidsNew->Value.MVbin.cValues);

	// move the old entryids to the new array
	if (lpPropfbEntryids != nullptr)
		for (i = 0; i < lpPropfbEntryids->Value.MVbin.cValues; ++i)
			lpPropfbEntryidsNew->Value.MVbin.lpbin[i] = lpPropfbEntryids->Value.MVbin.lpbin[i]; /* cheap copy */
	// Add the new entryid on position 3
	lpPropfbEntryidsNew->Value.MVbin.lpbin[2] = lpPropFBMessage->Value.bin;
	lpPropfbEntryidsNew->ulPropTag = PR_FREEBUSY_ENTRYIDS;
	hr = lpFolder->SetProps(1, lpPropfbEntryidsNew, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = lpFolder->SaveChanges(KEEP_OPEN_READONLY);
	if (hr != hrSuccess)
		return hr;

	// Get the inbox
	hr = lpUserStore->GetReceiveFolder(nullptr, 0, &cbInBoxEntry, &~lpInboxEntry, nullptr);
	if (hr != hrSuccess)
		return hr;

	// Open the inbox
	hr = lpUserStore->OpenEntry(cbInBoxEntry, lpInboxEntry, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpFolder);
	if (hr != hrSuccess)
		return hr;
	hr = lpFolder->SetProps(1, lpPropfbEntryidsNew, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = lpFolder->SaveChanges(KEEP_OPEN_READONLY);
	if (hr != hrSuccess)
		return hr;
	return lpMessage->QueryInterface(IID_IMessage,
	       reinterpret_cast<void **>(lppMessage));
}

static HRESULT ParseFBEvents(FBStatus fbSts, LPSPropValue lpMonth,
    LPSPropValue lpEvent, ECFBBlockList *lpfbBlockList)
{
	struct tm	tmTmp;
	FBBlock_1	fbBlock;

	// Check varibales
	if(lpEvent == NULL || lpMonth == NULL || lpfbBlockList == NULL ||
		lpEvent->Value.MVbin.cValues != lpMonth->Value.MVl.cValues)
		return MAPI_E_INVALID_PARAMETER;

	memset(&fbBlock, 0, sizeof(fbBlock));

	for (ULONG i = 0; i < lpEvent->Value.MVbin.cValues; ++i) {
		if(lpEvent->Value.MVbin.lpbin[i].cb == 0) // notting to do
			continue;

		ULONG cEvents = lpEvent->Value.MVbin.lpbin[i].cb / sizeof(sfbEvent);
		auto lpfbEvents = reinterpret_cast<sfbEvent *>(lpEvent->Value.MVbin.lpbin[i].lpb);

		for (ULONG j = 0; j < cEvents; ++j) {
			memset(&tmTmp, 0, sizeof(struct tm));
			tmTmp.tm_year = FB_YEAR(lpMonth->Value.MVl.lpl[i]) - 1900;
			tmTmp.tm_mon = FB_MONTH(lpMonth->Value.MVl.lpl[i])-1;
			tmTmp.tm_mday = 1;
			tmTmp.tm_min = (int)(unsigned short)lpfbEvents[j].rtmStart;
			tmTmp.tm_isdst = -1;
			auto rtmStart = UnixTimeToRTime(timegm(&tmTmp));
			memset(&tmTmp, 0, sizeof(struct tm));
			tmTmp.tm_year = FB_YEAR(lpMonth->Value.MVl.lpl[i]) - 1900;
			tmTmp.tm_mon = FB_MONTH(lpMonth->Value.MVl.lpl[i])-1;
			tmTmp.tm_mday = 1;
			tmTmp.tm_min = (int)(unsigned short)lpfbEvents[j].rtmEnd;
			tmTmp.tm_isdst = -1;
			auto rtmEnd = UnixTimeToRTime(timegm(&tmTmp));
			
			// Don't reset fbBlock.m_tmEnd
			auto bMerge = fbBlock.m_tmEnd == rtmStart;
			fbBlock.m_fbstatus = fbSts;
			fbBlock.m_tmStart = rtmStart;
			fbBlock.m_tmEnd = rtmEnd;

			if (bMerge)
				lpfbBlockList->Merge(fbBlock);
			else
				lpfbBlockList->Add(fbBlock);
		}
	}
	return S_OK;
}

HRESULT GetFreeBusyMessageData(IMessage* lpMessage, LONG* lprtmStart, LONG* lprtmEnd, ECFBBlockList	*lpfbBlockList)
{
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
	auto hr = lpMessage->GetProps(sPropsFreeBusyData, 0, &cValuesFBData, &~lpPropArrayFBData);
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
	*lprtmStart = lpPropArrayFBData[FBDATA_START_RANGE].ulPropTag == PR_FREEBUSY_START_RANGE ?
	              lpPropArrayFBData[FBDATA_START_RANGE].Value.ul : 0;
	*lprtmEnd = lpPropArrayFBData[FBDATA_END_RANGE].ulPropTag == PR_FREEBUSY_END_RANGE ?
	            lpPropArrayFBData[FBDATA_END_RANGE].Value.ul : 0;
	return hr;
}

unsigned int DiffYearMonthToMonth( struct tm *tm1, struct tm *tm2)
{
	if(tm1->tm_year == tm2->tm_year)
		return tm2->tm_mon - tm1->tm_mon;
	else if(tm2->tm_year > tm1->tm_year && tm2->tm_mon >= tm1->tm_mon)
		return 12 * (tm2->tm_year - tm1->tm_year) + tm2->tm_mon - tm1->tm_mon;
	else if(tm2->tm_year > tm1->tm_year && tm2->tm_mon < tm1->tm_mon)
		return 12 * (tm2->tm_year - tm1->tm_year - 1) + tm2->tm_mon + 1 + (11 - tm1->tm_mon);
	return 0;
}

HRESULT CreateFBProp(FBStatus fbStatus, ULONG ulMonths, ULONG ulPropMonths, ULONG ulPropEvents, ECFBBlockList* lpfbBlockList, LPSPropValue* lppPropFBDataArray)
{
	struct tm tmStart, tmEnd;
	int ulLastMonth = 0, ulLastYear = 0;
	sfbEvent		fbEvent;
	FBBlock_1		fbBlk;
	memory_ptr<SPropValue> lpPropFBDataArray;

	//Check of propertys are mv
	if(lpfbBlockList == NULL || lppPropFBDataArray == NULL)
		return MAPI_E_INVALID_PARAMETER;

	// Set the list on the begin
	lpfbBlockList->Reset();
	ULONG ulMaxItemDataSize = (lpfbBlockList->Size() + 1) * sizeof(sfbEvent); // +1 block, for free/busy in two months

	/*
		First item is Months
		Second item is the Freebusy data
	*/
	auto hr = MAPIAllocateBuffer(2 * sizeof(SPropValue), &~lpPropFBDataArray);
	if (hr != hrSuccess)
		return hr;

	auto &xmo = lpPropFBDataArray[0].Value.MVl;
	auto &fbd = lpPropFBDataArray[1].Value.MVbin;
	xmo.cValues = 0;
	fbd.cValues = 0;

	hr = MAPIAllocateMore((ulMonths + 1) * sizeof(ULONG), lpPropFBDataArray, reinterpret_cast<void **>(&xmo.lpl));  // +1 for free/busy in two months
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateMore((ulMonths + 1) * sizeof(SBinary), lpPropFBDataArray, reinterpret_cast<void **>(&fbd.lpbin)); // +1 for free/busy in two months
	if (hr != hrSuccess)
		return hr;

	//memset(&fbd.lpbin, 0, ulArrayItems);

	lpPropFBDataArray[0].ulPropTag = ulPropMonths;
	lpPropFBDataArray[1].ulPropTag = ulPropEvents;
	LONG iMonth = -1;
	bool bFound = false;

	while (lpfbBlockList->Next(&fbBlk) == hrSuccess &&
	       iMonth < static_cast<LONG>(ulMonths))
	{
		if(fbBlk.m_fbstatus == fbStatus || fbStatus == fbKopanoAllBusy)
		{
			gmtime_safe(RTimeToUnixTime(fbBlk.m_tmStart), &tmStart);
			gmtime_safe(RTimeToUnixTime(fbBlk.m_tmEnd), &tmEnd);
			
			if(tmStart.tm_year > ulLastYear || tmStart.tm_mon > ulLastMonth)
			{
				++iMonth;
				xmo.lpl[iMonth] = FB_YEARMONTH(tmStart.tm_year + 1900, tmStart.tm_mon + 1);
				++xmo.cValues;
				++fbd.cValues;
				hr = MAPIAllocateMore(ulMaxItemDataSize, lpPropFBDataArray, reinterpret_cast<void **>(&fbd.lpbin[iMonth].lpb));
				if (hr != hrSuccess)
					return hr;
				fbd.lpbin[iMonth].cb = 0;
			}

			//Different months in a block
			if(tmEnd.tm_year > tmStart.tm_year || tmEnd.tm_mon > tmStart.tm_mon)
			{
				fbEvent.rtmStart = (short)( ((tmStart.tm_mday-1)*24*60) + (tmStart.tm_hour*60) + tmStart.tm_min);
				getMaxMonthMinutes((short)tmStart.tm_year+1900, (short)tmStart.tm_mon, (short*)&fbEvent.rtmEnd);

				// Add item to struct
				memcpy(fbd.lpbin[iMonth].lpb+fbd.lpbin[iMonth].cb, &fbEvent, sizeof(sfbEvent));
				fbd.lpbin[iMonth].cb += sizeof(sfbEvent);
				assert(fbd.lpbin[iMonth].cb <= ulMaxItemDataSize);
				auto ulDiffMonths = DiffYearMonthToMonth(&tmStart, &tmEnd);
				auto tmTmp = tmStart;

				// Set the day on the begin of the month because: if mday is 31 and the next month is 30 then you get the wrong month
				tmTmp.tm_mday = 1;
				for (int i = 1; i < ulDiffMonths && xmo.cValues < ulMonths; ++i) {
					++iMonth;
					tmTmp.tm_isdst = -1;
					++tmTmp.tm_mon;
					mktime(&tmTmp);
					xmo.lpl[iMonth] = FB_YEARMONTH(tmTmp.tm_year + 1900, tmTmp.tm_mon + 1);
					++xmo.cValues;
					++fbd.cValues;
					hr = MAPIAllocateMore(ulMaxItemDataSize, lpPropFBDataArray, reinterpret_cast<void **>(&fbd.lpbin[iMonth].lpb));
					if (hr != hrSuccess)
						return hr;
					fbd.lpbin[iMonth].cb = 0;
				
					fbEvent.rtmStart = 0;					
					getMaxMonthMinutes((short)tmTmp.tm_year+1900, (short)tmTmp.tm_mon, (short*)&fbEvent.rtmEnd);

					// Add item to struct
					memcpy(fbd.lpbin[iMonth].lpb + fbd.lpbin[iMonth].cb, &fbEvent, sizeof(sfbEvent));
					fbd.lpbin[iMonth].cb += sizeof(sfbEvent);
					assert(fbd.lpbin[iMonth].cb <= ulMaxItemDataSize);
				}

				++iMonth;
				++tmTmp.tm_mon;
				tmTmp.tm_isdst = -1;
				mktime(&tmTmp);
				xmo.lpl[iMonth] = FB_YEARMONTH(tmTmp.tm_year + 1900, tmTmp.tm_mon + 1);
				++xmo.cValues;
				++fbd.cValues;
				hr = MAPIAllocateMore(ulMaxItemDataSize, lpPropFBDataArray, reinterpret_cast<void **>(&fbd.lpbin[iMonth].lpb));
				if (hr != hrSuccess)
					return hr;
				fbd.lpbin[iMonth].cb = 0;

				fbEvent.rtmStart = 0;
				fbEvent.rtmEnd = (short)( ((tmEnd.tm_mday-1)*24*60) + (tmEnd.tm_hour*60) + tmEnd.tm_min);
			} else {
				fbEvent.rtmStart = (short)( ((tmStart.tm_mday-1)*24*60) + (tmStart.tm_hour*60) + tmStart.tm_min);
				fbEvent.rtmEnd = (short)( ((tmEnd.tm_mday-1)*24*60) + (tmEnd.tm_hour*60) + tmEnd.tm_min);
			}

			// Add item to struct
			memcpy(fbd.lpbin[iMonth].lpb + fbd.lpbin[iMonth].cb, &fbEvent, sizeof(sfbEvent));
			fbd.lpbin[iMonth].cb += sizeof(sfbEvent);

			ulLastYear = tmEnd.tm_year;
			ulLastMonth = tmEnd.tm_mon;

			bFound = true;
			assert(fbd.lpbin[iMonth].cb <= ulMaxItemDataSize);
		}
		assert(iMonth == -1 || (iMonth >= 0 && static_cast<ULONG>(iMonth) < ulMonths + 1));
		assert(fbd.cValues <= ulMonths + 1);
		assert(xmo.cValues <= ulMonths + 1);
	}
	if (!bFound)
		return MAPI_E_NOT_FOUND;
	*lppPropFBDataArray = lpPropFBDataArray.release();
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
	if (lpsInputOccrInfo != nullptr)
		for (ULONG i = 0; i < ulModVal; ++i)
			lpsNewOccrInfo[i] = lpsInputOccrInfo[i];
	if (lpcValues != NULL)
		*lpcValues = ulModVal;
	lpsNewOccrInfo[ulModVal -1] = sOccrInfo;
	*lppsOccrInfo = lpsNewOccrInfo.release();
	MAPIFreeBuffer(lpsInputOccrInfo);
	return hrSuccess;
}

} /* namespace */
