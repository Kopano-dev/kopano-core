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

#ifndef PROTOCOLBASE_H
#define PROTOCOLBASE_H

#include <kopano/memory.hpp>
#include <mapi.h>
#include "Http.h"

class ProtocolBase {
public:
	ProtocolBase(Http *, IMAPISession *, const std::string &srv_tz, const std::string &charset);
	virtual ~ProtocolBase() _kc_impdtor;
	HRESULT HrInitializeClass();

	virtual HRESULT HrHandleCommand(const std::string &strMethod) = 0;
	
protected:
	Http *m_lpRequest = nullptr;
	KCHL::object_ptr<IMAPISession> m_lpSession;
	std::string m_strSrvTz;
	std::string m_strCharset;
	KCHL::object_ptr<IMsgStore> m_lpDefStore; //!< We always need the store of the user that is logged in.
	KCHL::object_ptr<IMsgStore> m_lpActiveStore; //!< The store we're acting on
	KCHL::object_ptr<IAddrBook> m_lpAddrBook;
	KCHL::object_ptr<IMailUser> m_lpLoginUser; //!< the logged in user
	KCHL::object_ptr<IMailUser> m_lpActiveUser; //!< the owner of m_lpActiveStore
	KCHL::object_ptr<IMAPIFolder> m_lpUsrFld; //!< The active folder (calendar, inbox, outbox)
	KCHL::object_ptr<IMAPIFolder> m_lpIPMSubtree; //!< IPMSubtree of active store, used for CopyFolder/CreateFolder
	KCHL::memory_ptr<SPropTagArray> m_lpNamedProps; //!< named properties of the active store
	std::wstring m_wstrFldOwner;   //!< url owner part
	std::wstring m_wstrFldName;	   //!< url foldername part

	std::wstring m_wstrUser;	//!< login username (http auth user)
	bool m_blFolderAccess = true; //!< can we delete the current folder
	ULONG m_ulUrlFlag = 0;
	ULONG m_ulFolderFlag = 0;
	convert_context m_converter;

	std::string W2U(const std::wstring&); //!< convert widestring to UTF-8
	std::string W2U(const WCHAR* lpwWideChar);
	std::wstring U2W(const std::string&); //!< convert UTF-8 to widestring
	std::string SPropValToString(const SPropValue *lpSprop);
	std::string strAgent;
};

#endif
