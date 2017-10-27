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

static char *password_encrypt_crypt(const char *data, unsigned int len) {
	char salt[3];
	rand_get(salt, 2);
	salt[0] &= 0x7F;
	salt[1] &= 0x7F;
	salt[2] = '\0';
	char cryptbuf[14];
	DES_fcrypt(data, salt, cryptbuf);
	auto res = new char[20];
	snprintf(res, 20, "{CRYPT}%s", cryptbuf);
	return res;
}

static int password_check_crypt(const char *data, unsigned int len, const char *crypted) {
	char salt[3];
	char cryptbuf[14];
	salt[0] = crypted[0] & 0x7F;
	salt[1] = crypted[1] & 0x7F;
	salt[2] = 0;
	DES_fcrypt(data, salt, cryptbuf);

	if (!strcmp(cryptbuf, crypted))
		return 0;
	else
		return 1;
}

static char *password_encrypt_md5(const char *data, unsigned int len) {
	unsigned char md5_out[MD5_DIGEST_LENGTH];
	const int base64_len = MD5_DIGEST_LENGTH * 4 / 3 + 4;
	char *res;

	MD5((unsigned char *) data, len, md5_out);
	res = new char[base64_len+6];
	snprintf(res, base64_len + 6, "{MD5}%s", base64_encode(md5_out, sizeof(md5_out)).c_str());
	return res;
}

static int password_check_md5(const char *data, unsigned int len, const char *crypted) {
	unsigned char md5_out[MD5_DIGEST_LENGTH];
	MD5((unsigned char *) data, len, md5_out);
	return strcmp(crypted, base64_encode(md5_out, sizeof(md5_out)).c_str());
}

// md5sum + salt at the end. md5sum length == 16, salt length == 4
static char *password_encrypt_smd5(const char *data, unsigned int len) {
	MD5_CTX ctx;
	unsigned char md5_out[MD5_DIGEST_LENGTH + 4];
	unsigned char *salt = md5_out + MD5_DIGEST_LENGTH; // salt is at the end of the digest
	constexpr size_t base64_len = sizeof(md5_out) * 4 / 3 + 4;
	char *res;

	rand_get(reinterpret_cast<char *>(salt), 4);
	MD5_Init(&ctx);
	MD5_Update(&ctx, data, len);
	MD5_Update(&ctx, salt, 4);
	MD5_Final(md5_out, &ctx);	// writes upto the salt
	res = new char[base64_len+7];
	snprintf(res, base64_len + 7, "{SMD5}%s", base64_encode(md5_out, sizeof(md5_out)).c_str());
	return res;
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

static char *password_encrypt_ssha(const char *data, unsigned int len, bool bSalted) {
	unsigned char SHA_out[SHA_DIGEST_LENGTH+4];
	unsigned char *salt = SHA_out + SHA_DIGEST_LENGTH;
	constexpr size_t base64_len = sizeof(SHA_out) * 4 / 3 + 4;
	std::string b64_out;
	char *res;

	if (!bSalted) {
		SHA1(reinterpret_cast<const unsigned char *>(data), len, SHA_out);
		b64_out = base64_encode(SHA_out, SHA_DIGEST_LENGTH);
	} else {
		SHA_CTX ctx;
		SHA1_Init(&ctx);
		rand_get(reinterpret_cast<char *>(salt), 4);
		SHA1_Update(&ctx, data, len);
		SHA1_Update(&ctx, salt, 4);
		SHA1_Final(SHA_out, &ctx);
		b64_out = base64_encode(SHA_out, SHA_DIGEST_LENGTH + 4);
	}

	res = new char[base64_len + 12];
	snprintf(res, base64_len + 11, "{%s}%s", bSalted ? "SSHA" : "SHA", b64_out.c_str());
	return res;
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

	digest.assign((char*)SHA_out, SHA_DIGEST_LENGTH);
	if (bSalted)
		digest += salt;
	return strcmp(base64_encode(digest.c_str(), digest.length()).c_str(), crypted);
}

char *encryptPassword(int type, const char *password) {
	switch(type) {
	case PASSWORD_CRYPT:
		return password_encrypt_crypt(password, strlen(password));
	case PASSWORD_MD5:
		return password_encrypt_md5(password, strlen(password));
	case PASSWORD_SMD5:
		return password_encrypt_smd5(password, strlen(password));
	case PASSWORD_SHA:
		return password_encrypt_ssha(password, strlen(password), false);
	case PASSWORD_SSHA:
		return password_encrypt_ssha(password, strlen(password), true);
	default:
		return NULL;
	}
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
