/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef RTF_H
#define RTF_H

#include <kopano/zcdefs.h>

namespace KC {

extern KC_EXPORT unsigned int rtf_get_uncompressed_length(const char *data, unsigned int size);
extern KC_EXPORT unsigned int rtf_decompress(char *dst, const char *src, unsigned int src_size);
extern KC_EXPORT unsigned int rtf_compress(char **dst, unsigned int *dst_size, const char *src, unsigned int src_size);

}

#endif // RTF_H
