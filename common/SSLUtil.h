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

#define KC_DEFAULT_CIPHERLIST "DEFAULT:!LOW:!SSLv2:!EXPORT:!aNULL"

} /* namespace */

#endif
