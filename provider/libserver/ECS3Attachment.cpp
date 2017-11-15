#include "config.h"
#ifdef HAVE_LIBS3_H
#include <kopano/platform.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <stdexcept>
#include <cerrno>
#include <unistd.h>
#include <zlib.h>
#include <mapidefs.h>
#include <mapitags.h>
#include <kopano/MAPIErrors.h>
#include <kopano/stringutil.h>
#include "../../common/ECSerializer.h"
#include "../common/SOAPUtils.h"
#include "ECAttachmentStorage.h"
#include "ECS3Attachment.h"
#include "StreamUtil.h"

using steady_clock = std::chrono::steady_clock;

namespace KC {

/* Number of times the server should retry to send the command to the S3 servers, this is required to process redirects. */
#define S3_RETRIES 5

/* Number of seconds to sleep before trying again */
#define S3_SLEEP_DELAY 1

#define now_positive() (std::chrono::steady_clock::now() + std::chrono::seconds(600))
#define now_negative() (std::chrono::steady_clock::now() + std::chrono::seconds(60))

/* callback data */
struct s3_cd {
	struct soap *soap = nullptr;
	unsigned char *data = nullptr;
	ECSerializer *sink = nullptr;
	bool alloc_data = false;
	size_t size = 0, processed = 0;
	int retries = 0;
	S3Status status = S3StatusOK;
};

/* callback data wrapper */
struct s3_cdw {
	ECS3Attachment *caller;
	void *cbdata;
};

struct s3_cache_entry {
	KC::time_point valid_until;
	size_t size;
};
#define S3_NEGATIVE_ENTRY SIZE_MAX

static void *ec_libs3_handle;
#define W(n) static decltype(S3_ ## n) *DY_ ## n;
W(put_object)
W(initialize)
W(status_is_retryable)
W(deinitialize)
W(get_status_name)
W(head_object)
W(delete_object)
W(get_object)
#undef W

/* This ought to be moved into ECS3Attachment, if and when that becomes a singleton. */
static std::mutex m_cachelock;
static std::map<ULONG, s3_cache_entry> m_cache;

/**
 * Static function used to forward the response properties callback to the
 * right object.
 *
 * @param properties the response properties of the S3 request.
 * @param cbdata contains the callback pointer to the ECS3Attachment instance and its callback data.
 *
 * @return The status of the response properties callback function.
 */
S3Status ECS3Attachment::response_prop_cb(const S3ResponseProperties *properties, void *cbdata)
{
	auto data = static_cast<struct s3_cdw *>(cbdata);
	return data->caller->response_prop(properties, data->cbdata);
}

/**
 * Static function used to forward the callback to the right object.
 *
 * @param status the S3 resonse code of our request
 * @param errorDetails can be NULL, otherwise it contains details on the error that occurred.
 * @param cbdata contains the callback pointer to the ECS3Attachment instance and its callback data.
 */
void ECS3Attachment::response_complete_cb(S3Status status,
    const S3ErrorDetails *error, void *cbdata)
{
	auto data = static_cast<struct s3_cdw *>(cbdata);
	return data->caller->response_complete(status, error, data->cbdata);
}

/**
 * Static function used to forward the callback to the right object.
 *
 * @param bufferSize the size of the buffer which we can read from
 * @param buffer is the buffer reference that we need to read from
 * @param cbdata contains the callback pointer to the ECS3Attachment instance and its callback data.
 *
 * @return The status of the reading more data from the buffer.
 */
S3Status ECS3Attachment::get_obj_cb(int bufferSize, const char *buffer, void *cbdata)
{
	auto data = static_cast<struct s3_cdw *>(cbdata);
	return data->caller->get_obj(bufferSize, buffer, data->cbdata);
}

/**
 * Static function used to forward the callback to the right object.
 *
 * @param bufferSize the size of the buffer which we can write to
 * @param buffer is the buffer reference that we need to write to
 * @param cbdata contains the callback pointer to the ECS3Attachment instance and its callback data.
 *
 * @return the number of bytes that have been written to the buffer.
 */
int ECS3Attachment::put_obj_cb(int bufferSize, char *buffer, void *cbdata)
{
	auto data = static_cast<struct s3_cdw *>(cbdata);
	return data->caller->put_obj(bufferSize, buffer, data->cbdata);
}

ECRESULT ECS3Attachment::StaticInit(ECConfig *cf)
{
	ec_log_info("S3: initializing attachment storage");
	/*
	 * Do a dlopen of libs3.so.4 so that the implicit pull-in of
	 * libldap-2.4.so.2 symbols does not pollute our namespace of
	 * libldap_r-2.4.so.2 symbols.
	 */
	void *h = ec_libs3_handle = dlopen("libs3.so.4", RTLD_LAZY | RTLD_LOCAL);
	const char *err;
	if (ec_libs3_handle == NULL) {
		ec_log_warn("dlopen libs3.so.4: %s", (err = dlerror()) ? err : "<none>");
		return KCERR_DATABASE_ERROR;
	}
#define W(n) do { \
		DY_ ## n = reinterpret_cast<decltype(DY_ ## n)>(dlsym(h, "S3_" #n)); \
		if (DY_ ## n == NULL) { \
			ec_log_warn("dlsym S3_" #n ": %s", (err = dlerror()) ? err : "<none>"); \
			dlclose(h); \
			ec_libs3_handle = NULL; \
			return KCERR_DATABASE_ERROR; \
		} \
	} while (false)

	W(put_object);
	W(initialize);
	W(status_is_retryable);
	W(deinitialize);
	W(get_status_name);
	W(head_object);
	W(delete_object);
	W(get_object);
#undef W

	S3Status status = DY_initialize("Kopano Mail", S3_INIT_ALL, cf->GetSetting("attachment_s3_hostname"));
	if (status != S3StatusOK) {
		ec_log_err("S3: error while initializing attachment storage: %s",
			DY_get_status_name(status));
		return KCERR_NETWORK_ERROR;
	}
	return erSuccess;
}

ECRESULT ECS3Attachment::StaticDeinit(void)
{
	ec_log_info("S3: deinitializing attachment storage");
	/* Deinitialize the S3 storage environment */
	if (ec_libs3_handle != NULL) {
		DY_deinitialize();
		dlclose(ec_libs3_handle);
	}
	return erSuccess;
}

/*
 * Locking requirements of ECAttachmentStorage: In the case of
 * ECAttachmentStorage locking to protect against concurrent access is futile.
 * The theory is as follows:
 *
 * If two users have a reference to the same attachment, neither can delete the
 * mail causing the other person to lose the attachment. This means that
 * concurrent copy and delete actions are possible on the same attachment. The
 * only case where this does not hold is the case when a user deletes the mail
 * he is copying at the same time and that this is the last mail which
 * references that specific attachment.
 *
 * The only race condition which might occur is that the dagent delivers a mail
 * with attachment, the server returns the attachment id back to the dagent,
 * but the user for whom the message was intended deletes the mail &
 * attachment. In this case the dagent will no longer send the attachment but
 * only the attachment id. When that happens the server can return an error and
 * simply request the dagent to resend the attachment and to obtain a new
 * attachment id.
 */

/**
 * The ECS3Attachment Storage engine is used to store attachments as separate
 * files in the S3 storage cluster. This is useful to ensure you have enough
 * storage capacity on your servers and can be used to cluster multiple Kopano
 * servers together, allowing each of them to access the same storage for the
 * attachments.
 *
 * @param database The database connection of the Kopano Server
 * @param protocol sets the type of protocol that should be used to connect to the Amazon S3 cluster.
 *		   The options are http or https, of which https is the preferred option.
 * @param uriStyle The uri style of the bucket, allowed options are virtualhost or path,
 * @param accessKeyId The access key of your Amazon account for S3 storage.
 * @param secretAccessKey The secret access key of your Amazon account for S3 storage.
 * @param bucketName The name of the bucket in which you want to store all the attachments.
 * @param basepath In order to use the same bucket with different Kopano clusters, you can select
 *		   different basepaths for each cluster, this works like a separate directory where
 *		   all the files are stored in. NOTE: Use the same basepath for all the servers in
 *		   a single server cluster.
 * @param ulCompressionLevel the compression level used to gzip the attachment data.
 */
ECS3Attachment::ECS3Attachment(ECDatabase *database, const char *protocol,
    const char *uri_style, const char *access_key_id,
    const char *secret_access_key, const char *bucket_name, const char *region,
    const char *basepath, unsigned int complvl) :
	ECAttachmentStorage(database, complvl), m_basepath(basepath)
{
	memset(&m_bucket_ctx, 0, sizeof(m_bucket_ctx));
	m_bucket_ctx.bucketName = bucket_name;
	m_bucket_ctx.protocol = strncmp(protocol, "https", 5) == 0 ? S3ProtocolHTTPS : S3ProtocolHTTP;
	m_bucket_ctx.uriStyle = strncmp(uri_style, "path", 4) == 0 ? S3UriStylePath : S3UriStyleVirtualHost;
	m_bucket_ctx.accessKeyId = access_key_id;
	m_bucket_ctx.secretAccessKey = secret_access_key;
	m_bucket_ctx.authRegion = region;

	/* Set the handlers */
	m_response_handler.propertiesCallback = &ECS3Attachment::response_prop_cb;
	m_response_handler.completeCallback = &ECS3Attachment::response_complete_cb;
	m_put_obj_handler.responseHandler = m_response_handler;
	m_put_obj_handler.putObjectDataCallback = &ECS3Attachment::put_obj_cb;
	m_get_obj_handler.responseHandler = m_response_handler;
	m_get_obj_handler.getObjectDataCallback = &ECS3Attachment::get_obj_cb;
	m_get_conditions.ifModifiedSince = -1;
	m_get_conditions.ifNotModifiedSince = -1;
	m_get_conditions.ifMatchETag = NULL;
	m_get_conditions.ifNotMatchETag = NULL;
}

ECS3Attachment::~ECS3Attachment(void)
{
	assert(!m_transact);
}

/**
 * As soon as the response properties are returned by S3, this callback will be
 * called to process them.
 *
 * @param properties the properties object that contains details on the request
 * @param cbdata the callback data reference that was given at the time the request fired.
 *
 * @return the status of processing the information. Generally, this will
 * return S3StatusOK, however, if it should allocate memory for the data
 * buffer, and fails to do so, it will return with S3StatusAbortedByCallback.
 */
S3Status ECS3Attachment::response_prop(const S3ResponseProperties *properties, void *cbdata)
{
	auto data = static_cast<struct s3_cd *>(cbdata);

	if (properties->contentLength != 0) {
		data->size = properties->contentLength;
		ec_log_debug("S3: received the response properties, content length: %zu", data->size);
	} else {
		ec_log_debug("S3: received the response properties");
	}
	/*
	 * Only allocate memory if we are not able to use a serializer sink, we
	 * are instructed to alloc data->data and have not allocated it yet.
	 */
	if (data->sink == NULL && data->alloc_data && data->data == NULL) {
		data->data = s_alloc_nothrow<unsigned char>(data->soap, data->size);
		if (data->data == NULL) {
			ec_log_err("S3: cannot allocate %zu bytes", data->size);
			return S3StatusAbortedByCallback;
		}
	}
	return S3StatusOK;
}

/**
 * Once the request has been processed by S3 and we received all the data, this
 * function will be called to react accordingly.
 *
 * @param status the S3 resonse code of our request
 * @param errorDetails can be NULL, otherwise it contains details on the error that occurred.
 * @param cbdata contains the callback pointer to the ECS3Attachment instance and its callback data.
 *
 * @return The status of the response properties callback function.
 */
void ECS3Attachment::response_complete(S3Status status,
    const S3ErrorDetails *error, void *cbdata)
{
	auto data = static_cast<struct s3_cd *>(cbdata);
	data->status = status;

	ec_log_debug("S3: response completed: %s.", DY_get_status_name(status));
	if (status == S3StatusOK)
		return;
	if (error == 0) {
		ec_log_err("S3: Amazon return status %s", DY_get_status_name(status));
		return;
	}
	ec_log_err("S3: Amazon return status %s, error: %s, resource: \"%s\"",
		DY_get_status_name(status),
		error->message ? error->message : "<unknown>",
		error->resource ? error->resource : "<none>");
	if (error->furtherDetails != NULL)
		ec_log_err("S3: Amazon error details: %s", error->furtherDetails);
	for (ssize_t i = 0; i < error->extraDetailsCount; ++i)
		ec_log_err("S3: Amazon error details: %s: %s",
			error->extraDetails[i].name, error->extraDetails[i].value);
}

/**
 * This callback function will read more data from the buffer used to receive
 * data from S3.
 *
 * @param bufferSize the size of the buffer which we can read from
 * @param buffer is the buffer reference that we need to read from
 * @param cbdata contains the callback pointer to the ECS3Attachment instance and its callback data.
 *
 * @return The status of the reading more data from the buffer.
 */
S3Status ECS3Attachment::get_obj(int bufferSize, const char *buffer, void *cbdata)
{
	auto data = static_cast<struct s3_cd *>(cbdata);
	ECRESULT er;
	/*
	 * Check if we were able to acquire the memory. There are two cases
	 * where this could go wrong: Either we ran out of memory, in which
	 * case we should never have reached this call, so cancel it. Or S3 did
	 * not return the properties of the object, and therefore we have not
	 * allocated the memory yet, in this case we should abort too.
	 */
	if (data->data == NULL && data->sink == NULL)
		return S3StatusAbortedByCallback;
	/*
	 * Check if we are not trying to write outside the acquired memory
	 * scope, if so abort.
	 */
	if (data->processed + bufferSize > data->size)
		return S3StatusAbortedByCallback;

	ec_log_debug("S3: Getting bytes from callback: Remaining bytes to get: %zu. Reading %d bytes",
		data->size - data->processed, bufferSize);
	if (data->sink != NULL) {
		er = data->sink->Write(buffer, 1, bufferSize);
		if (er != erSuccess) {
			ec_log_err("S3: unable to write to serializer sink: %s (0x%x)",
				GetMAPIErrorMessage(kcerr_to_mapierr(er)), er);
			return S3StatusAbortedByCallback;
		}
	} else {
		memcpy(data->data + data->processed, buffer, bufferSize);
	}
	data->processed += bufferSize;
	return S3StatusOK;
}

/**
 * This callback function will write more data from the buffer used to write
 * data to S3.
 *
 * @param bufferSize the size of the buffer which we can write to
 * @param buffer is the buffer reference that we need to write to
 * @param cbdata contains the callback pointer to the ECS3Attachment instance and its callback data.
 *
 * @return the number of bytes that have been written to the buffer.
 */
int ECS3Attachment::put_obj(int bufferSize, char *buffer, void *cbdata)
{
	auto data = static_cast<struct s3_cd *>(cbdata);
	ECRESULT ret;
	int toRead = 0, remaining = 0;

	/*
	 * Check if we have a data buffer or serializer to read from.
	 */
	if (data->data == NULL && data->sink == NULL)
		return -1;

	/* Check if we are not trying to write outside the acquired memory scope, if so abort. */
	if (data->processed > data->size)
		return -1;

	remaining = data->size - data->processed;
	if (remaining > 0) {
		toRead = remaining > bufferSize ? bufferSize : remaining;
		ec_log_debug("S3: Putting data using callback: "
			"Remaining bytes to put: %d - Writing %d bytes in %d buffer",
			remaining, toRead, bufferSize);

		if (data->sink != NULL) {
			ret = data->sink->Read(buffer, 1, toRead);
			if (ret != erSuccess) {
				ec_log_err("S3: Unable to read from the serializer sink");
				return -1;
			}
		} else {
			memcpy(buffer, data->data + data->processed, toRead);
		}
		data->processed += toRead;
	} else {
		ec_log_debug("S3: putting data using callback: Remaining bytes to put: %d - We processed all the data, but S3 expects more", remaining);
	}
	return toRead;
}

/**
 * For a given instance id, check if this has valid attachment data present.
 *
 * @param[in] ins_id instance id to check validity
 *
 * @return instance present
 */
bool ECS3Attachment::ExistAttachmentInstance(const ext_siid &ins_id)
{
	size_t ignored;
	return GetSizeInstance(ins_id, &ignored, nullptr) == hrSuccess;
}

/**
 * Load instance data using soap and return as blob.
 *
 * IMPORTANT: We allocated the data in referred to by data_p to store the
 * attachment. The caller of this function is responsible for freeing the
 * memory after its use.
 *
 * @param[in] soap soap to use memory allocations for
 * @param[in] ins_id InstanceID to load
 * @param[out] size_p size in data_p
 * @param[out] data_p data of instance
 *
 * @return Kopano error code
 */
ECRESULT ECS3Attachment::LoadAttachmentInstance(struct soap *soap,
    const ext_siid &ins_id, size_t *size_p, unsigned char **data_p)
{
	ECRESULT ret = KCERR_NOT_FOUND;
	struct s3_cd cd;
	cd.alloc_data = true;
	cd.soap = soap;
	struct s3_cdw cwdata;
	cwdata.caller = this;
	cwdata.cbdata = &cd;

	auto filename = make_att_filename(ins_id, false);
	auto fn = filename.c_str();
	ec_log_debug("S3: loading %s into buffer", fn);
	/*
	 * Loop at most S3_RETRIES times, to make sure that if the servers of S3
	 * reply with a redirect, we actually try again and process it.
	 */
	cd.retries = S3_RETRIES;
	do {
		DY_get_object(&m_bucket_ctx, fn, &m_get_conditions, 0, 0,
			nullptr, 0, &m_get_obj_handler, &cwdata);
		if (DY_status_is_retryable(cd.status))
			ec_log_debug("S3: load %s: retryable status: %s",
				fn, DY_get_status_name(cd.status));
	} while (DY_status_is_retryable(cd.status) && should_retry(cd));

	ec_log_debug("S3: load %s: %s", fn, DY_get_status_name(cd.status));
	if (cd.size != cd.processed) {
		ec_log_err("S3: load %s: short read %zu/%zu bytes",
			fn, cd.processed, cd.size);
		ret = KCERR_DATABASE_ERROR;
	} else if (cd.data == nullptr) {
		ret = KCERR_NOT_ENOUGH_MEMORY;
	} else if (cd.status != S3StatusOK) {
		ret = KCERR_NETWORK_ERROR;
	} else {
		/*
		 * We allocated the data in cd.data, which is referred to by
		 * data_p. The caller of this function is responsible for freeing the
		 * memory after its use.
		 */
		*size_p = cd.size;
		*data_p = cd.data;
		ret = erSuccess;
	}
	if (ret != erSuccess && cd.data != nullptr && soap == nullptr)
		delete cd.data;
	/*
	 * Make sure we clear the cd.data variable so we cannot write
	 * to it after it is freed externally.
	 */
	cd.data = nullptr;
	return ret;
}

/**
 * Load instance data using a serializer.
 *
 * @param[in] ins_id InstanceID to load
 * @param[out] size_p size written in in sink
 * @param[in] sink serializer to write in
 *
 * @return erSuccess if we were able to load the instance, or the error code
 * if we could not.
 */
ECRESULT ECS3Attachment::LoadAttachmentInstance(const ext_siid &ins_id,
    size_t *size_p, ECSerializer *sink)
{
	ECRESULT ret = KCERR_NOT_FOUND;
	struct s3_cd cd;
	cd.sink = sink;
	struct s3_cdw cwdata;
	cwdata.caller = this;
	cwdata.cbdata = &cd;

	auto filename = make_att_filename(ins_id, false);
	auto fn = filename.c_str();
	ec_log_debug("S3: loading %s into serializer", fn);
	/*
	 * Loop at most S3_RETRIES times, to make sure that if the servers of S3
	 * reply with a redirect, we actually try again and process it.
	 */
	cd.retries = S3_RETRIES;
	do {
		DY_get_object(&m_bucket_ctx, fn, &m_get_conditions, 0, 0,
			nullptr, 0, &m_get_obj_handler, &cwdata);
		if (DY_status_is_retryable(cd.status))
			ec_log_debug("S3: load %s: retryable status: %s",
				fn, DY_get_status_name(cd.status));
	} while (DY_status_is_retryable(cd.status) && should_retry(cd));

	ec_log_debug("S3: load %s: %s", fn, DY_get_status_name(cd.status));
	if (cd.size != cd.processed) {
		ec_log_err("S3: load %s: short read %zu/%zu bytes",
			fn, cd.processed, cd.size);
		ret = KCERR_DATABASE_ERROR;
	} else if (cd.data == nullptr) {
		ret = KCERR_NOT_ENOUGH_MEMORY;
	} else if (cd.status != S3StatusOK) {
		ret = erSuccess;
	} else {
		scoped_lock locker(m_cachelock);
		m_cache[ins_id.siid] = {now_positive(), cd.size};
		*size_p = cd.size;
	}
	/*
	 * Make sure we do not write to the sink accidentally, therefore reset
	 * it to NULL.
	 */
	cd.sink = nullptr;
	return ret;
}

/**
 * Save a property in a new instance from a blob
 *
 * @param[in] ins_id InstanceID to save data under
 * @param[in] propid unused, required by interface, see ECDatabaseAttachment
 * @param[in] size size of data
 * @param[in] data Data of property
 *
 * @return Kopano error code
 */
ECRESULT ECS3Attachment::SaveAttachmentInstance(const ext_siid &ins_id,
    ULONG propid, size_t size, unsigned char *data)
{
	ECRESULT ret = KCERR_NOT_FOUND;
	bool comp = false;
	struct s3_cd cd;
	cd.data = data;
	cd.size = size;
	struct s3_cdw cwdata;
	cwdata.caller = this;
	cwdata.cbdata = &cd;

	auto filename = make_att_filename(ins_id, comp && size != 0);
	auto fn = filename.c_str();
	ec_log_debug("S3: saving %s (buffer of %zu bytes)", fn, size);
	/*
	 * Loop at most S3_RETRIES times, to make sure that if the servers of S3
	 * reply with a redirect, we actually try again and process it.
	 */
	cd.retries = S3_RETRIES;
	do {
		DY_put_object(&m_bucket_ctx, fn, size, nullptr, nullptr, 0,
			&m_put_obj_handler, &cwdata);
		if (DY_status_is_retryable(cd.status))
			ec_log_debug("S3: save %s: retryable status: %s",
				fn, DY_get_status_name(cd.status));
	} while (DY_status_is_retryable(cd.status) && should_retry(cd));

	ec_log_debug("S3: save %s: %s", fn, DY_get_status_name(cd.status));
	/* set in transaction before disk full check to remove empty file */
	if (m_transact)
		m_new_att.emplace(ins_id);

	if (cd.size != cd.processed) {
		ec_log_err("S3: save %s: processed only %zu/%zu bytes",
			fn, cd.processed, cd.size);
		ret = KCERR_DATABASE_ERROR;
	} else if (cd.status == S3StatusOK) {
		scoped_lock locker(m_cachelock);
		m_cache[ins_id.siid] = {now_positive(), cd.size};
		ret = erSuccess;
	}
	cd.data = NULL;
	return ret;
}

/**
 * Save a property in a new instance from a serializer
 *
 * @param[in] ins_id InstanceID to save data under
 * @param[in] propid unused, required by interface, see ECDatabaseAttachment
 * @param[in] size size in source
 * @param[in] source serializer to read data from
 *
 * @return Kopano error code
 */
ECRESULT ECS3Attachment::SaveAttachmentInstance(const ext_siid &ins_id,
    ULONG propid, size_t size, ECSerializer *source)
{
	ECRESULT ret = KCERR_NOT_FOUND;
	bool comp = false;
	struct s3_cd cd;
	cd.sink = source;
	cd.size = size;
	struct s3_cdw cwdata;
	cwdata.caller = this;
	cwdata.cbdata = &cd;

	auto filename = make_att_filename(ins_id, comp && size != 0);
	auto fn = filename.c_str();
	ec_log_debug("S3: saving %s (serializer with %zu bytes)", fn, size);
	/*
	 * Loop at most S3_RETRIES times, to make sure that if the servers of S3
	 * reply with a redirect, we actually try again and process it.
	 */
	cd.retries = S3_RETRIES;
	do {
		DY_put_object(&m_bucket_ctx, fn, size, nullptr, nullptr, 0,
			&m_put_obj_handler, &cwdata);
		if (DY_status_is_retryable(cd.status))
			ec_log_debug("S3: save %s: retryable status: %s",
				fn, DY_get_status_name(cd.status));
	}
	while (DY_status_is_retryable(cd.status) && should_retry(cd));

	ec_log_debug("S3: save %s: %s", fn, DY_get_status_name(cd.status));
	/* set in transaction before disk full check to remove empty file */
	if (m_transact)
		m_new_att.emplace(ins_id);

	if (cd.size != cd.processed) {
		ec_log_err("S3: save %s: processed only %zu/%zu bytes",
			fn, cd.processed, cd.size);
		ret = KCERR_DATABASE_ERROR;
	} else if (cd.status == S3StatusOK) {
		scoped_lock locker(m_cachelock);
		m_cache[ins_id.siid] = {now_positive(), cd.size};
		ret = erSuccess;
	}
	cd.sink = NULL;
	return ret;
}

/**
 * Delete given instances from the filesystem
 *
 * @param[in] lstDeleteInstances List of instance ids to remove from the filesystem
 * @param[in] bReplace Transaction marker
 *
 * @return Kopano error code
 */
ECRESULT ECS3Attachment::DeleteAttachmentInstances(const std::list<ext_siid> &lstDeleteInstances, bool bReplace)
{
	ECRESULT ret = erSuccess;
	int errors = 0;
	for (const auto &del_id : lstDeleteInstances) {
		ret = this->DeleteAttachmentInstance(del_id, bReplace);
		if (ret != erSuccess)
			++errors;
	}
	return errors == 0 ? erSuccess : KCERR_DATABASE_ERROR;
}

/**
 * Delete a marked instance from the filesystem
 *
 * @param[in] ins_id instance id to remove
 *
 * @return Kopano error code
 */
ECRESULT ECS3Attachment::del_marked_att(const ext_siid &ins_id)
{
	struct s3_cd cd;
	struct s3_cdw cwdata;
	cwdata.caller = this;
	cwdata.cbdata = &cd;

	auto filename = make_att_filename(ins_id, false);
	auto fn = filename.c_str();
	ec_log_debug("S3: delete marked attachment: %s", fn);
	/*
	 * Loop at most S3_RETRIES times, to make sure that if the servers of
	 * S3 reply with a redirect, we actually try again and process it.
	 */
	cd.retries = S3_RETRIES;
	do {
		DY_delete_object(&m_bucket_ctx, fn, nullptr, 0,
			&m_response_handler, &cwdata);
		if (DY_status_is_retryable(cd.status))
			ec_log_debug("S3: delete %s: retryable status: %s",
				fn, DY_get_status_name(cd.status));
	} while (DY_status_is_retryable(cd.status) && should_retry(cd));

	ec_log_debug("S3: delete %s: %s", fn, DY_get_status_name(cd.status));
	if (cd.status == S3StatusOK || cd.status == S3StatusHttpErrorNotFound) {
		/* Delete successful, or did not exist before */
		scoped_lock locker(m_cachelock);
		m_cache[ins_id.siid] = {now_negative(), S3_NEGATIVE_ENTRY};
	}
	/* else { do not touch cache for network errors, etc. } */

	return cd.status == S3StatusOK ? erSuccess : KCERR_NOT_FOUND;
}

/**
 * Delete a single instanceid from the filesystem
 *
 * @param[in] ins_id instance id to remove
 * @param[in] bReplace Transaction marker
 *
 * @return
 */
ECRESULT ECS3Attachment::DeleteAttachmentInstance(const ext_siid &ins_id,
    bool bReplace)
{
	auto filename = make_att_filename(ins_id, m_bFileCompression);
	if (!m_transact)
		return del_marked_att(ins_id);
	ec_log_debug("S3: set delete mark for %u", ins_id.siid);
	m_marked_att.emplace(ins_id);
	return erSuccess;
}

/**
 * Return a filename for an instance id
 *
 * @param[in] ins_id instance id to convert to a filename
 * @param[in] bCompressed add compression marker to filename
 *
 * @return Kopano error code
 */
std::string ECS3Attachment::make_att_filename(const ext_siid &esid, bool comp)
{
	auto filename = m_basepath + PATH_SEPARATOR + stringify(esid.siid);
	if (comp)
		filename += ".gz";
	return filename;
}

/**
 * This function will check whether we should give it one more try or not.
 *
 * @return whether we should retry
 */
bool ECS3Attachment::should_retry(struct s3_cd &cd)
{
	if (cd.retries <= 0)
		return false;
	--cd.retries;
	sleep(S3_SLEEP_DELAY);
	return true;
}

/**
 * Return the size of an instance
 *
 * @param[in] ins_id InstanceID to check the size for
 * @param[out] size_p Size of the instance
 * @param[out] compr_p the instance was compressed
 *
 * @return Kopano error code
 */
ECRESULT ECS3Attachment::GetSizeInstance(const ext_siid &ins_id,
    size_t *size_p, bool *compr_p)
{
	bool comp = false;
	auto filename = make_att_filename(ins_id, comp);
	auto fn = filename.c_str();

	ulock_normal locker(m_cachelock);
	auto cache_item = m_cache.find(ins_id.siid);
	if (cache_item != m_cache.cend() && steady_clock::now() < cache_item->second.valid_until) {
		if (cache_item->second.size == S3_NEGATIVE_ENTRY)
			return KCERR_NOT_FOUND;
		*size_p = cache_item->second.size;
		if (compr_p != nullptr)
			*compr_p = comp;
		return erSuccess;
	}
	locker.unlock();

	struct s3_cd cd;
	struct s3_cdw cwdata;
	cwdata.caller = this;
	cwdata.cbdata = &cd;

	ec_log_debug("S3: getsize %s", fn);
	/*
	 * Loop at most S3_RETRIES times, to make sure that if the servers of
	 * S3 reply with a redirect, we actually try again and process it.
	 */
	cd.retries = S3_RETRIES;
	do {
		DY_head_object(&m_bucket_ctx, fn, nullptr, 0,
			&m_response_handler, &cwdata);
		if (DY_status_is_retryable(cd.status))
			ec_log_debug("S3: getsize %s: retryable status: %s",
				fn, DY_get_status_name(cd.status));
	} while (DY_status_is_retryable(cd.status) && should_retry(cd));

	ec_log_debug("S3: getsize %s: %s, %zu bytes",
		fn, DY_get_status_name(cd.status), cd.size);
	if (cd.status == S3StatusHttpErrorNotFound) {
		locker.lock();
		m_cache[ins_id.siid] = {now_negative(), S3_NEGATIVE_ENTRY};
		return KCERR_NOT_FOUND;
	}
	if (cd.status != S3StatusOK)
		return KCERR_NOT_FOUND;
	locker.lock();
	m_cache[ins_id.siid] = {now_positive(), cd.size};
	*size_p = cd.size;
	if (compr_p != NULL)
		*compr_p = comp;
	return erSuccess;
}

kd_trans ECS3Attachment::Begin(ECRESULT &trigger)
{
	ec_log_debug("S3: begin transaction");
	if (m_transact) {
		/* Possible a duplicate begin call, don't destroy the data in production */
		assert(false);
		return kd_trans();
	}
	m_new_att.clear();
	m_marked_att.clear();
	m_transact = true;
	return kd_trans(*this, trigger);
}

ECRESULT ECS3Attachment::Commit(void)
{
	bool error = false;

	ec_log_debug("S3: commit transaction");
	if (!m_transact) {
		assert(false);
		return erSuccess;
	}

	/* Disable the transaction */
	m_transact = false;
	/* Delete marked attachments */
	for (const auto &att_id : m_marked_att)
		if (del_marked_att(att_id) != erSuccess)
			error = true;

	m_new_att.clear();
	m_marked_att.clear();
	return error ? KCERR_DATABASE_ERROR : erSuccess;
}

ECRESULT ECS3Attachment::Rollback(void)
{
	bool error = false;

	ec_log_debug("S3: rollback transaction");
	if (!m_transact) {
		assert(false);
		return erSuccess;
	}

	/* Disable the transaction */
	m_transact = false;
	/* Remove the created attachments */
	for (const auto &att_id : m_new_att)
		if (DeleteAttachmentInstance(att_id, false) != erSuccess)
			error = true;
	/* Restore marked attachment */
	for (const auto &att_id : m_marked_att) {
		ec_log_debug("S3: removed delete mark for %u", att_id.siid);
		m_marked_att.erase(att_id);
	}
	m_new_att.clear();
	m_marked_att.clear();
	return error ? KCERR_DATABASE_ERROR : erSuccess;
}

} /* namespace */

#endif /* LIBS3_H */
