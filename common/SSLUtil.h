/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef SSLUTIL_H
#define SSLUTIL_H

#include <kopano/zcdefs.h>
#include <openssl/obj_mac.h>
#include <openssl/ossl_typ.h>

namespace KC {

extern _kc_export void ssl_threading_setup(void);
extern _kc_export void ssl_threading_cleanup(void);
extern _kc_export void SSL_library_cleanup(void);
extern _kc_export void ssl_random_init(void);
extern _kc_export void ssl_random(bool b64bit, uint64_t *out);
extern _kc_export bool ec_tls_minproto(SSL_CTX *, const char *min_proto);

#define KC_DEFAULT_TLSMINPROTO "tls1.2"
#define KC_DEFAULT_CIPHERLIST "DEFAULT:!LOW:!SSLv2:!SSLv3:!TLSv1.0:!TLSv1.1:!EXPORT:!DH:!PSK:!kRSA:!aDSS:!aNULL:+AES"
#ifdef NID_X25519
#	define KC_DEFAULT_ECDH_CURVES "X25519:P-521:P-384:P-256"
#else
#	define KC_DEFAULT_ECDH_CURVES "P-521:P-384:P-256"
#endif

} /* namespace */

#endif
