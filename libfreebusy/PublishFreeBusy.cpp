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
#include <kopano/ECRestriction.h>
#include "PublishFreeBusy.h"
#include <kopano/namedprops.h>
#include <kopano/mapiguidext.h>
#include <iostream>
#include <memory>
#include <vector>
#include <algorithm>
#include <kopano/memory.hpp>
#include <kopano/ECLogger.h>
#include "recurrence.h"
#include <kopano/MAPIErrors.h>
#include "ECFreeBusyUpdate.h"
#include "freebusyutil.h"
#include "ECFreeBusySupport.h"
#include <mapiutil.h>

using namespace std;
using namespace KCHL;

namespace KC {

#define START_TIME 0
#define END_TIME 1

/** 
 * Publish free/busy information from the default calendar
 * 
 * @param[in] lpSession Session object of user
 * @param[in] lpDefStore Store of user
 * @param[in] tsStart Start time to publish data of
 * @param[in] ulMonths Number of months to publish
 * 
 * @return MAPI Error code
 */
HRESULT HrPublishDefaultCalendar(IMAPISession *lpSession, IMsgStore *lpDefStore,
    time_t tsStart, ULONG ulMonths)
{
	HRESULT hr = hrSuccess;
	std::unique_ptr<PublishFreeBusy> lpFreeBusy;
	object_ptr<IMAPITable> lpTable;
	memory_ptr<FBBlock_1> lpFBblocks;
	ULONG cValues = 0;

	ec_log_debug("current time %d", (int)tsStart);
	lpFreeBusy.reset(new PublishFreeBusy(lpSession, lpDefStore, tsStart, ulMonths));
	hr = lpFreeBusy->HrInit();
	if (hr != hrSuccess)
		return hr;
	hr = lpFreeBusy->HrGetResctItems(&~lpTable);
	if (hr != hrSuccess) {
		ec_log_info("Error while finding messages for free/busy publish, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	hr = lpFreeBusy->HrProcessTable(lpTable, &~lpFBblocks, &cValues);
	if(hr != hrSuccess) {
		ec_log_info("Error while finding free/busy blocks, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	if (cValues == 0) {
		ec_log_info("No messages for free/busy publish");
		return hr;
	}
	hr = lpFreeBusy->HrMergeBlocks(&+lpFBblocks, &cValues);
	if(hr != hrSuccess) {
		ec_log_info("Error while merging free/busy blocks, entries: %d, error code: 0x%x %s", cValues, hr, GetMAPIErrorMessage(hr));
		return hr;
	}
	ec_log_debug("Publishing %d free/busy blocks", cValues);
	hr = lpFreeBusy->HrPublishFBblocks(lpFBblocks, cValues);
	if (hr != hrSuccess)
		ec_log_info("Error while publishing free/busy blocks, entries: %d, error code: 0x%x %s", cValues, hr, GetMAPIErrorMessage(hr));
	return hr;
}

/** 
 * Class handling free/busy publishing.
 * @todo validate input time & months.
 *
 * @param[in] lpSession 
 * @param[in] lpDefStore 
 * @param[in] tsStart 
 * @param[in] ulMonths 
 */
PublishFreeBusy::PublishFreeBusy(IMAPISession *lpSession, IMsgStore *lpDefStore,
    time_t tsStart, ULONG ulMonths) :
	__propmap(7)
{
	m_lpSession = lpSession;
	m_lpDefStore = lpDefStore;
	m_tsStart = tsStart;
	m_tsEnd = tsStart + (ulMonths * (30*24*60*60));

	UnixTimeToFileTime(m_tsStart, &m_ftStart);
	UnixTimeToFileTime(m_tsEnd , &m_ftEnd);
}

/** 
 * Initialize object. Get named properties required for publishing
 * free/busy data.
 * 
 * @return MAPI Error code
 */
HRESULT PublishFreeBusy::HrInit()
{
	HRESULT hr = hrSuccess;
	
	PROPMAP_INIT_NAMED_ID (APPT_STARTWHOLE, 	PT_SYSTIME, PSETID_Appointment, dispidApptStartWhole)
	PROPMAP_INIT_NAMED_ID (APPT_ENDWHOLE, 		PT_SYSTIME, PSETID_Appointment, dispidApptEndWhole)
	PROPMAP_INIT_NAMED_ID (APPT_CLIPEND,		PT_SYSTIME, PSETID_Appointment, dispidClipEnd)
	PROPMAP_INIT_NAMED_ID (APPT_ISRECURRING,	PT_BOOLEAN, PSETID_Appointment, dispidRecurring)
	PROPMAP_INIT_NAMED_ID (APPT_FBSTATUS,		PT_LONG, PSETID_Appointment,	dispidBusyStatus)
	PROPMAP_INIT_NAMED_ID (APPT_RECURRINGSTATE,	PT_BINARY, PSETID_Appointment,	dispidRecurrenceState)
	PROPMAP_INIT_NAMED_ID (APPT_TIMEZONESTRUCT,	PT_BINARY, PSETID_Appointment,	dispidTimeZoneData)
	PROPMAP_INIT (m_lpDefStore)
	;
 exitpm:
	return hr;
}

/** 
 * Create a contents table with items to be published for free/busy
 * times.
 * 
 * @todo rename function 
 * @todo use restriction class to create restrictions
 *
 * @param[out] lppTable Default calendar contents table
 * 
 * @return MAPI Error code
 */
HRESULT PublishFreeBusy::HrGetResctItems(IMAPITable **lppTable)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMAPIFolder> lpDefCalendar;
	object_ptr<IMAPITable> lpTable;
	SPropValue lpsPropStart;
	SPropValue lpsPropEnd;
	SPropValue lpsPropIsRecc;
	SPropValue lpsPropReccEnd;
		
	hr = HrOpenDefaultCalendar(m_lpDefStore, &~lpDefCalendar);
	if(hr != hrSuccess)
		return hr;
	hr = lpDefCalendar->GetContentsTable(0, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	
	lpsPropStart.ulPropTag = PROP_APPT_STARTWHOLE;
	lpsPropStart.Value.ft = m_ftStart;

	lpsPropEnd.ulPropTag = PROP_APPT_ENDWHOLE;
	lpsPropEnd.Value.ft = m_ftEnd;

	lpsPropIsRecc.ulPropTag = PROP_APPT_ISRECURRING;
	lpsPropIsRecc.Value.b = true;

	lpsPropReccEnd.ulPropTag = PROP_APPT_CLIPEND;
	lpsPropReccEnd.Value.ft = m_ftStart;

	hr = ECOrRestriction(
		//ITEM[START] >= START && ITEM[START] <= END;
		ECAndRestriction(
			ECPropertyRestriction(RELOP_GE, PROP_APPT_STARTWHOLE, &lpsPropStart) + // item[start]
			ECPropertyRestriction(RELOP_LE, PROP_APPT_STARTWHOLE, &lpsPropEnd) // item[start]
		) +
		//ITEM[END] >= START && ITEM[END] <= END;
		ECAndRestriction(
			ECPropertyRestriction(RELOP_GE, PROP_APPT_ENDWHOLE, &lpsPropStart) + // item[end]
			ECPropertyRestriction(RELOP_LE, PROP_APPT_ENDWHOLE, &lpsPropEnd) // item[end]
		) +
		//ITEM[START] < START && ITEM[END] > END;
		ECAndRestriction(
			ECPropertyRestriction(RELOP_LT, PROP_APPT_STARTWHOLE, &lpsPropStart) + // item[start]
			ECPropertyRestriction(RELOP_GT, PROP_APPT_ENDWHOLE, &lpsPropEnd) // item[end]
		) +
		ECAndRestriction(
			ECExistRestriction(PROP_APPT_CLIPEND) +
			ECPropertyRestriction(RELOP_EQ, PROP_APPT_ISRECURRING, &lpsPropIsRecc) +
			ECPropertyRestriction(RELOP_GE, PROP_APPT_CLIPEND, &lpsPropReccEnd)
		) +
		ECAndRestriction(
			ECNotRestriction(ECExistRestriction(PROP_APPT_CLIPEND)) +
			ECPropertyRestriction(RELOP_LE, PROP_APPT_STARTWHOLE, &lpsPropEnd) +
			ECPropertyRestriction(RELOP_EQ, PROP_APPT_ISRECURRING, &lpsPropIsRecc)
		)
	).RestrictTable(lpTable);
	if (hr != hrSuccess)
		return hr;
	*lppTable = lpTable.release();
	return hrSuccess;
}

/**
 * Calculates the freebusy blocks from the rows of the table.
 * It also adds the occurrences of the recurrence in the array of blocks.
 *
 * @param[in]	lpTable			restricted mapi table containing the rows
 * @param[out]	lppfbBlocks		array of freebusy blocks
 * @param[out]	lpcValues		number of freebusy blocks in lppfbBlocks array
 *
 * @return		MAPI Error code
 */
HRESULT PublishFreeBusy::HrProcessTable(IMAPITable *lpTable, FBBlock_1 **lppfbBlocks, ULONG *lpcValues)
{
	HRESULT hr = hrSuccess;
	SRowSet *lpRowSet = NULL;
	memory_ptr<OccrInfo> lpOccrInfo;
	FBBlock_1 *lpfbBlocks = NULL;
	recurrence lpRecurrence;
	ULONG ulFbStatus = 0;
	SizedSPropTagArray(7, proptags) =
		{7, {PROP_APPT_STARTWHOLE, PROP_APPT_ENDWHOLE,
		PROP_APPT_FBSTATUS, PROP_APPT_ISRECURRING,
		PROP_APPT_RECURRINGSTATE, PROP_APPT_CLIPEND,
		PROP_APPT_TIMEZONESTRUCT}};
	hr = lpTable->SetColumns(proptags, 0);
	if(hr != hrSuccess)
		goto exit;

	while (true)
	{
		hr = lpTable->QueryRows(50, 0, &lpRowSet);
		if(hr != hrSuccess)
			goto exit;

		if(lpRowSet->cRows == 0)
			break;
		
		for (ULONG i = 0; i < lpRowSet->cRows; ++i) {
			TIMEZONE_STRUCT ttzInfo = {0};
			
			ulFbStatus = 0;

			if(lpRowSet->aRow[i].lpProps[3].ulPropTag == PROP_APPT_ISRECURRING 
				&& lpRowSet->aRow[i].lpProps[3].Value.b == true)
			{
				if(lpRowSet->aRow[i].lpProps[4].ulPropTag == PROP_APPT_RECURRINGSTATE) 
				{
					hr = lpRecurrence.HrLoadRecurrenceState((char *)(lpRowSet->aRow[i].lpProps[4].Value.bin.lpb),lpRowSet->aRow[i].lpProps[4].Value.bin.cb, 0);
					if(FAILED(hr)) {
						ec_log_err("Error loading recurrence state, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
						continue;
					}

					if (lpRowSet->aRow[i].lpProps[6].ulPropTag == PROP_APPT_TIMEZONESTRUCT)
						ttzInfo = *(TIMEZONE_STRUCT*)lpRowSet->aRow[i].lpProps[6].Value.bin.lpb;

					if (lpRowSet->aRow[i].lpProps[2].ulPropTag == PROP_APPT_FBSTATUS)
						ulFbStatus = lpRowSet->aRow[i].lpProps[2].Value.ul;
					hr = lpRecurrence.HrGetItems(m_tsStart, m_tsEnd, ttzInfo, ulFbStatus, &+lpOccrInfo, lpcValues);
					if (hr != hrSuccess || !lpOccrInfo) {
						ec_log_err("Error expanding items for recurring item, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
						continue;
					}
				}
			}
			else
			{
				OccrInfo sOccrBlock;
				
				if (lpRowSet->aRow[i].lpProps[0].ulPropTag == PROP_APPT_STARTWHOLE) 
					FileTimeToRTime(&lpRowSet->aRow[i].lpProps[0].Value.ft, &sOccrBlock.fbBlock.m_tmStart);

				if (lpRowSet->aRow[i].lpProps[1].ulPropTag == PROP_APPT_ENDWHOLE) {
				
					FileTimeToRTime(&lpRowSet->aRow[i].lpProps[1].Value.ft, &sOccrBlock.fbBlock.m_tmEnd);
					FileTimeToUnixTime(lpRowSet->aRow[i].lpProps[1].Value.ft, &sOccrBlock.tBaseDate);
				}
				if (lpRowSet->aRow[i].lpProps[2].ulPropTag == PROP_APPT_FBSTATUS) 
					sOccrBlock.fbBlock.m_fbstatus = (FBStatus)lpRowSet->aRow[i].lpProps[2].Value.ul;
				hr = HrAddFBBlock(sOccrBlock, &+lpOccrInfo, lpcValues);
				if (hr != hrSuccess) {
					ec_log_debug("Error adding occurrence block to list, error code: 0x%x %s", hr, GetMAPIErrorMessage(hr));
					goto exit;
				}
			}
	
		}
		FreeProws(lpRowSet);
		lpRowSet = NULL;
	}
	
	if (lpcValues != 0 && lpOccrInfo != NULL) {
		hr = MAPIAllocateBuffer(sizeof(FBBlock_1)* (*lpcValues), (void**)&lpfbBlocks);
		if(hr != hrSuccess)
			goto exit;

		for (ULONG i = 0 ; i < *lpcValues; ++i)
			lpfbBlocks[i]  = lpOccrInfo[i].fbBlock;

		*lppfbBlocks = lpfbBlocks;
		lpfbBlocks = NULL;
	}

exit:
	if (lpRowSet)
		FreeProws(lpRowSet);

	return hr;
}

/** 
 * Merge overlapping free/busy blocks.
 * 
 * @param[in,out] lppfbBlocks In: generated blocks, Out: merged blocks
 * @param[in,out] lpcValues Number of blocks in lppfbBlocks
 * 
 * @return MAPI Error code
 */
HRESULT PublishFreeBusy::HrMergeBlocks(FBBlock_1 **lppfbBlocks, ULONG *lpcValues)
{
	FBBlock_1 *lpFbBlocks = NULL;
	ULONG cValues = *lpcValues;
	ULONG ulLevel = 0;
	time_t tsLastTime = 0;
	TSARRAY sTsitem = {0,0,0};
	std::map<time_t , TSARRAY> mpTimestamps;
	std::vector <ULONG> vctStatus;
	std::vector <FBBlock_1> vcFBblocks;
	time_t tTemp = 0;

	ec_log_debug("Input blocks %ul", cValues);

	lpFbBlocks = *lppfbBlocks;
	for (ULONG i = 0; i < cValues; ++i) {
		sTsitem.ulType = START_TIME;
		sTsitem.ulStatus = lpFbBlocks[i].m_fbstatus;
		sTsitem.tsTime = lpFbBlocks[i].m_tmStart;
		RTimeToUnixTime(sTsitem.tsTime, &tTemp);

		// @note ctime adds \n character
		ec_log_debug("Blocks start %s", ctime(&tTemp));

		mpTimestamps[sTsitem.tsTime] = sTsitem;
		
		sTsitem.ulType = END_TIME;
		sTsitem.ulStatus = lpFbBlocks[i].m_fbstatus;
		sTsitem.tsTime = lpFbBlocks[i].m_tmEnd;

		mpTimestamps[sTsitem.tsTime] = sTsitem;
	}
	
	for (const auto &pts : mpTimestamps) {
		FBBlock_1 fbBlockTemp;

		sTsitem = pts.second;
		switch(sTsitem.ulType)
		{
		case START_TIME:
			if (ulLevel != 0 && tsLastTime != sTsitem.tsTime)
			{
				std::sort(vctStatus.begin(),vctStatus.end());
				fbBlockTemp.m_tmStart = tsLastTime;
				fbBlockTemp.m_tmEnd = sTsitem.tsTime;
				fbBlockTemp.m_fbstatus = (enum FBStatus)(vctStatus.size()> 0 ? vctStatus.back(): 0);// sort it to get max of status
				if(fbBlockTemp.m_fbstatus != 0)
					vcFBblocks.push_back(fbBlockTemp);
			}
			++ulLevel;
			vctStatus.push_back(sTsitem.ulStatus);
			tsLastTime = sTsitem.tsTime;
			break;
		case END_TIME:
			if(tsLastTime != sTsitem.tsTime)
			{
				std::sort(vctStatus.begin(),vctStatus.end());// sort it to get max of status
				fbBlockTemp.m_tmStart = tsLastTime;
				fbBlockTemp.m_tmEnd = sTsitem.tsTime;
				fbBlockTemp.m_fbstatus = (enum FBStatus)(vctStatus.size()> 0 ? vctStatus.back(): 0);
				if(fbBlockTemp.m_fbstatus != 0)
					vcFBblocks.push_back(fbBlockTemp);
			}
			--ulLevel;
			if(!vctStatus.empty()){
				auto iterStatus = std::find(vctStatus.begin(), vctStatus.end(), sTsitem.ulStatus);
				if (iterStatus != vctStatus.end())
					vctStatus.erase(iterStatus);
			}
			tsLastTime = sTsitem.tsTime;
			break;
		}
	}

	// Free previously allocated memory
	if (*lppfbBlocks != NULL) {
		MAPIFreeBuffer(*lppfbBlocks);
		*lppfbBlocks = NULL;
	}

	HRESULT hr = MAPIAllocateBuffer(sizeof(FBBlock_1) * vcFBblocks.size(),
	             reinterpret_cast<void **>(&lpFbBlocks));
	if (hr != hrSuccess)
		return hr;

	ULONG i = 0;
	for (const auto &vcblock : vcFBblocks)
		lpFbBlocks[i++] = vcblock;
	*lppfbBlocks = lpFbBlocks;
	*lpcValues = vcFBblocks.size();

	ec_log_debug("Output blocks %d", *lpcValues);
	return hrSuccess;
}

/** 
 * Save free/busy blocks in public for current user
 * 
 * @param[in] lpfbBlocks new blocks to publish
 * @param[in] cValues number of blocks to publish
 * 
 * @return MAPI Error code
 */
HRESULT PublishFreeBusy::HrPublishFBblocks(FBBlock_1 *lpfbBlocks, ULONG cValues)
{
	HRESULT hr = hrSuccess;
	object_ptr<ECFreeBusyUpdate> lpFBUpdate;
	object_ptr<IMessage> lpMessage;
	object_ptr<IMsgStore> lpPubStore;
	memory_ptr<SPropValue> lpsPrpUsrMEid;
	time_t tsStart = 0;

	hr = HrOpenECPublicStore(m_lpSession, &~lpPubStore);
	if(hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(m_lpDefStore, PR_MAILBOX_OWNER_ENTRYID, &~lpsPrpUsrMEid);
	if(hr != hrSuccess)
		return hr;
	hr = GetFreeBusyMessage(m_lpSession, lpPubStore, m_lpDefStore, lpsPrpUsrMEid[0].Value.bin.cb, reinterpret_cast<ENTRYID *>(lpsPrpUsrMEid[0].Value.bin.lpb), true, &~lpMessage);
	if(hr != hrSuccess)
		return hr;
	hr = ECFreeBusyUpdate::Create(lpMessage, &~lpFBUpdate);
	if(hr != hrSuccess)
		return hr;
	hr = lpFBUpdate->ResetPublishedFreeBusy();
	if(hr != hrSuccess)
		return hr;
	hr = lpFBUpdate->PublishFreeBusy(lpfbBlocks, cValues);
	if(hr != hrSuccess)
		return hr;

	FileTimeToUnixTime(m_ftStart, &tsStart);
	// @todo use a "start of day" function?
	tsStart = tsStart - 86400; // 24*60*60 = 86400 include current day.
	UnixTimeToFileTime(tsStart, &m_ftStart);
	return lpFBUpdate->SaveChanges(m_ftStart, m_ftEnd);
}

} /* namespace */
