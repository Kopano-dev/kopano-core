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
#include <dovecot/index-mail.h>
#include <dovecot/index-storage.h>
#include <dovecot/mail-storage.h>
#include <dovecot/mail-storage-private.h>
#include <dovecot/mailbox-list-private.h>
#include <stdbool.h>
#include <stdio.h>
#include "dovestore.h"
#define export __attribute__((visibility("default")))

struct kp_storage {
	struct mail_storage super;
};

struct kp_mailbox {
	struct mailbox super;
	struct kp_storage *kp_storage;
	void *mapises;
};

struct kp_mailbox_list {
	struct mailbox_list super;
};

static const struct mailbox kp_mailbox_reg;
static struct mailbox_list kp_mblist_reg;
static struct mail_storage kp_storage_reg;

static void kp_storage_get_list_settings(const struct mail_namespace *ns,
    struct mailbox_list_settings *set)
{
	fprintf(stderr, "%s\n", __func__);
	if (set->layout == NULL)
		set->layout = "mapimblist";
	if (set->subscription_fname == NULL)
		set->subscription_fname = "subscriptions";
}

static struct mailbox_list *kp_mblist_alloc(void)
{
	fprintf(stderr, "%s\n", __func__);
	pool_t pool = pool_alloconly_create("KGWC mailbox list", 1024);
	struct kp_mailbox_list *list = p_new(pool, struct kp_mailbox_list, 1);
	list->super = kp_mblist_reg;
	list->super.pool = pool;
	return &list->super;
}

static char kp_mblist_hiersep(struct mailbox_list *ml)
{
	fprintf(stderr, "%s\n", __func__);
	return '/';
}

static void kp_mblist_deinit(struct mailbox_list *super)
{
	fprintf(stderr, "%s\n", __func__);
	pool_unref(&super->pool);
}

static struct mailbox_list_iterate_context *
kp_mblist_iterinit(struct mailbox_list *list_base, const char *const *patterns,
    enum mailbox_list_iter_flags flags)
{
	struct kp_mblist *mblist = (void *)list_base;
}

static struct mail_storage *kp_storage_alloc(void)
{
	fprintf(stderr, "%s\n", __func__);
	pool_t pool = pool_alloconly_create("KGWC storage", 2048);
	struct kp_storage *s = p_new(pool, struct kp_storage, 1);
	if (s == NULL)
		return NULL;
	s->super = kp_storage_reg;
	s->super.pool = pool;
	return &s->super;
}

static int kp_storage_create(struct mail_storage *super,
    struct mail_namespace *ns, const char **err)
{
	fprintf(stderr, "%s\n", __func__);
	struct kp_storage *stg = (void *)super;
	return 0;
}

static struct mailbox *kp_mailbox_alloc(struct mail_storage *storage,
    struct mailbox_list *mblist, const char *vname, enum mailbox_flags flags)
{
	fprintf(stderr, "%s\n", __func__);
	pool_t pool = pool_alloconly_create("KGWC mailbox", 1024 * 4);
	struct kp_mailbox *m = p_new(pool, struct kp_mailbox, 1);
	if (m == NULL)
		return NULL;
	m->super = kp_mailbox_reg;
	m->super.vname = p_strdup(pool, vname);
	m->super.name = p_strdup(pool, mailbox_list_get_storage_name(mblist, vname));
	m->super.storage = storage;
	m->super.list = mblist;
	m->super.pool = pool;
	m->super.flags = flags;
	index_storage_mailbox_alloc(&m->super, vname, flags, MAIL_INDEX_PREFIX);
	return &m->super;
}

static int kp_mailbox_exists(struct mailbox *box, bool auto_boxes,
    enum mailbox_existence *r)
{
	fprintf(stderr, "%s\n", __func__);
	*r = box->inbox_any ? MAILBOX_EXISTENCE_SELECT : MAILBOX_EXISTENCE_NONE;
	return 0;
}

static int kp_mailbox_create(struct mailbox *mbox,
    const struct mailbox_update *update, bool directory)
{
	fprintf(stderr, "%s\n", __func__);
	return index_storage_mailbox_create(mbox, directory);
}

static int kp_mailbox_open(struct mailbox *box)
{
	fprintf(stderr, "%s\n", __func__);
	if (index_storage_mailbox_open(box, false) < 0)
		return -1;
	struct kp_mailbox *kbox = (void *)box;
	kbox->mapises = kpxx_login();
	return 0;
}

static void kp_mailbox_close(struct mailbox *box)
{
	fprintf(stderr, "%s\n", __func__);
	struct kp_mailbox *kbox = (void *)box;
	kpxx_logout(kbox->mapises);
	index_storage_mailbox_close(box);
}

