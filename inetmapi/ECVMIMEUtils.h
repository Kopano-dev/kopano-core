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
#include <kopano/ECLogger.h>
#include <vmime/vmime.hpp>
#include <inetmapi/inetmapi.h>

class ECVMIMESender _kc_final : public ECSender {
private:
	HRESULT HrMakeRecipientsList(LPADRBOOK lpAdrBook, LPMESSAGE lpMessage, vmime::shared_ptr<vmime::message>, vmime::mailboxList &recipients, bool bAllowEveryone, bool bAlwaysExpandDistrList);
	HRESULT HrExpandGroup(LPADRBOOK lpAdrBook, LPSPropValue lpGroupName, LPSPropValue lpGroupEntryID, vmime::mailboxList &recipients, std::set<std::wstring> &setGroups, std::set<std::wstring> &setRecips, bool bAllowEveryone);
	HRESULT HrAddRecipsFromTable(LPADRBOOK lpAdrBook, IMAPITable *lpTable, vmime::mailboxList &recipients, std::set<std::wstring> &setGroups, std::set<std::wstring> &setRecips, bool bAllowEveryone, bool bAlwaysExpandDistrList);

public:
	ECVMIMESender(ECLogger *newlpLogger, std::string strSMTPHost, int port);
	HRESULT sendMail(LPADRBOOK lpAdrBook, LPMESSAGE lpMessage, vmime::shared_ptr<vmime::message>, bool bAllowEveryone, bool bAlwaysExpandDistrList);
};

#endif
