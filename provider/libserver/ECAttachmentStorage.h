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

#ifndef EC_ATTACHMENT_STORAGE
#define EC_ATTACHMENT_STORAGE

#include <kopano/zcdefs.h>
#include "ECDatabase.h"
#include <list>
#include <set>
#include <string>
#include <dirent.h>
#include <sys/types.h>

class ECSerializer;
class ECLogger;

class ECAttachmentStorage {
public:
	ECAttachmentStorage(ECDatabase *lpDatabase, unsigned int ulCompressionLevel);

	ULONG AddRef();
	ULONG Release();

	static ECRESULT CreateAttachmentStorage(ECDatabase *lpDatabase, ECConfig *lpConfig, ECAttachmentStorage **lppAttachmentStorage);

	/* Single Instance Attachment wrappers (should not be overridden by subclasses) */
	bool ExistAttachment(ULONG ulObjId, ULONG ulPropId);
	ECRESULT LoadAttachment(struct soap *soap, ULONG ulObjId, ULONG ulPropId, size_t *lpiSize, unsigned char **lppData);
	ECRESULT LoadAttachment(ULONG ulObjId, ULONG ulPropId, size_t *lpiSize, ECSerializer *lpSink);
	ECRESULT SaveAttachment(ULONG ulObjId, ULONG ulPropId, bool bDeleteOld, size_t iSize, unsigned char *lpData, ULONG *lpulInstanceId);
	ECRESULT SaveAttachment(ULONG ulObjId, ULONG ulPropId, bool bDeleteOld, size_t iSize, ECSerializer *lpSource, ULONG *lpulInstanceId);
	ECRESULT SaveAttachment(ULONG ulObjId, ULONG ulPropId, bool bDeleteOld, ULONG ulInstanceId, ULONG *lpulInstanceId);
	ECRESULT CopyAttachment(ULONG ulObjId, ULONG ulNewObjId);
	ECRESULT DeleteAttachments(const std::list<ULONG> &lstDeleteObjects);
	ECRESULT DeleteAttachment(ULONG ulObjId, ULONG ulPropId);
	ECRESULT GetSize(ULONG ulObjId, ULONG ulPropId, size_t *lpulSize);

	/* Convert ObjectId (hierarchyid) into Instance Id */
	ECRESULT GetSingleInstanceId(ULONG ulObjId, ULONG ulPropId, ULONG *lpulInstanceId);
	ECRESULT GetSingleInstanceIds(const std::list<ULONG> &lstObjIds, std::list<ULONG> *lstAttachIds);

	/* Request parents for a particular Single Instance Id */
	ECRESULT GetSingleInstanceParents(ULONG ulInstanceId, std::list<ULONG> *lplstObjIds);

	/* Single Instance Attachment handlers (must be overridden by subclasses) */
	virtual bool ExistAttachmentInstance(ULONG ulInstanceId) = 0;

	virtual ECRESULT Begin() = 0;
	virtual ECRESULT Commit() = 0;
	virtual ECRESULT Rollback() = 0;

protected:
	virtual ~ECAttachmentStorage(void) {}
	
	/* Single Instance Attachment handlers (must be overridden by subclasses) */
	virtual ECRESULT LoadAttachmentInstance(struct soap *soap, ULONG ulInstanceId, size_t *lpiSize, unsigned char **lppData) = 0;
	virtual ECRESULT LoadAttachmentInstance(ULONG ulInstanceId, size_t *lpiSize, ECSerializer *lpSink) = 0;
	virtual ECRESULT SaveAttachmentInstance(ULONG ulInstanceId, ULONG ulPropId, size_t iSize, unsigned char *lpData) = 0;
	virtual ECRESULT SaveAttachmentInstance(ULONG ulInstanceId, ULONG ulPropId, size_t iSize, ECSerializer *lpSource) = 0;
	virtual ECRESULT DeleteAttachmentInstances(const std::list<ULONG> &lstDeleteInstances, bool bReplace) = 0;
	virtual ECRESULT DeleteAttachmentInstance(ULONG ulInstanceId, bool bReplace) = 0;
	virtual ECRESULT GetSizeInstance(ULONG ulInstanceId, size_t *lpulSize, bool *lpbCompressed = NULL) = 0;

private:
	/* Add reference between Object and Single Instance */
	ECRESULT SetSingleInstanceId(ULONG ulObjId, ULONG ulInstanceId, ULONG ulTag);

