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

#ifndef __DAGENT_ARCHIVE_H
#define __DAGENT_ARCHIVE_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <mapix.h>

#include <kopano/mapi_ptr.h>
#include <list>
#include <memory>

class ArchiveResult _kc_final {
public:
	void AddMessage(KC::MessagePtr ptrMessage);
	void Undo(IMAPISession *lpSession);

private:
	std::list<KC::MessagePtr> m_lstMessages;
};


class Archive;
typedef std::unique_ptr<Archive> ArchivePtr;

class Archive _kc_final {
public:
	static HRESULT Create(IMAPISession *, ArchivePtr *);
	HRESULT HrArchiveMessageForDelivery(IMessage *lpMessage);
	HRESULT HrArchiveMessageForSending(IMessage *lpMessage, ArchiveResult *lpResult);

	bool HaveErrorMessage() const { return !m_strErrorMessage.empty(); }
	LPCTSTR GetErrorMessage() const { return m_strErrorMessage.c_str(); }

private:
	Archive(IMAPISession *);
	void SetErrorMessage(HRESULT hr, LPCTSTR lpszMessage);

	// Inhibit copying
	Archive(const Archive &) = delete;
	Archive &operator=(const Archive &) = delete;

	KC::MAPISessionPtr m_ptrSession;
	KC::tstring m_strErrorMessage;
};


#endif // ndef __DAGENT_ARCHIVE_H
