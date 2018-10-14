/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

// based on vmime/messaging/smtp/SMTPTransport.hpp, but with additions
// we cannot use a class derived from SMTPTransport, since that class has alot of privates

//
// VMime library (http://www.vmime.org)
// Copyright (C) 2002-2009 Vincent Richard <vincent@vincent-richard.net>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 3 of
// the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Linking this library statically or dynamically with other modules is making
// a combined work based on this library.  Thus, the terms and conditions of
// the GNU General Public License cover the whole combination.
//

#ifndef MAPI_NET_SMTP_SMTPTRANSPORT_HPP_INCLUDED
#define MAPI_NET_SMTP_SMTPTRANSPORT_HPP_INCLUDED

#include <vmime/config.hpp>
#include <vmime/net/transport.hpp>
#include <vmime/net/socket.hpp>
#include <vmime/net/timeoutHandler.hpp>
#include <vmime/net/smtp/SMTPResponse.hpp>
#include <vmime/net/smtp/SMTPServiceInfos.hpp>
#include <inetmapi/inetmapi.h>

namespace vmime {
namespace net {
namespace smtp {


class SMTPResponse;


/** SMTP transport service.
  */
class MAPISMTPTransport final : public transport {
public:

	MAPISMTPTransport(vmime::shared_ptr<session> sess, vmime::shared_ptr<security::authenticator> auth, const bool secured = false);
	~MAPISMTPTransport();

	const std::string getProtocolName(void) const { return "mapismtp"; }

	static const serviceInfos &getInfosInstance(void) { return sm_infos; }
	const serviceInfos& getInfos() const { return sm_infos; }

	void connect();
	bool isConnected() const;
	void disconnect();

	void noop();

	void send(const mailbox &expeditor, const mailboxList &recipients, utility::inputStream &, size_t, utility::progressListener * = NULL, const mailbox &sender = mailbox());

	bool isSecuredConnection(void) const { return m_secured; }
	vmime::shared_ptr<connectionInfos> getConnectionInfos(void) const { return m_cntInfos; }

	// additional functions
	const std::vector<KC::sFailedRecip> &getPermanentFailedRecipients(void) const { return mPermanentFailedRecipients; }
	const std::vector<KC::sFailedRecip> &getTemporaryFailedRecipients(void) const { return mTemporaryFailedRecipients; }
	void requestDSN(BOOL bRequest, const std::string &strTrackid);

private:

	void sendRequest(const string& buffer, const bool end = true);
	vmime::shared_ptr<SMTPResponse> readResponse(void);

	void internalDisconnect();

	void helo();
	void authenticate();
#if VMIME_HAVE_SASL_SUPPORT
	void authenticateSASL();
#endif // VMIME_HAVE_SASL_SUPPORT

#if VMIME_HAVE_TLS_SUPPORT
	void startTLS();
#endif // VMIME_HAVE_TLS_SUPPORT

	vmime::shared_ptr<socket> m_socket;
	bool m_authentified = false;
	bool m_extendedSMTP = false;
	std::map <string, std::vector <string> > m_extensions;
	vmime::shared_ptr<timeoutHandler> m_timeoutHandler;

	const bool m_isSMTPS;
	bool m_secured = false;
	vmime::shared_ptr<connectionInfos> m_cntInfos;


	// Service infos
	static SMTPServiceInfos sm_infos;

	// additional data
	std::vector<KC::sFailedRecip> mTemporaryFailedRecipients;
	std::vector<KC::sFailedRecip> mPermanentFailedRecipients;
	bool m_bDSNRequest = false;
	std::string m_strDSNTrackid;
	SMTPResponse::state m_response_state;
};


} // smtp
} // net
} // vmime


#endif // MAPI_NET_SMTP_SMTPTRANSPORT_HPP_INCLUDED
