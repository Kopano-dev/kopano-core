/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include "plugin.h"
#include <openssl/des.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <cstring>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <kopano/stringutil.h>
#include "ldappasswords.h"

namespace KC {

static int password_check_crypt(const char *data, unsigned int len, const char *crypted) {
	char salt[3];
	char cryptbuf[14];
	salt[0] = crypted[0] & 0x7F;
	salt[1] = crypted[1] & 0x7F;
	salt[2] = 0;
	DES_fcrypt(data, salt, cryptbuf);
	return strcmp(cryptbuf, crypted);
}

static int password_check_md5(const char *data, unsigned int len, const char *crypted) {
	unsigned char md5_out[MD5_DIGEST_LENGTH];
	MD5((unsigned char *) data, len, md5_out);
	return strcmp(crypted, base64_encode(md5_out, sizeof(md5_out)).c_str());
}

static int password_check_smd5(const char *data, unsigned int len, const char *crypted) {
	unsigned char md5_out[MD5_DIGEST_LENGTH];
	MD5_CTX ctx;
	auto digest = base64_decode(crypted);
	if (digest.size() < MD5_DIGEST_LENGTH + 4)
		return 1;
	std::string salt(digest.c_str() + MD5_DIGEST_LENGTH, digest.length() - MD5_DIGEST_LENGTH);

	MD5_Init(&ctx);
	MD5_Update(&ctx, data, len);
	MD5_Update(&ctx, salt.c_str(), salt.length());
	MD5_Final(md5_out, &ctx);
	return strcmp(crypted, base64_encode(md5_out, sizeof(md5_out)).c_str());
}

static int password_check_ssha(const char *data, unsigned int len, const char *crypted, bool bSalted) {
	std::string salt;
	unsigned char SHA_out[SHA_DIGEST_LENGTH];
	std::string pwd(data, len);
	auto digest = base64_decode(crypted);

	if (bSalted) {
		if (digest.size() < SHA_DIGEST_LENGTH + 4)
			return 1;
		salt.assign(digest.c_str()+SHA_DIGEST_LENGTH, digest.length()-SHA_DIGEST_LENGTH);
		pwd += salt;
	}

	memset(SHA_out, 0, sizeof(SHA_out));
	SHA1((const unsigned char*)pwd.c_str(), pwd.length(), SHA_out);
	digest.assign(reinterpret_cast<char *>(SHA_out), SHA_DIGEST_LENGTH);
	if (bSalted)
		digest += salt;
	return strcmp(base64_encode(digest.c_str(), digest.length()).c_str(), crypted);
}

int checkPassword(int type, const char *password, const char *crypted) {
	switch(type) {
	case PASSWORD_CRYPT:
		return password_check_crypt(password, strlen(password), crypted);
	case PASSWORD_MD5:
		return password_check_md5(password, strlen(password), crypted);
	case PASSWORD_SMD5:
		return password_check_smd5(password, strlen(password), crypted);
	case PASSWORD_SHA:
		return password_check_ssha(password, strlen(password), crypted, false);
	case PASSWORD_SSHA:
		return password_check_ssha(password, strlen(password), crypted, true);
	default:
		return 1;
	}
}

} /* namespace */
