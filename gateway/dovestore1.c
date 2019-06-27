/*
 * Copyright 2019 Kopano and its licensors
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
#include <dovecot/mail-copy.h>
#include <dovecot/mail-storage.h>
#include <dovecot/mail-storage-private.h>
#include <dovecot/mailbox-list-private.h>
#include <dovecot/mailbox-tree.h>
#include <stdbool.h>
#include <stdio.h>
#include <libHX/misc.h>
#include "dovestore.h"
#define export __attribute__((visibility("default")))

struct kp_storage {
	struct mail_storage super;
};

struct kp_mailbox {
	struct mailbox super;
	struct kp_storage *kp_storage;
	void *mapises, *mapistore, *mapifld;
};

struct kp_mailbox_list {
	struct mailbox_list super;
	void *mapises, *mapistore;
};

struct kp_mblist_iterctx {
	struct mailbox_list_iterate_context super;
	struct mailbox_info info;
	char **mbhier;
	unsigned int mbpos;
};

struct kp_mail {
	struct index_mail super;
};

static const struct mailbox kp_mailbox_reg;
static struct mailbox_list kp_mblist_reg;
static struct mail_storage kp_storage_reg;
static const struct mail_vfuncs kp_mail_vfuncs;

static void kp_storage_get_list_settings(const struct mail_namespace *ns,
    struct mailbox_list_settings *set)
{
	fprintf(stderr, "%s\n", __func__);
	if (set->layout == NULL)
		set->layout = "mapimblist";
	if (set->root_dir != NULL && *set->root_dir != '\0' &&
	    set->index_dir == NULL) {
		/* we don't really care about root_dir, but we just need to get index_dir autocreated. */
		set->index_dir = set->root_dir;
	}
}

static struct mailbox_list *kp_mblist_alloc(void)
{
	fprintf(stderr, "%s\n", __func__);
	pool_t pool = pool_alloconly_create("KGWC mailbox list", 1024);
	struct kp_mailbox_list *list = p_new(pool, struct kp_mailbox_list, 1);
	list->super = kp_mblist_reg;
	list->super.pool = pool;
	list->mapistore = NULL;
	return &list->super;
}

static int kp_mblist_init(struct mailbox_list *vmbl, const char **errp)
{
	fprintf(stderr, "%s\n", __func__);
	struct kp_mailbox_list *list = (void *)vmbl;
	list->mapises = kpxx_login();
	if (list->mapises != NULL) {
		list->mapistore = kpxx_store_get(list->mapises);
		if (list->mapistore == NULL) {
			kpxx_logout(list->mapises);
			list->mapises = NULL;
			return -1;
		}
	}
	return 0;
}

static void kp_mblist_deinit(struct mailbox_list *vmbl)
{
	fprintf(stderr, "%s\n", __func__);
	struct kp_mailbox_list *list = (void *)vmbl;
	kpxx_store_put(list->mapistore);
	kpxx_logout(list->mapises);
	pool_unref(&list->super.pool);
}

static char kp_mblist_hiersep(struct mailbox_list *ml)
{
	return '/';
}

static int kp_mblist_getpath(struct mailbox_list *_list, const char *name,
    enum mailbox_list_path_type type, const char **pathp)
{
	*pathp = NULL;
	return 0;
}

static const char *kp_mblist_tempprefix(struct mailbox_list *vmbl, bool global)
{
	fprintf(stderr, "%s\n", __func__);
	return global ? mailbox_list_get_global_temp_prefix(vmbl) :
	       mailbox_list_get_temp_prefix(vmbl);
}

static struct mailbox_list_iterate_context *
kp_mblist_iterinit(struct mailbox_list *vmbl, const char *const *patterns,
    enum mailbox_list_iter_flags flags)
{
	struct kp_mailbox_list *list = (void *)vmbl;
	pool_t pool = pool_alloconly_create("mailbox list imapc iter", 1024);
	struct kp_mblist_iterctx *ctx = p_new(pool, struct kp_mblist_iterctx, 1);
	ctx->super.pool  = pool;
	ctx->super.list  = vmbl;
	ctx->super.flags = flags;
	array_create(&ctx->super.module_contexts, pool, sizeof(void *), 5);
	ctx->info.vname  = NULL;
	ctx->info.ns     = vmbl->ns;
	ctx->mbhier      = kpxx_hierarchy_list(list->mapistore);
	ctx->mbpos       = 0;
	return &ctx->super;
}

