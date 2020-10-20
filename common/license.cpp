/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005–2016 Zarafa and its licensors
 */
#include <memory>
#include <string>
#include <openssl/evp.h>
#include <mapicode.h>
#include <mapidefs.h>
#include <kopano/license.hpp>
#include <kopano/platform.h>

namespace KC {

struct sslfree {
	public:
	void operator()(EVP_CIPHER_CTX *c) { EVP_CIPHER_CTX_free(c); }
};

static constexpr uint8_t kex[16] = {0x94,0x46,0x5f,0xbf,0x9e,0x64,0xa5,0xf1,0xf3,0xf9,0xde,0x1e,0xee,0xb2,0xef,0xcf};
static constexpr uint8_t kiv[8]  = {0x5c,0x76,0x9d,0x91,0xb2,0xec,0x29,0x58};

HRESULT licstream_enc(const void *src, size_t src_size, std::string &dst)
{
	std::unique_ptr<EVP_CIPHER_CTX, sslfree> ctx(EVP_CIPHER_CTX_new());
	EVP_EncryptInit(ctx.get(), EVP_bf_cbc(), kex, kiv);
	dst.resize(src_size + EVP_CIPHER_CTX_block_size(ctx.get()));
	auto outbuf = reinterpret_cast<unsigned char *>(&dst[0]);
	int dst_size = 0, trailer_len = 0;
	EVP_EncryptUpdate(ctx.get(), outbuf, &dst_size, reinterpret_cast<const unsigned char *>(src), src_size);
	if (EVP_EncryptFinal(ctx.get(), outbuf + dst_size, &trailer_len) != 1)
		return MAPI_E_INVALID_PARAMETER;
	dst.resize(dst_size + trailer_len);
	return hrSuccess;
}

HRESULT licstream_dec(const void *src, size_t src_size, std::string &dst)
{
	std::unique_ptr<EVP_CIPHER_CTX, sslfree> ctx(EVP_CIPHER_CTX_new());
	EVP_DecryptInit(ctx.get(), EVP_bf_cbc(), kex, kiv);
	dst.resize(src_size + EVP_CIPHER_CTX_block_size(ctx.get()));
	auto outbuf = reinterpret_cast<unsigned char *>(&dst[0]);
	int dst_size = 0, trailer_len = 0;
	EVP_DecryptUpdate(ctx.get(), outbuf, &dst_size, reinterpret_cast<const unsigned char *>(src), src_size);
	if (EVP_DecryptFinal(ctx.get(), outbuf + dst_size, &trailer_len) != 1)
		return MAPI_E_INVALID_PARAMETER;
	dst.resize(dst_size + trailer_len);
	return hrSuccess;
}

}
