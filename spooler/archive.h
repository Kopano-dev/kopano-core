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

#include <mapidefs.h>
#include <mapix.h>

#include <kopano/mapi_ptr.h>
#include <kopano/tstring.h>

#include <list>
#include <memory>

class ECLogger;

class ArchiveResult {
public:
	void AddMessage(MessagePtr ptrMessage);
	void Undo(IMAPISession *lpSession);

private:
	std::list<MessagePtr> m_lstMessages;
};


class Archive;
typedef std::unique_ptr<Archive> ArchivePtr;

class Archive {
public:
	static HRESULT Create(IMAPISession *lpSession, ECLogger *lpLogger, ArchivePtr *lpptrArchive);
	~Archive();

	HRESULT HrArchiveMessageForDelivery(IMessage *lpMessage);
	HRESULT HrArchiveMessageForSending(IMessage *lpMessage, ArchiveResult *lpResult);

	bool HaveErrorMessage() const { return !m_strErrorMessage.empty(); }
	LPCTSTR GetErrorMessage() const { return m_strErrorMessage.c_str(); }

private:
	Archive(IMAPISession *lpSession, ECLogger *lpLogger);
	void SetErrorMessage(HRESULT hr, LPCTSTR lpszMessage);

	// Inhibit copying
	Archive(const Archive&);
	Archive& operator=(const Archive&);

private:
	MAPISessionPtr	m_ptrSession;
	ECLogger		*m_lpLogger;
	tstring			m_strErrorMessage;
};


#endif // ndef __DAGENT_ARCHIVE_H