static const struct mailbox_info *
kp_mblist_iternext(struct mailbox_list_iterate_context *vctx)
{
	struct kp_mblist_iterctx *ctx = (void *)vctx;
	const char *name = ctx->mbhier[ctx->mbpos++];
	if (name == NULL)
		return NULL;
	ctx->info.vname = name;
	return &ctx->info;
}

static int kp_mblist_iterdeinit(struct mailbox_list_iterate_context *vctx)
{
	struct kp_mblist_iterctx *ctx = (void *)vctx;
	HX_zvecfree(ctx->mbhier);
	pool_unref(&vctx->pool);
	return 0;
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

static void kp_notify_changes(struct mailbox *m)
{}

static struct mail_save_context *kp_save_alloc(struct mailbox_transaction_context *t)
{
	struct mail_save_context *ctx = i_new(struct mail_save_context, 1);
	ctx->transaction = t;
	return ctx;
}

static int kp_save_begin(struct mail_save_context *ctx, struct istream *input)
{
	mail_storage_set_error(ctx->transaction->box->storage,
		MAIL_ERROR_NOTPOSSIBLE, "KP does not support saving mails");
	return -1;
}

static int kp_save_continue(struct mail_save_context *ctx)
{
	return -1;
}

static int kp_save_finish(struct mail_save_context *ctx)
{
	index_save_context_free(ctx);
	return -1;
}

static void kp_save_cancel(struct mail_save_context *ctx)
{
	index_save_context_free(ctx);
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
	m->super.list->props |= MAILBOX_LIST_PROP_AUTOCREATE_DIRS;
	m->super.pool = pool;
	m->super.flags = flags;
	m->super.mail_vfuncs = &kp_mail_vfuncs;
	index_storage_mailbox_alloc(&m->super, vname, flags, MAIL_INDEX_PREFIX);
	m->mapises = NULL;
	m->mapistore = NULL;
	m->mapifld = NULL;
	return &m->super;
}

static int kp_mailbox_exists(struct mailbox *box, bool auto_boxes,
    enum mailbox_existence *r)
{
	fprintf(stderr, "%s %s\n", __func__, box->vname);
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
	if (box->deleting || (box->flags & MAILBOX_FLAG_SAVEONLY) != 0)
		return 0;
	if (*box->name == '\0' &&
	    (box->list->ns->flags & NAMESPACE_FLAG_INBOX_ANY) != 0) {
		/* Trying to open INBOX as the namespace prefix. */
		mail_storage_set_error(box->storage, MAIL_ERROR_NOTFOUND, "Mailbox isn't selectable");
		mailbox_close(box);
		return -1;
	}

	struct kp_mailbox *kbox = (void *)box;
	kbox->mapises = kpxx_login();
	if (kbox->mapises == NULL)
		return -1;
	kbox->mapistore = kpxx_store_get(kbox->mapises);
	if (kbox->mapistore == NULL)
		return -1;
	int ret = kpxx_folder_get(kbox->mapistore, box->name, &kbox->mapifld);
	if (ret == -ENOENT)
		mail_storage_set_error(box->storage, MAIL_ERROR_NOTFOUND, T_MAIL_ERR_MAILBOX_NOT_FOUND(box->vname));
	else if (ret == -EACCES)
		mail_storage_set_error(box->storage, MAIL_ERROR_PERM, "EACCES");
	else if (ret == -EINVAL)
		mail_storage_set_error(box->storage, MAIL_ERROR_NOTFOUND, "EINVAL");
	if (ret != 0)
		return -1;
	return 0;
}

static void kp_mailbox_close(struct mailbox *box)
{
	fprintf(stderr, "%s\n", __func__);
	struct kp_mailbox *kbox = (void *)box;
	kpxx_folder_put(kbox->mapifld);
	kpxx_store_put(kbox->mapistore);
	kpxx_logout(kbox->mapises);
	index_storage_mailbox_close(box);
}

static int kp_mailbox_get_metadata(struct mailbox *box,
    enum mailbox_metadata_items items, struct mailbox_metadata *md)
{
	if (items & MAILBOX_METADATA_GUID) {
		/* so use entryid instead once we have it */
		mail_generate_guid_128_hash(box->name, md->guid);
		items &= ~MAILBOX_METADATA_GUID;
	}
	return index_mailbox_get_metadata(box, items, md);
}

static struct mailbox_sync_context *kp_mailbox_sync_init(struct mailbox *m,
    enum mailbox_sync_flags f)
{
	return index_mailbox_sync_init(m, f, false);
}

static struct mail *kp_mail_alloc(struct mailbox_transaction_context *t,
    enum mail_fetch_field wanted_fields,
    struct mailbox_header_lookup_ctx *wanted_headers)
{
	pool_t pool = pool_alloconly_create("mail", 2048);
	struct kp_mail *mail = p_new(pool, struct kp_mail, 1);
	mail->super.mail.pool = pool;
	index_mail_init(&mail->super, t, wanted_fields, wanted_headers);
	return &mail->super.mail.mail;
}

static int kp_mail_get_stream(struct mail *_mail, bool get_body,
    struct message_size *hdr_size, struct message_size *body_size,
    struct istream **stream_r)
{
	return -1;
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
	.get_stream = kp_mail_get_stream,
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
		.get_metadata = kp_mailbox_get_metadata,
		.set_subscribed = index_storage_set_subscribed,
		.attribute_set = index_storage_attribute_set,
		.attribute_get = index_storage_attribute_get,
		.attribute_iter_init = index_storage_attribute_iter_init,
		.attribute_iter_next = index_storage_attribute_iter_next,
		.attribute_iter_deinit = index_storage_attribute_iter_deinit,
		.list_index_has_changed = index_storage_list_index_has_changed,
		.list_index_update_sync = index_storage_list_index_update_sync,
		.sync_init = kp_mailbox_sync_init,
		.sync_next = index_mailbox_sync_next,
		.sync_deinit = index_mailbox_sync_deinit,
		.sync_notify = NULL,
		.notify_changes = kp_notify_changes,
		.transaction_begin = index_transaction_begin,
		.transaction_commit = index_transaction_commit,
		.transaction_rollback = index_transaction_rollback,
		.get_private_flags_mask = NULL,
		.mail_alloc = kp_mail_alloc,
		.search_init = index_storage_search_init,
		.search_deinit = index_storage_search_deinit,
		.search_next_nonblock = index_storage_search_next_nonblock,
		.search_next_update_seq = index_storage_search_next_update_seq,
		.save_alloc = kp_save_alloc,
		.save_begin = kp_save_begin,
		.save_continue = kp_save_continue,
		.save_finish = kp_save_finish,
		.save_cancel = kp_save_cancel,
		.copy = mail_storage_copy,
		.transaction_save_commit_pre = NULL,
		.transaction_save_commit_post = NULL,
		.transaction_save_rollback = NULL,
		.is_inconsistent = index_storage_is_inconsistent,
	},
};

static struct mailbox_list kp_mblist_reg = {
	.name = "mapimblist",
	.props = MAILBOX_LIST_PROP_NO_ROOT | MAILBOX_LIST_PROP_AUTOCREATE_DIRS,
	.mailbox_name_max_length = MAILBOX_LIST_NAME_MAX_LENGTH,
	.v = {
		.alloc = kp_mblist_alloc,
		.init = kp_mblist_init,
		.deinit = kp_mblist_deinit,
		.get_hierarchy_sep = kp_mblist_hiersep,
		.get_vname = mailbox_list_default_get_vname,
		.get_storage_name = mailbox_list_default_get_storage_name,
		.get_path = kp_mblist_getpath,
		.get_temp_prefix = kp_mblist_tempprefix,
		.join_refpattern = NULL,
		.iter_init = kp_mblist_iterinit,
		.iter_next = kp_mblist_iternext,
		.iter_deinit = kp_mblist_iterdeinit,
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