static const struct mail_vfuncs kp_mail_vfuncs = {
	.close = index_mail_close,
	.free = index_mail_free,
	.set_seq = index_mail_set_seq,
	.set_uid = index_mail_set_uid,
	.set_uid_cache_updates = index_mail_set_uid_cache_updates,
	.prefetch = index_mail_prefetch,
	.precache = index_mail_precache,
	.add_temp_wanted_fields = index_mail_add_temp_wanted_fields,
	.get_flags = index_mail_get_flags,
	.get_keywords = index_mail_get_keywords,
	.get_keyword_indexes = index_mail_get_keyword_indexes,
	.get_modseq = index_mail_get_modseq,
	.get_pvt_modseq = index_mail_get_pvt_modseq,
	.get_parts = index_mail_get_parts,
	.get_date = index_mail_get_date,
	.get_received_date = index_mail_get_received_date,
	.get_save_date = index_mail_get_save_date,
	.get_virtual_size = index_mail_get_virtual_size,
	.get_physical_size = index_mail_get_physical_size,
	.get_first_header = index_mail_get_first_header,
	.get_headers = index_mail_get_headers,
	.get_header_stream = index_mail_get_header_stream,
//	.get_stream = index_mail_get_stream,
	.get_binary_stream = index_mail_get_binary_stream,
	.get_special = index_mail_get_special,
	.get_backend_mail = index_mail_get_backend_mail,
	.update_flags = index_mail_update_flags,
	.update_keywords = index_mail_update_keywords,
	.update_modseq = index_mail_update_modseq,
	.update_pvt_modseq = index_mail_update_pvt_modseq,
	.update_pop3_uidl = NULL,
	.expunge = index_mail_expunge,
	.set_cache_corrupted = index_mail_set_cache_corrupted,
	.istream_opened = index_mail_opened,
};

static const struct mailbox kp_mailbox_reg = {
	.name = "mapimbox",
	.vname = "mapivmbox",
	.v = {
		.is_readonly = index_storage_is_readonly,
		.enable = index_storage_mailbox_enable,
		.exists = kp_mailbox_exists,
		.open = kp_mailbox_open,
		.close = index_storage_mailbox_close,
		.free = index_storage_mailbox_free,
		.create_box = kp_mailbox_create,
		.update_box = index_storage_mailbox_update,
		.delete_box = index_storage_mailbox_delete,
		.rename_box = index_storage_mailbox_rename,
		.get_status = index_storage_get_status,
		.get_metadata = index_mailbox_get_metadata,
		.set_subscribed = index_storage_set_subscribed,
		.attribute_set = index_storage_attribute_set,
		.attribute_get = index_storage_attribute_get,
		.attribute_iter_init = index_storage_attribute_iter_init,
		.attribute_iter_next = index_storage_attribute_iter_next,
		.attribute_iter_deinit = index_storage_attribute_iter_deinit,
	},
};

static struct mailbox_list kp_mblist_reg = {
	.name = "mapimblist",
	.props = MAILBOX_LIST_PROP_NO_ROOT | MAILBOX_LIST_PROP_AUTOCREATE_DIRS,
	.mailbox_name_max_length = MAILBOX_LIST_NAME_MAX_LENGTH,
	.v = {
		.alloc = kp_mblist_alloc,
		.get_hierarchy_sep = kp_mblist_hiersep,
		.deinit = kp_mblist_deinit,
		.iter_init = kp_mblist_iterinit,
		.get_storage_name = mailbox_list_default_get_storage_name,
		.get_path = mailbox_list_get_path,
	},
};

/* bad bad bad API... no !@#$ documentation what functions need to be
 * implemented, and moreover, what they are supposed to return/do */

static struct mail_storage kp_storage_reg = {
	.name = "dovemapi",
	.class_flags = MAIL_STORAGE_CLASS_FLAG_NO_ROOT /* "root not required for this storage" */,
	.v = {
		.alloc = kp_storage_alloc,
		.create = kp_storage_create,
		.destroy = index_storage_destroy,
		.get_list_settings = kp_storage_get_list_settings,
		.mailbox_alloc = kp_mailbox_alloc,
	},
};

export void dovemapi_plugin_init(void)
{
	mail_storage_class_register(&kp_storage_reg);
	mailbox_list_register(&kp_mblist_reg);
}

export void dovemapi_plugin_deinit(void)
{
	mailbox_list_unregister(&kp_mblist_reg);
	mail_storage_class_unregister(&kp_storage_reg);
}
