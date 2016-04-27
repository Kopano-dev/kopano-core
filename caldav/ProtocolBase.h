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

#include <mapi.h>
#include "Http.h"

class ProtocolBase
{
public:
	ProtocolBase(Http *lpRequest, IMAPISession *lpSession, ECLogger *lpLogger, std::string strSrvTz, std::string strCharset);
	virtual ~ProtocolBase();

	HRESULT HrInitializeClass();

	virtual HRESULT HrHandleCommand(const std::string &strMethod) = 0;
	
protected:
	Http *m_lpRequest;
	IMAPISession *m_lpSession;
	ECLogger *m_lpLogger;
	std::string m_strSrvTz;
	std::string m_strCharset;

	IMsgStore *m_lpDefStore;		//!< We always need the store of the user that is logged in.
	IMsgStore *m_lpActiveStore;		//!< The store we're acting on
	IAddrBook *m_lpAddrBook;
	IMailUser *m_lpLoginUser;		//!< the logged in user
	IMailUser *m_lpActiveUser;		//!< the owner of m_lpActiveStore
	IMAPIFolder *m_lpUsrFld;		//!< The active folder (calendar, inbox, outbox)
	IMAPIFolder *m_lpIPMSubtree;	//!< IPMSubtree of active store, used for CopyFolder/CreateFolder

	SPropTagArray *m_lpNamedProps; //!< named properties of the active store
	std::wstring m_wstrFldOwner;   //!< url owner part
	std::wstring m_wstrFldName;	   //!< url foldername part

	std::wstring m_wstrUser;	//!< login username (http auth user)

	bool m_blFolderAccess;		//!< can we delete the current folder
	ULONG m_ulUrlFlag;
	ULONG m_ulFolderFlag;

	convert_context m_converter;

	std::string W2U(const std::wstring&); //!< convert widestring to utf-8
	std::string W2U(const WCHAR* lpwWideChar);
	std::wstring U2W(const std::string&); //!< convert utf-8 to widestring
	std::string SPropValToString(SPropValue * lpSprop);
};

#endif
