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

class KC_EXPORT_DYCAST ECVMIMESender final : public ECSender {
private:
	KC_HIDDEN HRESULT HrMakeRecipientsList(IAddrBook *, IMessage *, vmime::shared_ptr<vmime::message>, vmime::mailboxList &recips, bool allow_everyone, bool always_expand_distlist);
	KC_HIDDEN HRESULT HrExpandGroup(IAddrBook *, const SPropValue *grp_name, const SPropValue *grp_eid, vmime::mailboxList &recips, std::set<std::wstring> &s_groups, std::set<std::wstring> &s_recips, bool allow_everyone);
	KC_HIDDEN HRESULT HrAddRecipsFromTable(IAddrBook *, IMAPITable *table, vmime::mailboxList &recips, std::set<std::wstring> &s_groups, std::set<std::wstring> &s_recips, bool allow_everyone, bool always_expand_distlist);

public:
	KC_HIDDEN ECVMIMESender(const std::string &host, int port);
	KC_HIDDEN HRESULT sendMail(IAddrBook *, IMessage *, vmime::shared_ptr<vmime::message>, bool allow_everyone, bool always_expand_distlist);
};

extern vmime::parsingContext imopt_default_parsectx();
extern vmime::generationContext imopt_default_genctx();

} /* namespace */

#endif
