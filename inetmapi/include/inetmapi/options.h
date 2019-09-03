/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef __INETMAPI_OPTIONS_H
#define __INETMAPI_OPTIONS_H

#include <kopano/zcdefs.h>
#include <map>
#include <string>

namespace KC {

struct delivery_options {
	bool use_received_date;			// Use the 'received' date instead of the current date as delivery date
	bool mark_as_read;				// Deliver the message 'read' instead of unread
	bool add_imap_data;				// Save IMAP optimizations to the server
	bool parse_smime_signed;		// Parse actual S/MIME content instead of just writing out the S/MIME data to a single attachment

	/*
	 * If @charset_strict_rfc is false, VMIMEToMAPI will try to
	 * re-interpret {messages with unexpected characters} in character
	 * sets other than the one specified in the mail header, which may
	 * worsen the result.
	 */
	bool charset_strict_rfc;

	/* Enables the joining of HTML parts (one document's stylesheet can hack another). */
	bool insecure_html_join;

	LPSBinary user_entryid;			// If not NULL, specifies the entryid of the user for whom we are delivering. If set, allows generating PR_MESSAGE_*_ME properties.
	const char *ascii_upgrade; // Upgrade ASCII parts to this new (ASCII-compatible) charset
	bool html_safety_filter;
	std::map<std::string, std::string> cset_subst; /* custom substitutions for broken charsets */
	std::vector<std::string> indexed_headers; /* the headers we want to index in the PS_INTERNET_HEADERS namespace */
	bool header_strict_rfc, conversion_notices;
};

struct sending_options {
	char *alternate_boundary;		// Specifies a specific boundary prefix to use when creating MIME boundaries
	char *charset_upgrade;
	int use_tnef;					// -1: minimize usage, 0: autodetect, 1: force
	bool no_recipients_workaround;	// Specified that we wish to accepts messages with no recipients (for example, when converting an attached email with no recipients)
	bool msg_in_msg, headers_only, add_received_date, allow_send_to_everyone;
	bool enable_dsn;				/**< Enable SMTP Delivery Status Notifications */
	bool always_expand_distr_list, ignore_missing_attachments;
};

extern _kc_export void imopt_default_delivery_options(delivery_options *);
extern _kc_export void imopt_default_sending_options(sending_options *);

} /* namespace */

#endif
