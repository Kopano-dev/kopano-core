/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

// -*- Mode: c++ -*-

#ifndef LDAPPASSWORDS_H
#define LDAPPASSWORDS_H

namespace KC {

/**
 * @defgroup userplugin_password Password validation
 * @ingroup userplugin
 * @{
 */


enum {
	PASSWORD_CRYPT,
	PASSWORD_MD5,
	PASSWORD_SMD5,
	PASSWORD_SHA,
	PASSWORD_SSHA
};

/**
 * Compare unencrypted password with encrypted password with the
 * requested encryption type
 *
 * @param[in]	type
 *					The encryption type (CRYPT, MD5, SMD5, SSHA)
 * @param[in]	password
 *					The unencryped password which should match crypted
 * @param[in]	crypted
 *					The encrypted password which should match password
 * @return 0 when the passwords match
 */
extern int checkPassword(int type, const char *password, const char *crypted);

/** @} */

} /* namespace */

#endif
