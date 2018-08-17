#ifndef EC_S3_ATTACHMENT
#define EC_S3_ATTACHMENT

#include "config.h"
#ifdef HAVE_LIBS3_H
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <libs3.h>
#include "ECAttachmentStorage.h"

struct soap;

namespace KC {

class ECSerializer;
class ECLogger;
struct s3_cd;

struct s3_cache_entry {
	KC::time_point valid_until;
	size_t size;
};

class ECS3Config final : public ECAttachmentConfig {
	public:
	virtual ~ECS3Config();
	virtual ECRESULT init(std::shared_ptr<ECConfig>) override;
	virtual ECAttachmentStorage *new_handle(ECDatabase *) override;

	private:
	std::string m_akid, m_sakey, m_bkname, m_region, m_path;
	unsigned int m_comp;

	void *m_handle = nullptr;
#define W(n) decltype(S3_ ## n) *DY_ ## n;
	W(put_object)
	W(initialize)
	W(status_is_retryable)
	W(deinitialize)
	W(get_status_name)
	W(head_object)
	W(delete_object)
	W(get_object)
#undef W

	S3BucketContext m_bkctx{};
	/*
	 * The Request Context and Response Handler variables are responsible
	 * for the operation of the handler functions.
	 */
	S3ResponseHandler m_response_handler{};
	S3PutObjectHandler m_put_obj_handler{};
	S3GetObjectHandler m_get_obj_handler{};
	S3GetConditions m_get_conditions{};
	std::mutex m_cachelock;
	std::map<ULONG, s3_cache_entry> m_cache;

	friend class ECS3Attachment;
};

class ECS3Attachment _kc_final : public ECAttachmentStorage {
	public:
	ECS3Attachment(ECS3Config &, ECDatabase *);

	protected:
	virtual ~ECS3Attachment(void);

	/* Single Instance Attachment handlers */
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
	ECS3Config &m_config;
	std::set<ext_siid> m_new_att, m_marked_att;
	bool m_transact = false;

	friend class ECS3Config;
};

} /* namespace */

#endif /* LIBS3_H */
#endif
