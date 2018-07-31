/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef PROTOCOLBASE_H
#define PROTOCOLBASE_H

#include <kopano/memory.hpp>
#include <mapi.h>
#include "Http.h"

class ProtocolBase {
public:
	ProtocolBase(Http &, IMAPISession *, const std::string &srv_tz, const std::string &charset);
	virtual ~ProtocolBase() = default;
	HRESULT HrInitializeClass();
	virtual HRESULT HrHandleCommand(const std::string &strMethod) = 0;

protected:
	Http &m_lpRequest;
	KC::object_ptr<IMAPISession> m_lpSession;
	std::string m_strSrvTz;
	std::string m_strCharset;
	KC::object_ptr<IMsgStore> m_lpDefStore; //!< We always need the store of the user that is logged in.
	KC::object_ptr<IMsgStore> m_lpActiveStore; //!< The store we're acting on
	KC::object_ptr<IAddrBook> m_lpAddrBook;
	KC::object_ptr<IMailUser> m_lpLoginUser; //!< the logged in user
	KC::object_ptr<IMailUser> m_lpActiveUser; //!< the owner of m_lpActiveStore
	KC::object_ptr<IMAPIFolder> m_lpUsrFld; //!< The active folder (calendar, inbox, outbox)
	KC::object_ptr<IMAPIFolder> m_lpIPMSubtree; //!< IPMSubtree of active store, used for CopyFolder/CreateFolder
	KC::memory_ptr<SPropTagArray> m_lpNamedProps; //!< named properties of the active store
	std::wstring m_wstrFldOwner;   //!< url owner part
	std::wstring m_wstrFldName;	   //!< url foldername part
	std::wstring m_wstrUser;	//!< login username (http auth user)
	bool m_blFolderAccess = true; //!< can we delete the current folder
	ULONG m_ulUrlFlag = 0, m_ulFolderFlag = 0;
	KC::convert_context m_converter;

	std::string W2U(const std::wstring&); //!< convert widestring to UTF-8
	std::string W2U(const WCHAR* lpwWideChar);
	std::wstring U2W(const std::string&); //!< convert UTF-8 to widestring
	std::string SPropValToString(const SPropValue *lpSprop);
	std::string strAgent;
};

#endif
