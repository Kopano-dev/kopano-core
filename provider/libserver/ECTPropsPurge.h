/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECTPROPSPURGE_H
#define ECTPROPSPURGE_H

#include <kopano/zcdefs.h>
#include <memory>
#include <mutex>
#include <pthread.h>

namespace KC {

class ECDatabase;
class ECConfig;
class ECDatabaseFactory;
class ECSession;

class ECTPropsPurge final {
public:
	ECTPropsPurge(std::shared_ptr<ECConfig>, ECDatabaseFactory *lpDatabaseFactory);
    ~ECTPropsPurge();

    static ECRESULT PurgeDeferredTableUpdates(ECDatabase *lpDatabase, unsigned int ulFolderId);
    static ECRESULT GetDeferredCount(ECDatabase *lpDatabase, unsigned int *lpulCount);
    static ECRESULT GetLargestFolderId(ECDatabase *lpDatabase, unsigned int *lpulFolderId);
    static ECRESULT AddDeferredUpdate(ECSession *lpSession, ECDatabase *lpDatabase, unsigned int ulFolderId, unsigned int ulOldFolderId, unsigned int ulObjId);
    static ECRESULT AddDeferredUpdateNoPurge(ECDatabase *lpDatabase, unsigned int ulFolderId, unsigned int ulOldFolderId, unsigned int ulObjId);
    static ECRESULT NormalizeDeferredUpdates(ECSession *lpSession, ECDatabase *lpDatabase, unsigned int ulFolderId);

private:
    ECRESULT PurgeThread();
    ECRESULT PurgeOverflowDeferred(ECDatabase *lpDatabase);
    static ECRESULT GetDeferredCount(ECDatabase *lpDatabase, unsigned int ulFolderId, unsigned int *lpulCount);
    static void *Thread(void *param);

	std::mutex m_hMutexExit;
	std::condition_variable m_hCondExit;
    pthread_t			m_hThread;
	bool m_thread_active = false, m_bExit = false;
	std::shared_ptr<ECConfig> m_lpConfig;
    ECDatabaseFactory *m_lpDatabaseFactory;
};

} /* namespace */

#endif
