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
#include <kopano/stringutil.h>
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include "ECSession.h"
#include "ECSessionManager.h"
#include "ECDatabaseFactory.h"
#include "ECStatsCollector.h"
#include <kopano/kcodes.h>

#include "ECTPropsPurge.h"

using namespace KC::chrono_literals;

namespace KC {

extern ECStatsCollector*     g_lpStatsCollector;

ECTPropsPurge::ECTPropsPurge(ECConfig *lpConfig,
    ECDatabaseFactory *lpDatabaseFactory) :
	m_lpConfig(lpConfig), m_lpDatabaseFactory(lpDatabaseFactory)
{
    // Start our purge thread
    pthread_create(&m_hThread, NULL, Thread, (void *)this);
    set_thread_name(m_hThread, "TPropsPurge");
}

ECTPropsPurge::~ECTPropsPurge()
{
	// Signal thread to exit
	ulock_normal l_exit(m_hMutexExit);
	m_bExit = true;
	m_hCondExit.notify_all();
	l_exit.unlock();
	
	// Wait for the thread to exit
	pthread_join(m_hThread, NULL);
}

/**
 * This is just a pthread_create() wrapper which calls PurgeThread()
 *
 * @param param Pthread context param
 * @return pthread return code, 0 on success, 1 on error
 */
void * ECTPropsPurge::Thread(void *param)
{
	kcsrv_blocksigs();
	static_cast<ECTPropsPurge *>(param)->PurgeThread();
	return NULL;
}

/**
 * Main TProps purger loop
 *
 * This is a constantly running loop that checks the number of deferred updates in the
 * deferredupdate table, and starts purging them if it goes over a certain limit. The purged
 * items are from the largest folder first; A folder with 20 deferredupdates will be purged
 * before a folder with only 10 deferred updates.
 *
 * The loop (thread) will exit ASAP when m_bExit is set to TRUE.
 *
 * @return result
 */
ECRESULT ECTPropsPurge::PurgeThread()
{
    ECRESULT er = erSuccess;
    ECDatabase *lpDatabase = NULL;
    
    while(1) {
    	// Run in a loop constantly checking our deferred update table
    	
        if(!lpDatabase) {
            er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
            if(er != erSuccess) {
                ec_log_crit("Unable to get database connection for delayed purge!");
                Sleep(60000);
                continue;
            }
        }

		// Wait a while before repolling the count, unless we are requested to exit        
        {
			ulock_normal l_exit(m_hMutexExit);
            
			if (m_bExit)
				break;
			m_hCondExit.wait_for(l_exit, 10s);
			if (m_bExit)
				break;
        }
        
        PurgeOverflowDeferred(lpDatabase); // Ignore error, just retry
    }
    
    // Don't touch anything in *this from this point, we may have been delete()d by this time
    return er;
}

/**
 * Purge deferred updates
 *
 * This purges deferred updates until the total number of deferred updates drops below
 * the limit in max_deferred_records.
 *
 * @param lpDatabase Database to use
 * @return Result
 */
ECRESULT ECTPropsPurge::PurgeOverflowDeferred(ECDatabase *lpDatabase)
{
    unsigned int ulCount = 0;
    unsigned int ulFolderId = 0;
    unsigned int ulMaxDeferred = atoi(m_lpConfig->GetSetting("max_deferred_records"));
    
	if (ulMaxDeferred == 0)
		return erSuccess;
	while (!m_bExit) {
		auto er = GetDeferredCount(lpDatabase, &ulCount);
		if (er != erSuccess)
			return er;
		if (ulCount < ulMaxDeferred)
			break;
		auto dtx = lpDatabase->Begin(er);
		if (er != erSuccess)
			return er;
		er = GetLargestFolderId(lpDatabase, &ulFolderId);
		if (er != erSuccess)
			return er;
		er = PurgeDeferredTableUpdates(lpDatabase, ulFolderId);
		if (er != erSuccess)
			return er;
		er = dtx.commit();
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

/**
 * Get the deferred record count
 *
 * This gets the total number of deferred records
 *
 * @param[in] lpDatabase Database pointer
 * @param[out] Number of records
 * @return Result
 */
ECRESULT ECTPropsPurge::GetDeferredCount(ECDatabase *lpDatabase, unsigned int *lpulCount)
{
	DB_RESULT lpResult;
    
	auto er = lpDatabase->DoSelect("SELECT count(*) FROM deferredupdate", &lpResult);
    if(er != erSuccess)
		return er;
	auto lpRow = lpResult.fetch_row();
    if(!lpRow || !lpRow[0]) {
	ec_log_err("ECTPropsPurge::GetDeferredCount(): row or column null");
		return KCERR_DATABASE_ERROR;
    }
    
    *lpulCount = atoui(lpRow[0]);
        return erSuccess;
}

/**
 * Get the folder with the most deferred items in it
 *
 * Retrieves the hierarchy ID of the folder with the most deferred records in it. If two or more
 * folders tie, then one of these folders is returned. It is undefined exactly which one will be returned.
 *
 * @param[in] lpDatabase Database pointer
 * @param[out] lpulFolderId Hierarchy ID of folder
 * @return Result
 */
ECRESULT ECTPropsPurge::GetLargestFolderId(ECDatabase *lpDatabase, unsigned int *lpulFolderId)
{
	DB_RESULT lpResult;
    
	auto er = lpDatabase->DoSelect("SELECT folderid, COUNT(*) as c FROM deferredupdate GROUP BY folderid ORDER BY c DESC LIMIT 1", &lpResult);
    if(er != erSuccess)
		return er;
	auto lpRow = lpResult.fetch_row();
	if (lpRow == nullptr || lpRow[0] == nullptr)
		// Could be that there are no deferred updates, so give an appropriate error
		return KCERR_NOT_FOUND;
    *lpulFolderId = atoui(lpRow[0]);
	return erSuccess;
}

/**
 * Purge deferred table updates stored for folder ulFolderId
 *
 * This purges deferred records for hierarchy and contents tables of ulFolderId, and removes
 * them from the deferredupdate table.
 *
 * @param[in] lpDatabase Database pointer
 * @param[in] Hierarchy ID of folder to purge
 * @return Result
 */
// @todo, multiple threads call this function, which will cause problems
ECRESULT ECTPropsPurge::PurgeDeferredTableUpdates(ECDatabase *lpDatabase, unsigned int ulFolderId)
{
	unsigned int ulAffected;
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;

	std::string strIn;
	
	// This makes sure that we lock the record in the hierarchy *first*. This helps in serializing access and avoiding deadlocks.
	std::string strQuery = "SELECT hierarchyid FROM deferredupdate WHERE folderid=" + stringify(ulFolderId);
	auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	if (lpDBResult.get_num_rows() == 0)
		return erSuccess;
	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		strIn += lpDBRow[0];
		strIn += ",";
	}
	strIn.resize(strIn.size()-1);

	strQuery = "SELECT id FROM hierarchy WHERE id IN(";
	strQuery += strIn;
	strQuery += ") FOR UPDATE";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
			
	strQuery = "REPLACE INTO tproperties (folderid, hierarchyid, tag, type, val_ulong, val_string, val_binary, val_double, val_longint, val_hi, val_lo) ";
	strQuery += "SELECT " + stringify(ulFolderId) + ", p.hierarchyid, p.tag, p.type, val_ulong, LEFT(val_string, " + stringify(TABLE_CAP_STRING) + "), LEFT(val_binary, " + stringify(TABLE_CAP_BINARY) + "), val_double, val_longint, val_hi, val_lo FROM properties AS p FORCE INDEX(primary) JOIN deferredupdate FORCE INDEX(folderid) ON deferredupdate.hierarchyid=p.hierarchyid WHERE tag NOT IN(4105, 4115) AND deferredupdate.folderid = " + stringify(ulFolderId);

	er = lpDatabase->DoInsert(strQuery);
	if(er != erSuccess)
		return er;

	strQuery = "DELETE FROM deferredupdate WHERE hierarchyid IN(" + strIn + ")";
	er = lpDatabase->DoDelete(strQuery, &ulAffected);
	if(er != erSuccess)
		return er;
	g_lpStatsCollector->Increment(SCN_DATABASE_MERGES);
	g_lpStatsCollector->Increment(SCN_DATABASE_MERGED_RECORDS, (int)ulAffected);
	return erSuccess;
}

ECRESULT ECTPropsPurge::GetDeferredCount(ECDatabase *lpDatabase, unsigned int ulFolderId, unsigned int *lpulCount)
{
	DB_RESULT lpDBResult;
	unsigned int ulCount = 0;
	
	std::string strQuery = "SELECT count(*) FROM deferredupdate WHERE folderid = " + stringify(ulFolderId);
	auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	auto lpDBRow = lpDBResult.fetch_row();
	if(!lpDBRow || !lpDBRow[0])
		ulCount = 0;
	else
		ulCount = atoui(lpDBRow[0]);
		
	*lpulCount = ulCount;
	return erSuccess;
}

/**
 * Add a deferred update
 *
 * Adds a deferred update to the deferred updates table and purges the deferred updates for the folder if necessary.
 *
 * @param[in] lpSession Session that created the change
 * @param[in] lpDatabase Database handle
 * @param[in] ulFolderId Folder ID to add a deferred update to
 * @param[in] ulOldFolderId Previous folder ID if the message was moved (may be 0)
 * @param[in] ulObjId Object ID that should be added
 * @return result
 */
ECRESULT ECTPropsPurge::AddDeferredUpdate(ECSession *lpSession, ECDatabase *lpDatabase, unsigned int ulFolderId, unsigned int ulOldFolderId, unsigned int ulObjId)
{
	auto er = AddDeferredUpdateNoPurge(lpDatabase, ulFolderId, ulOldFolderId, ulObjId);
	if (er != erSuccess)
		return er;
	return NormalizeDeferredUpdates(lpSession, lpDatabase, ulFolderId);
}

/**
 * Add a deferred update
 *
 * Adds a deferred update to the deferred updates table but never purges the deferred updates for the folder.
 *
 * @param[in] lpDatabase Database handle
 * @param[in] ulFolderId Folder ID to add a deferred update to
 * @param[in] ulOldFolderId Previous folder ID if the message was moved (may be 0)
 * @param[in] ulObjId Object ID that should be added
 * @return result
 */
ECRESULT ECTPropsPurge::AddDeferredUpdateNoPurge(ECDatabase *lpDatabase, unsigned int ulFolderId, unsigned int ulOldFolderId, unsigned int ulObjId)
{
	std::string strQuery;

	if (ulOldFolderId)
		// Message has moved into a new folder. If the record is already there then just update the existing record so that srcfolderid from a previous move remains untouched
		strQuery = "INSERT INTO deferredupdate(hierarchyid, srcfolderid, folderid) VALUES(" + stringify(ulObjId) + "," + stringify(ulOldFolderId) + "," + stringify(ulFolderId) + ") ON DUPLICATE KEY UPDATE folderid = " + stringify(ulFolderId);
	else
		// Message has modified. If there is already a record for this message, we don't need to do anything
		strQuery = "INSERT IGNORE INTO deferredupdate(hierarchyid, srcfolderid, folderid) VALUES(" + stringify(ulObjId) + "," + stringify(ulFolderId) + "," + stringify(ulFolderId) + ")";
		
	return lpDatabase->DoInsert(strQuery);
}

/**
 * Purge the deferred updates table if the count for the folder exceeds max_deferred_records_folder
 *
 * Purges the deferred updates for the folder if necessary.
 *
 * @param[in] lpSession Session that created the change
 * @param[in] lpDatabase Database handle
 * @param[in] ulFolderId Folder ID to add a deferred update to
 * @return result
 */
ECRESULT ECTPropsPurge::NormalizeDeferredUpdates(ECSession *lpSession, ECDatabase *lpDatabase, unsigned int ulFolderId)
{
	unsigned int ulCount = 0;
	auto ulMaxDeferred = atoui(lpSession->GetSessionManager()->GetConfig()->GetSetting("max_deferred_records_folder"));
	
	if (ulMaxDeferred == 0)
		return erSuccess;
	auto er = GetDeferredCount(lpDatabase, ulFolderId, &ulCount);
	if (er != erSuccess)
		return er;
	if (ulCount < ulMaxDeferred)
		return erSuccess;
	return PurgeDeferredTableUpdates(lpDatabase, ulFolderId);
}

} /* namespace */
