/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef USERUTIL_INCLUDED
#define USERUTIL_INCLUDED

#include <kopano/zcdefs.h>
#include <set>
#include <list>
#include <string>

#include <mapidefs.h>
#include <mapix.h>

namespace KC {

extern _kc_export HRESULT GetArchivedUserList(IMAPISession *, const char *sslkey, const char *sslpass, std::list<std::string> *users, bool local_only = false);
extern _kc_export HRESULT GetArchivedUserList(IMAPISession *, const char *sslkey, const char *sslpass, std::list<std::wstring> *users, bool local_only = false);

class _kc_export DataCollector {
public:
	_kc_hidden virtual HRESULT GetRequiredPropTags(LPMAPIPROP, LPSPropTagArray *) const;
	virtual HRESULT GetRestriction(LPMAPIPROP lpProp, LPSRestriction *lppRestriction);
	_kc_hidden virtual HRESULT CollectData(LPMAPITABLE store_table) = 0;
};

extern _kc_export HRESULT GetMailboxData(IMAPISession *, const char *sslkey, const char *sslpass, bool local_only, DataCollector *);

} /* namespace */

#endif
