/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef SSLUTIL_H
#define SSLUTIL_H

#include <kopano/zcdefs.h>

namespace KC {

extern _kc_export void ssl_threading_setup(void);
extern _kc_export void ssl_threading_cleanup(void);
extern _kc_export void SSL_library_cleanup(void);
extern _kc_export void ssl_random_init(void);
extern _kc_export void ssl_random(bool b64bit, uint64_t *out);

#define KC_DEFAULT_SSLPROTOLIST "!SSLv2 !SSLv3"
#define KC_DEFAULT_CIPHERLIST "DEFAULT:!LOW:!SSLv2:!SSLv3:!EXPORT:!aNULL"
// NOTE(longsleep): Unfortunately OpenSSL does not allow to specify curves which
// are not supported by the particular release. Thus for now curve X25519 is not
// in the default ECDH curve list since it is only supported starting OpenSSL 1.1.
#define KC_DEFAULT_ECDH_CURVES "P-521:P-384:P-256"

} /* namespace */

#endif
