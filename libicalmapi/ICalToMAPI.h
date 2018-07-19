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

#ifndef ICALTOMAPI_H
#define ICALTOMAPI_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <string>
#include <list>
#include "icalitem.h"

#define IC2M_NO_RECIPIENTS	0x0001
#define IC2M_APPEND_ONLY	0x0002
#define IC2M_NO_ORGANIZER	0x0004
#define IC2M_NO_BODY		0x0008

namespace KC {

class ICalToMapi {
public:
	/*
	    - lpPropObj to lookup named properties
	    - Addressbook (Global AddressBook for looking up users)
	 */
	ICalToMapi(IMAPIProp *lpPropObj, LPADRBOOK lpAdrBook, bool bNoRecipients) : m_lpPropObj(lpPropObj), m_lpAdrBook(lpAdrBook), m_bNoRecipients(bNoRecipients) {};
	virtual ~ICalToMapi(void) = default;
	virtual HRESULT ParseICal(const std::string& strIcal, const std::string& strCharset, const std::string& strServerTZ, IMailUser *lpImailUser, ULONG ulFlags) = 0;
	virtual ULONG GetItemCount() = 0;
	virtual HRESULT GetItemInfo(ULONG ulPosition, eIcalType *lpType, time_t *lptLastModified, SBinary *lpUid) = 0;
	virtual HRESULT GetItem(ULONG ulPosition, ULONG ulFlags, LPMESSAGE lpMessage) = 0;
	virtual HRESULT GetFreeBusyInfo(time_t *lptstart, time_t *lptend, std::string *lpstrUID, std::list<std::string> **lplstUsers) = 0;

protected:
	LPMAPIPROP m_lpPropObj;
	LPADRBOOK m_lpAdrBook;
	bool m_bNoRecipients;
};

extern _kc_export HRESULT CreateICalToMapi(IMAPIProp *propobj, LPADRBOOK, bool no_recipients, ICalToMapi **ret);

} /* namespace */

#endif
