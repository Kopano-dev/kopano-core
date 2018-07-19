/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
