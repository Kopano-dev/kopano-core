#ifndef EC_S3_ATTACHMENT
#define EC_S3_ATTACHMENT

#include "config.h"
#ifdef HAVE_LIBS3_H
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <list>
#include <set>
#include <string>
#include <libs3.h>
#include "ECAttachmentStorage.h"

class ECSerializer;
class ECLogger;
struct s3_cd;

class ECS3Attachment _zcp_final : public ECAttachmentStorage {
	public:
	static ECRESULT StaticInit(ECConfig *);
	static ECRESULT StaticDeinit(void);
	ECS3Attachment(ECDatabase *, const char *, const char *, const char *, const char *, const char *, const char *, unsigned int);

	protected:
	virtual ~ECS3Attachment(void);

	/* Single Instance Attachment handlers */
	virtual bool ExistAttachmentInstance(ULONG);
	virtual ECRESULT LoadAttachmentInstance(struct soap *, ULONG, size_t *, unsigned char **);
	virtual ECRESULT LoadAttachmentInstance(ULONG, size_t *, ECSerializer *);
	virtual ECRESULT SaveAttachmentInstance(ULONG, ULONG, size_t, unsigned char *);
	virtual ECRESULT SaveAttachmentInstance(ULONG, ULONG, size_t, ECSerializer *);
	virtual ECRESULT DeleteAttachmentInstances(const std::list<ULONG> &, bool);
	virtual ECRESULT DeleteAttachmentInstance(ULONG, bool);
	virtual ECRESULT GetSizeInstance(ULONG, size_t *, bool * = NULL);

	virtual ECRESULT Begin(void);
	virtual ECRESULT Commit(void);
	virtual ECRESULT Rollback(void);

	private:
	static S3Status response_prop_cb(const S3ResponseProperties *, void *);
	static void response_complete_cb(S3Status, const S3ErrorDetails *, void *);
	static S3Status get_obj_cb(int, const char *, void *);
	static int put_obj_cb(int, char *, void *);

	S3Status response_prop(const S3ResponseProperties *, void *);
	void response_complete(S3Status, const S3ErrorDetails *, void *);
	S3Status get_obj(int, const char *, void *);
	int put_obj(int, char *, void *);

	std::string make_att_filename(ULONG, bool);
	bool should_retry(struct s3_cd *);
	struct s3_cd create_cd(void);

	/* helper functions for transacted deletion */
	ECRESULT mark_att_for_del(ULONG);
	ECRESULT del_marked_att(ULONG);
	ECRESULT restore_marked_att(ULONG);

	/* Variables: */
	std::string m_basepath;
	S3BucketContext m_bucket_ctx;
	std::set<ULONG> m_new_att;
	std::set<ULONG> m_deleted_att;
	std::set<ULONG> m_marked_att;
	bool m_transact;

	/*
	 * The Request Context and Response Handler variables are responsible
	 * for the operation of the handler functions.
	 */
	S3ResponseHandler m_response_handler;
	S3PutObjectHandler m_put_obj_handler;
	S3GetObjectHandler m_get_obj_handler;
	S3GetConditions m_get_conditions;
};

#endif /* LIBS3_H */
#endif
