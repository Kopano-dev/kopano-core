#ifndef EC_S3_ATTACHMENT
#define EC_S3_ATTACHMENT

#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#ifdef HAVE_LIBS3_H
#include <kopano/platform.h>
#include <map>
#include <mutex>
#include <string>
#include <libs3.h>
#include <kopano/timeutil.hpp>
#include "ECAttachmentStorage.h"

struct soap;

namespace KC {

struct s3_cache_entry {
	KC::time_point valid_until;
	size_t size;
};

class ECS3Attachment;

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

} /* namespace */

#endif /* LIBS3_H */
#endif
