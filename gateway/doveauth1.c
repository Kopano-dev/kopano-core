/*
 * Copyright 2017 Kopano and its licensors
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HAVE_CONFIG_H
#	define HAVE_CONFIG_H /* d'uh'vecot */
#endif
#include <dovecot/config.h>
#include <dovecot/lib.h>
#include <dovecot/auth-request.h>
#include <dovecot/passdb.h>
#include "doveauth.h"
#define export __attribute__((visibility("default")))

static enum passdb_result
authdb_mapi_logon(struct auth_request *rq, const char *password)
{
	switch (authdb_mapi_logonxx(rq, rq->user, password)) {
	default:
		return PASSDB_RESULT_INTERNAL_FAILURE;
	case 0:
		return PASSDB_RESULT_PASSWORD_MISMATCH;
	case 1:
		return PASSDB_RESULT_OK;
	}
}

static void authdb_mapi_verify_plain(struct auth_request *req,
    const char *password, verify_plain_callback_t *callback)
{
	(*callback)(authdb_mapi_logon(req, password), req);
}

static struct passdb_module_interface authdb_mapi = {
	.name = "mapi",
	.verify_plain = authdb_mapi_verify_plain,
};

extern export void authdb_mapi_init(void);
void authdb_mapi_init(void)
{
	authdb_mapi_initxx();
	passdb_register_module(&authdb_mapi);
}

extern export void authdb_mapi_deinit(void);
void authdb_mapi_deinit(void)
{
	passdb_unregister_module(&authdb_mapi);
	authdb_mapi_deinitxx();
}

void auth_request_log_debugcc(void *rq, const char *text)
{
	auth_request_log_debug(rq, AUTH_SUBSYS_DB, "%s", text);
}
