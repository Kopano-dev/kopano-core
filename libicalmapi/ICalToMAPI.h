/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
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
	virtual ~ICalToMapi() = default;
	virtual HRESULT ParseICal2(const char *ical, const std::string &charset, const std::string &server_tz, IMailUser *, unsigned int flags) = 0;
	HRESULT ParseICal(const std::string &ical, const std::string &charset, const std::string &server_tz, IMailUser *mu, unsigned int flags)
	{
		return ParseICal2(ical.c_str(), charset, server_tz, mu, flags);
	}
	virtual ULONG GetItemCount() = 0;
	virtual HRESULT GetItemInfo(ULONG ulPosition, eIcalType *lpType, time_t *lptLastModified, SBinary *lpUid) = 0;
	virtual HRESULT GetItem(ULONG ulPosition, ULONG ulFlags, LPMESSAGE lpMessage) = 0;
	virtual HRESULT GetFreeBusyInfo(time_t *start, time_t *end, std::string *uid, const std::list<std::string> **users) = 0;

protected:
	LPMAPIPROP m_lpPropObj;
	LPADRBOOK m_lpAdrBook;
	bool m_bNoRecipients;
};

extern KC_EXPORT HRESULT CreateICalToMapi(IMAPIProp *propobj, IAddrBook *, bool no_recipients, ICalToMapi **ret);

} /* namespace */
