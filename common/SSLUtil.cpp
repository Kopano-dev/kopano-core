/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <mutex>
#include <kopano/platform.h>
#include <kopano/ECLogger.h>
#include "SSLUtil.h"
#include <pthread.h>
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/conf.h>
#include <openssl/engine.h>

namespace KC {

static std::recursive_mutex *ssl_locks;

#if defined(LIBRESSL_VERSION_NUMBER) || (defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER < 0x1010000fL)
#	define OLD_API 1
#endif

#ifdef OLD_API
static void ssl_lock(int mode, int n, const char *file, int line)
{
	if (mode & CRYPTO_LOCK)
		ssl_locks[n].lock();
	else
		ssl_locks[n].unlock();
}

static unsigned long ssl_id_function(void)
{
    return ((unsigned long) pthread_self());
}
#endif

void ssl_threading_setup() {
	if (ssl_locks)
		return;
	// make recursive, because of openssl bug http://rt.openssl.org/Ticket/Display.html?id=2813&user=guest&pass=guest
	ssl_locks = new std::recursive_mutex[CRYPTO_num_locks()];
	CRYPTO_set_locking_callback(ssl_lock);
	CRYPTO_set_id_callback(ssl_id_function);
}

void ssl_threading_cleanup() {
	if (!ssl_locks)
		return;
	CRYPTO_set_id_callback(NULL);
	CRYPTO_set_locking_callback(NULL);
	delete[] ssl_locks;
	ssl_locks = nullptr;
}

/**
 * Free most of the SSL library allocated memory.
 *
 * This will remove most of the memory used by
 * the ssl library. Don't use this function in libraries
 * because it will unload the whole SSL data.
 *
 * This function makes valgrind happy
 */
void SSL_library_cleanup()
{
#if OPENSSL_VERSION_NUMBER >= 0x1010000fL
	// No cleanup required, is a current OpenSSL.
#else // OPENSSL_VERSION_NUMBER < 0x1010000fL
	// Clean up any possible allocations.
	OBJ_cleanup();
	CONF_modules_free();
	EVP_cleanup();
#	ifndef OPENSSL_NO_ENGINE
	ENGINE_cleanup();
#	endif
#	if OPENSSL_VERSION_NUMBER >= 0x1000200fL && !defined(OPENSSL_NO_COMP)
	SSL_COMP_free_compression_methods();
#	endif
	ERR_free_strings();
#	ifdef OLD_API
	ERR_remove_state(0);
#	endif
	CRYPTO_cleanup_all_ex_data();
#endif // OPENSSL_VERSION_NUMBER
}

void ssl_random_init()
{
	rand_init();
	while (RAND_status() == 0) {
		char buffer[16];
		rand_get(buffer, sizeof buffer);
		RAND_seed(buffer, sizeof buffer);
	}
}

void ssl_random(bool b64bit, uint64_t *id)
{
#ifdef OLD_API
	int ret = RAND_pseudo_bytes(reinterpret_cast<unsigned char *>(id), sizeof(*id));
#else
	int ret = RAND_bytes(reinterpret_cast<unsigned char *>(id), sizeof(*id));
#endif
	if (ret < 0) {
		ec_log_crit("RAND_bytes < 0: %s\n", ERR_reason_error_string(ERR_get_error()));
		abort();
	}
	if (!b64bit)
		*id &= 0xFFFFFFFF;
}

bool ec_tls_minproto(SSL_CTX *ctx, const char *p)
{
#if defined(LIBRESSL_VERSION_NUMBER) || (defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10100000L)
	if (strcmp(p, "tls1.3") == 0)
#ifdef TLS1_3_VERSION
		SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
#else
		return false;
#endif
	else if (strcmp(p, "tls1.2") == 0)
		SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
	else if (strcmp(p, "tls1.1") == 0)
		SSL_CTX_set_min_proto_version(ctx, TLS1_1_VERSION);
	else if (strcmp(p, "tls1.0") == 0)
		SSL_CTX_set_min_proto_version(ctx, TLS1_VERSION);
	else if (strcmp(p, "ssl3") == 0)
		SSL_CTX_set_min_proto_version(ctx, SSL3_VERSION);
	else
		return false;
#else
	if (strcmp(p, "ssl3") == 0)
		SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2);
	else if (strcmp(p, "tls1.0") == 0)
		SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
	else if (strcmp(p, "tls1.1") == 0)
		SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1);
	else if (strcmp(p, "tls1.2") == 0)
		SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
	else if (strcmp(p, "tls1.3") == 0)
		SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2);
	else
		return false;
#endif
	return true;
}

} /* namespace */
