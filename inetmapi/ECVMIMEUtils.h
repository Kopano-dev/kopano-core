/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECVMIMEUTILS
#define ECVMIMEUTILS

#include <memory>
#include <string>
#include <set>
#include <kopano/zcdefs.h>
#include <vmime/vmime.hpp>
#include <inetmapi/inetmapi.h>

namespace KC {

class _kc_export_dycast ECVMIMESender final : public ECSender {
private:
	_kc_hidden HRESULT HrMakeRecipientsList(LPADRBOOK, LPMESSAGE, vmime::shared_ptr<vmime::message>, vmime::mailboxList &recips, bool allow_everyone, bool always_expand_distlist);
	_kc_hidden HRESULT HrExpandGroup(LPADRBOOK, const SPropValue *grp_name, const SPropValue *grp_eid, vmime::mailboxList &recips, std::set<std::wstring> &s_groups, std::set<std::wstring> &s_recips, bool allow_everyone);
	_kc_hidden HRESULT HrAddRecipsFromTable(LPADRBOOK, IMAPITable *table, vmime::mailboxList &recips, std::set<std::wstring> &s_groups, std::set<std::wstring> &s_recips, bool allow_everyone, bool always_expand_distlist);

public:
	_kc_hidden ECVMIMESender(const std::string &host, int port);
	_kc_hidden HRESULT sendMail(LPADRBOOK, LPMESSAGE, vmime::shared_ptr<vmime::message>, bool allow_everyone, bool always_expand_distlist);
};

extern vmime::parsingContext imopt_default_parsectx();
extern vmime::generationContext imopt_default_genctx();

} /* namespace */

#endif
