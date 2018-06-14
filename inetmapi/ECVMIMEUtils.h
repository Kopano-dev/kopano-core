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

#ifndef ECVMIMEUTILS
#define ECVMIMEUTILS

#include <memory>
#include <string>
#include <set>
#include <kopano/zcdefs.h>
#include <vmime/vmime.hpp>
#include <inetmapi/inetmapi.h>

namespace KC {

class _kc_export_dycast ECVMIMESender _kc_final : public ECSender {
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
