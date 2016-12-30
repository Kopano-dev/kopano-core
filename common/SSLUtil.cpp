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
#include <mutex>
#include <kopano/platform.h>
#include <kopano/lockhelper.hpp>
#include "SSLUtil.h"
#include <pthread.h>
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/conf.h>
#include <openssl/engine.h>

namespace KC {

static std::recursive_mutex *ssl_locks;

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
 * This will remove most of the memmory used by 
 * the ssl library. Don't use this function in libraries
 * because it will unload the whole SSL data.
 *
 * This function makes valgrind happy
 */
void SSL_library_cleanup()
{
	#ifndef OPENSSL_NO_ENGINE
		ENGINE_cleanup();
	#endif

	ERR_free_strings();
	ERR_remove_state(0);
	EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();

	CONF_modules_unload(0);

	sk_SSL_COMP_free(SSL_COMP_get_compression_methods());
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
	RAND_pseudo_bytes(reinterpret_cast<unsigned char *>(id), sizeof(*id));
	if (!b64bit)
		*id &= 0xFFFFFFFF;
}

} /* namespace */
