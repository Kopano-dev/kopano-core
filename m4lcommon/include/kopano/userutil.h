/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef USERUTIL_INCLUDED
#define USERUTIL_INCLUDED

#include <kopano/zcdefs.h>
#include <list>
#include <string>
#include <mapidefs.h>
#include <mapix.h>

namespace KC {

extern KC_EXPORT HRESULT GetArchivedUserList(IMAPISession *, const char *sslkey, const char *sslpass, std::list<std::string> *users, bool local_only = false);
extern KC_EXPORT HRESULT GetArchivedUserList(IMAPISession *, const char *sslkey, const char *sslpass, std::list<std::wstring> *users, bool local_only = false);

class KC_EXPORT DataCollector {
public:
	KC_HIDDEN virtual HRESULT GetRequiredPropTags(IMAPIProp *, SPropTagArray **) const;
	virtual HRESULT GetRestriction(LPMAPIPROP lpProp, LPSRestriction *lppRestriction);
	KC_HIDDEN virtual HRESULT CollectData(IMAPITable *store_table) = 0;
};

extern KC_EXPORT HRESULT GetMailboxData(IMAPISession *, const char *sslkey, const char *sslpass, bool local_only, DataCollector *);

} /* namespace */

#endif
