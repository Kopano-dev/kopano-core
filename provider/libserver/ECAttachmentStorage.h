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
#include <atomic>
#include <list>
#include <set>
#include <string>
#include <dirent.h>
#include <sys/types.h>

struct soap;

namespace KC {

class ECSerializer;
class ECLogger;

class ext_siid {
	public:
	ext_siid() = default;
	explicit ext_siid(unsigned int a) : siid(a) {}
	unsigned int siid = 0;
	inline constexpr bool operator==(const ext_siid &o) const { return siid == o.siid; }
	inline constexpr bool operator<(const ext_siid &r) const { return siid < r.siid; }
};

class ECAttachmentStorage : public kt_completion {
public:
	ECAttachmentStorage(ECDatabase *lpDatabase, unsigned int ulCompressionLevel);

	ULONG AddRef();
	ULONG Release();

	static ECRESULT CreateAttachmentStorage(ECDatabase *lpDatabase, ECConfig *lpConfig, ECAttachmentStorage **lppAttachmentStorage);

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
	virtual bool ExistAttachmentInstance(const ext_siid &) = 0;
	virtual kd_trans Begin(ECRESULT &) = 0;
protected:
	virtual ~ECAttachmentStorage(void) = default;
	
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
	std::atomic<unsigned int> m_ulRef{0};
};

class _kc_export_dycast ECDatabaseAttachment _kc_final :
    public ECAttachmentStorage {
public:
	ECDatabaseAttachment(ECDatabase *lpDatabase);

protected:
	/* Single Instance Attachment handlers */
	virtual bool ExistAttachmentInstance(const ext_siid &) override;
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
	_kc_hidden virtual bool ExistAttachmentInstance(const ext_siid &) override;
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

	std::string m_basepath;
	bool m_bTransaction = false;
	std::set<ext_siid> m_setNewAttachment, m_setDeletedAttachment, m_setMarkedAttachment;
};

} /* namespace */

#endif
