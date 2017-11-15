/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef EC_ATTACHMENT_STORAGE
#define EC_ATTACHMENT_STORAGE

#include <kopano/zcdefs.h>
#include "ECDatabase.h"
#include <atomic>
#include <list>
#include <set>
#include <string>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

struct soap;

namespace KC {

class ECAttachmentStorage;
class ECSerializer;
class ECLogger;

class ext_siid {
	public:
	ext_siid() = default;
	explicit ext_siid(unsigned int a, std::string f = "") : siid(a), filename(f) {}
	unsigned int siid = 0;
	std::string filename;
	inline bool operator==(const ext_siid &o) const { return siid == o.siid; }
	inline bool operator<(const ext_siid &r) const { return siid < r.siid; }
};

class ECAttachmentConfig {
	public:
	virtual ~ECAttachmentConfig() = default;
	static ECRESULT create(ECConfig *, ECAttachmentConfig **);
	virtual ECAttachmentStorage *new_handle(ECDatabase *) = 0;

	private:
	virtual ECRESULT init(ECConfig *) { return hrSuccess; }
};

class ECAttachmentStorage : public kt_completion {
public:
	ECAttachmentStorage(ECDatabase *lpDatabase, unsigned int ulCompressionLevel);
	virtual ~ECAttachmentStorage() = default;

	/* Single Instance Attachment wrappers (should not be overridden by subclasses) */
	bool ExistAttachment(ULONG ulObjId, ULONG ulPropId);
	bool ExistAttachmentInstance(ULONG);
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
	ECRESULT GetSingleInstanceId(ULONG ulObjId, ULONG ulPropId, ext_siid *);
	ECRESULT GetSingleInstanceIds(const std::list<ULONG> &lstObjIds, std::list<ext_siid> *);

	/* Request parents for a particular Single Instance Id */
	ECRESULT GetSingleInstanceParents(ULONG ulInstanceId, std::list<ext_siid> *);

	/* Single Instance Attachment handlers (must be overridden by subclasses) */
	virtual kd_trans Begin(ECRESULT &) = 0;
protected:
	/* Single Instance Attachment handlers (must be overridden by subclasses) */
	virtual ECRESULT LoadAttachmentInstance(struct soap *, const ext_siid &, size_t *size, unsigned char **data) = 0;
	virtual ECRESULT LoadAttachmentInstance(const ext_siid &, size_t *size, ECSerializer *sink) = 0;
	virtual ECRESULT SaveAttachmentInstance(const ext_siid &, ULONG propid, size_t, unsigned char *) = 0;
	virtual ECRESULT SaveAttachmentInstance(const ext_siid &, ULONG propid, size_t, ECSerializer *) = 0;
	virtual ECRESULT DeleteAttachmentInstances(const std::list<ext_siid> &, bool replace) = 0;
	virtual ECRESULT DeleteAttachmentInstance(const ext_siid &, bool replace) = 0;
	virtual ECRESULT GetSizeInstance(const ext_siid &, size_t *size, bool *comp = nullptr) = 0;

private:
	/* Count the number of times an attachment is referenced */
	ECRESULT IsOrphanedSingleInstance(const ext_siid &, bool *orphan);
	ECRESULT GetOrphanedSingleInstances(const std::list<ext_siid> &ins, std::list<ext_siid> *orps);
	ECRESULT DeleteAttachment(ULONG ulObjId, ULONG ulPropId, bool bReplace);

protected:
	ECDatabase *m_lpDatabase;
	bool m_bFileCompression;
	std::string m_CompressionLevel;
};

class _kc_export_dycast ECDatabaseAttachment _kc_final :
    public ECAttachmentStorage {
public:
	ECDatabaseAttachment(ECDatabase *lpDatabase);

protected:
	/* Single Instance Attachment handlers */
	virtual ECRESULT LoadAttachmentInstance(struct soap *soap, const ext_siid &, size_t *size, unsigned char **data) override;
	virtual ECRESULT LoadAttachmentInstance(const ext_siid &, size_t *size, ECSerializer *sink) override;
	virtual ECRESULT SaveAttachmentInstance(const ext_siid &, ULONG propid, size_t, unsigned char *) override;
	virtual ECRESULT SaveAttachmentInstance(const ext_siid &, ULONG propid, size_t, ECSerializer *) override;
	virtual ECRESULT DeleteAttachmentInstances(const std::list<ext_siid> &, bool replace) override;
	virtual ECRESULT DeleteAttachmentInstance(const ext_siid &, bool replace) override;
	virtual ECRESULT GetSizeInstance(const ext_siid &, size_t *size, bool *comp = nullptr) override;
	virtual kd_trans Begin(ECRESULT &) override;

	private:
	virtual ECRESULT Commit() override;
	virtual ECRESULT Rollback() override;
};

class ECFileAttachment _kc_final :
    public ECAttachmentStorage {
	public:
	_kc_hidden ECFileAttachment(ECDatabase *, const std::string &basepath, unsigned int compr_lvl, bool sync);

protected:
	_kc_hidden virtual ~ECFileAttachment(void);

	/* Single Instance Attachment handlers */
	_kc_hidden virtual ECRESULT LoadAttachmentInstance(struct soap *, const ext_siid &, size_t *, unsigned char **) override;
	_kc_hidden virtual ECRESULT LoadAttachmentInstance(const ext_siid &, size_t *, ECSerializer *) override;
	_kc_hidden virtual ECRESULT SaveAttachmentInstance(const ext_siid &, ULONG propid, size_t, unsigned char *) override;
	_kc_hidden virtual ECRESULT SaveAttachmentInstance(const ext_siid &, ULONG propid, size_t, ECSerializer *) override;
	_kc_hidden virtual ECRESULT DeleteAttachmentInstances(const std::list<ext_siid> &, bool replace) override;
	_kc_hidden virtual ECRESULT DeleteAttachmentInstance(const ext_siid &, bool replace) override;
	_kc_hidden virtual ECRESULT GetSizeInstance(const ext_siid &, size_t *size, bool *compr = nullptr) override;
	_kc_hidden virtual kd_trans Begin(ECRESULT &) override;
private:
	_kc_hidden std::string CreateAttachmentFilename(const ext_siid &, bool compressed);
	virtual ECRESULT Commit() override;
	virtual ECRESULT Rollback() override;

	size_t attachment_size_safety_limit;
	int m_dirFd = -1;
	DIR *m_dirp = nullptr;
	bool force_changes_to_disk;

	/* helper functions for transacted deletion */
	_kc_hidden ECRESULT MarkAttachmentForDeletion(const ext_siid &);
	_kc_hidden ECRESULT DeleteMarkedAttachment(const ext_siid &);
	_kc_hidden ECRESULT RestoreMarkedAttachment(const ext_siid &);
	_kc_hidden bool VerifyInstanceSize(const ext_siid &, size_t expected_size, const std::string &filename);
	_kc_hidden void give_filesize_hint(const int fd, const off_t len);
	_kc_hidden void my_readahead(int fd);
	std::string m_basepath;
	bool m_bTransaction = false;
	std::set<ext_siid> m_setNewAttachment, m_setDeletedAttachment, m_setMarkedAttachment;
};

} /* namespace */

#endif
