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

struct soap;

namespace KC {

class ECSerializer;
class ECLogger;
struct s3_cd;

class ECS3Attachment _kc_final : public ECAttachmentStorage {
	public:
	static ECRESULT StaticInit(ECConfig *);
	static ECRESULT StaticDeinit(void);
	ECS3Attachment(ECDatabase *, const char *, const char *, const char *, const char *, const char *, const char *, const char *, unsigned int);

	protected:
	virtual ~ECS3Attachment(void);

	/* Single Instance Attachment handlers */
	virtual bool ExistAttachmentInstance(const ext_siid &) override;
	virtual ECRESULT LoadAttachmentInstance(struct soap *, const ext_siid &, size_t *, unsigned char **) override;
	virtual ECRESULT LoadAttachmentInstance(const ext_siid &, size_t *, ECSerializer *) override;
	virtual ECRESULT SaveAttachmentInstance(const ext_siid &, ULONG, size_t, unsigned char *) override;
	virtual ECRESULT SaveAttachmentInstance(const ext_siid &, ULONG, size_t, ECSerializer *) override;
	virtual ECRESULT DeleteAttachmentInstances(const std::list<ext_siid> &, bool replace) override;
	virtual ECRESULT DeleteAttachmentInstance(const ext_siid &, bool replace) override;
	virtual ECRESULT GetSizeInstance(const ext_siid &, size_t *, bool * = nullptr) override;
	virtual kd_trans Begin(ECRESULT &) override;

	private:
	static S3Status response_prop_cb(const S3ResponseProperties *, void *);
	static void response_complete_cb(S3Status, const S3ErrorDetails *, void *);
	static S3Status get_obj_cb(int, const char *, void *);
	static int put_obj_cb(int, char *, void *);

	S3Status response_prop(const S3ResponseProperties *, void *);
	void response_complete(S3Status, const S3ErrorDetails *, void *);
	S3Status get_obj(int, const char *, void *);
	int put_obj(int, char *, void *);
	std::string make_att_filename(const ext_siid &, bool);
	bool should_retry(struct s3_cd &);
	struct s3_cd create_cd(void);
	ECRESULT del_marked_att(const ext_siid &);
	virtual ECRESULT Commit() override;
	virtual ECRESULT Rollback() override;

	/* Variables: */
	std::string m_basepath;
	S3BucketContext m_bucket_ctx;
	std::set<ext_siid> m_new_att, m_marked_att;
	bool m_transact = false;

	/*
	 * The Request Context and Response Handler variables are responsible
	 * for the operation of the handler functions.
	 */
	S3ResponseHandler m_response_handler;
	S3PutObjectHandler m_put_obj_handler;
	S3GetObjectHandler m_get_obj_handler;
	S3GetConditions m_get_conditions;
};

} /* namespace */

#endif /* LIBS3_H */
#endif