	/* Count the number of times an attachment is referenced */
	ECRESULT IsOrphanedSingleInstance(ULONG ulInstanceId, bool *bOrphan);
	ECRESULT GetOrphanedSingleInstances(const std::list<ULONG> &lstInstanceIds, std::list<ULONG> *lplstOrphanedInstanceIds);

	ECRESULT DeleteAttachment(ULONG ulObjId, ULONG ulPropId, bool bReplace);

protected:
	ECDatabase *m_lpDatabase;
	bool m_bFileCompression;
	std::string m_CompressionLevel;
	ULONG m_ulRef;
	pthread_mutex_t m_refcnt_lock;
};

class ECDatabaseAttachment _zcp_final : public ECAttachmentStorage {
public:
	ECDatabaseAttachment(ECDatabase *lpDatabase);

protected:
	/* Single Instance Attachment handlers */
	virtual bool ExistAttachmentInstance(ULONG ulInstanceId);
	virtual ECRESULT LoadAttachmentInstance(struct soap *soap, ULONG ulInstanceId, size_t *lpiSize, unsigned char **lppData);
	virtual ECRESULT LoadAttachmentInstance(ULONG ulInstanceId, size_t *lpiSize, ECSerializer *lpSink);
	virtual ECRESULT SaveAttachmentInstance(ULONG ulInstanceId, ULONG ulPropId, size_t iSize, unsigned char *lpData);
	virtual ECRESULT SaveAttachmentInstance(ULONG ulInstanceId, ULONG ulPropId, size_t iSize, ECSerializer *lpSource);
	virtual ECRESULT DeleteAttachmentInstances(const std::list<ULONG> &lstDeleteInstances, bool bReplace);
	virtual ECRESULT DeleteAttachmentInstance(ULONG ulInstanceId, bool bReplace);
	virtual ECRESULT GetSizeInstance(ULONG ulInstanceId, size_t *lpulSize, bool *lpbCompressed = NULL);

	virtual ECRESULT Begin();
	virtual ECRESULT Commit();
	virtual ECRESULT Rollback();
};

class ECFileAttachment _zcp_final : public ECAttachmentStorage {
public:
	ECFileAttachment(ECDatabase *lpDatabase, const std::string &basepath, unsigned int ulCompressionLevel, const bool force_changes_to_disk);

protected:
	virtual ~ECFileAttachment();
	
	/* Single Instance Attachment handlers */
	virtual bool ExistAttachmentInstance(ULONG ulInstanceId);
	virtual ECRESULT LoadAttachmentInstance(struct soap *soap, ULONG ulInstanceId, size_t *lpiSize, unsigned char **lppData);
	virtual ECRESULT LoadAttachmentInstance(ULONG ulObjId, size_t *lpiSize, ECSerializer *lpSink);
	virtual ECRESULT SaveAttachmentInstance(ULONG ulInstanceId, ULONG ulPropId, size_t iSize, unsigned char *lpData);
	virtual ECRESULT SaveAttachmentInstance(ULONG ulObjId, ULONG ulPropId, size_t iSize, ECSerializer *lpSource);
	virtual ECRESULT DeleteAttachmentInstances(const std::list<ULONG> &lstDeleteInstances, bool bReplace);
	virtual ECRESULT DeleteAttachmentInstance(ULONG ulInstanceId, bool bReplace);
	virtual ECRESULT GetSizeInstance(ULONG ulInstanceId, size_t *lpulSize, bool *lpbCompressed = NULL);

	virtual ECRESULT Begin();
	virtual ECRESULT Commit();
	virtual ECRESULT Rollback();

private:
	std::string CreateAttachmentFilename(ULONG ulInstanceId, bool bCompressed);

	size_t attachment_size_safety_limit;
	int m_dirFd;
	DIR *m_dirp;
	bool force_changes_to_disk;

	/* helper functions for transacted deletion */
	ECRESULT MarkAttachmentForDeletion(ULONG ulInstanceId);
	ECRESULT DeleteMarkedAttachment(ULONG ulInstanceId);
	ECRESULT RestoreMarkedAttachment(ULONG ulInstanceId);

	bool VerifyInstanceSize(const ULONG ulInstanceId, const size_t expectedSize, const std::string & filename);

	std::string m_basepath;
	bool m_bTransaction;
	std::set<ULONG> m_setNewAttachment;
	std::set<ULONG> m_setDeletedAttachment;
	std::set<ULONG> m_setMarkedAttachment;
};

#endif
