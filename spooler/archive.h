/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <mapidefs.h>
#include <mapix.h>
#include <kopano/mapi_ptr.h>
#include <kopano/memory.hpp>
#include <list>
#include <memory>

namespace KC { class ECLogger; }

class ArchiveResult final {
public:
	void AddMessage(KC::MessagePtr ptrMessage);
	void Undo(IMAPISession *lpSession);

private:
	std::list<KC::MessagePtr> m_lstMessages;
};


class Archive;
typedef std::unique_ptr<Archive> ArchivePtr;

class Archive final {
public:
	static HRESULT Create(IMAPISession *, ArchivePtr *);
	HRESULT HrArchiveMessageForDelivery(IMessage *lpMessage, std::shared_ptr<KC::ECLogger>);
	HRESULT HrArchiveMessageForSending(IMessage *lpMessage, ArchiveResult *lpResult, std::shared_ptr<KC::ECLogger>);

	bool HaveErrorMessage() const { return !m_strErrorMessage.empty(); }
	LPCTSTR GetErrorMessage() const { return m_strErrorMessage.c_str(); }

private:
	Archive(IMAPISession *);
	void SetErrorMessage(HRESULT hr, LPCTSTR lpszMessage);

	// Inhibit copying
	Archive(const Archive &) = delete;
	Archive &operator=(const Archive &) = delete;

	KC::object_ptr<IMAPISession> m_ptrSession;
	KC::tstring m_strErrorMessage;
};
