/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

// based on src/net/smtp/SMTPTransport.cpp, but with additions
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
#include <kopano/platform.h>
#include <memory>
#include <utility>
#include <kopano/tie.hpp>
#include <kopano/stringutil.h>
#include "MAPISMTPTransport.h"
#include <vmime/net/smtp/SMTPResponse.hpp>
#include <vmime/exception.hpp>
#include <vmime/platform.hpp>
#include <vmime/mailboxList.hpp>
#include <vmime/utility/filteredStream.hpp>
#include <vmime/utility/outputStreamSocketAdapter.hpp>
#include <vmime/utility/streamUtils.hpp>
#include <vmime/utility/stringUtils.hpp>
#include <vmime/net/defaultConnectionInfos.hpp>
#include <kopano/ECLogger.h>
#include <kopano/charset/traits.h>
#if VMIME_HAVE_SASL_SUPPORT
#	include <vmime/security/sasl/SASLContext.hpp>
#endif // VMIME_HAVE_SASL_SUPPORT
#if VMIME_HAVE_TLS_SUPPORT
#	include <vmime/net/tls/TLSSession.hpp>
#	include <vmime/net/tls/TLSSecuredConnectionInfos.hpp>
#endif // VMIME_HAVE_TLS_SUPPORT

// Helpers for service properties
#define GET_PROPERTY(type, prop) \
	(getInfos().getPropertyValue <type>(getSession(), \
		dynamic_cast <const SMTPServiceInfos&>(getInfos()).getProperties().prop))
#define HAS_PROPERTY(prop) \
	(getInfos().hasProperty(getSession(), \
		dynamic_cast <const SMTPServiceInfos&>(getInfos()).getProperties().prop))

// register new service, really hacked from (src/net/builtinServices.inl)
#include "serviceRegistration.inl"
REGISTER_SERVICE(smtp::MAPISMTPTransport, mapismtp, TYPE_TRANSPORT);

using namespace KC;

namespace vmime {
namespace net {
namespace smtp {

MAPISMTPTransport::MAPISMTPTransport(vmime::shared_ptr<session> sess,
    vmime::shared_ptr<security::authenticator> auth, const bool secured) :
	transport(sess, getInfosInstance(), auth), m_isSMTPS(secured)
{
}

MAPISMTPTransport::~MAPISMTPTransport()
{
	try
	{
		if (isConnected())
			disconnect();
		else if (m_socket)
			internalDisconnect();
	} catch (const vmime::exception &) {
		// Ignore
	}
}

void MAPISMTPTransport::connect()
{
	if (isConnected())
		throw exceptions::already_connected();

	const string address = GET_PROPERTY(string, PROPERTY_SERVER_ADDRESS);
	const port_t port = GET_PROPERTY(port_t, PROPERTY_SERVER_PORT);

	// Create the time-out handler
	if (getTimeoutHandlerFactory())
		m_timeoutHandler = getTimeoutHandlerFactory()->create();

	// Create and connect the socket
	// @note we don't want a timeout during the connect() call
	// because if we set this, the side-effect is when IPv6 is tried first, it will timeout
	// the handler will break the loop by returning false from the handleTimeOut() function.
	m_socket = getSocketFactory()->create();

#if VMIME_HAVE_TLS_SUPPORT
	if (m_isSMTPS)  // dedicated port/SMTPS
	{
		auto tlsSession = tls::TLSSession::create(getCertificateVerifier(), getSession()->getTLSProperties());
		auto tlsSocket = tlsSession->getSocket(m_socket);
		m_socket = tlsSocket;

		m_secured = true;
		m_cntInfos = vmime::make_shared<tls::TLSSecuredConnectionInfos>(address, port, tlsSession, tlsSocket);
	}
	else
#endif // VMIME_HAVE_TLS_SUPPORT
	{
		m_cntInfos = vmime::make_shared<defaultConnectionInfos>(address, port);
	}

	ec_log_debug("SMTP connecting to %s:%d", address.c_str(), port);
	m_socket->connect(address, port);
	ec_log_debug("SMTP server connected.");

	// Connection
	//
	// eg:  C: <connection to server>
	// ---  S: 220 smtp.domain.com Service ready
	auto resp = readResponse();
	if (resp->getCode() != 220) {
		internalDisconnect();
		throw exceptions::connection_greeting_error(resp->getText());
	}

	// Identification
	helo();

#if VMIME_HAVE_TLS_SUPPORT
	// Setup secured connection, if requested
	const bool tls = HAS_PROPERTY(PROPERTY_CONNECTION_TLS)
		&& GET_PROPERTY(bool, PROPERTY_CONNECTION_TLS);
	const bool tlsRequired = HAS_PROPERTY(PROPERTY_CONNECTION_TLS_REQUIRED)
		&& GET_PROPERTY(bool, PROPERTY_CONNECTION_TLS_REQUIRED);

	if (!m_isSMTPS && tls)  // only if not SMTPS
	{
		try
		{
			startTLS();
		} catch (const exceptions::command_error &) {
			/* Non-fatal error */
			if (tlsRequired)
				throw;
			/* else: TLS is not required, so do not bother */
		}
		// Fatal error
		catch (...)
		{
			throw;
		}

		// Must reissue a EHLO command [RFC-2487, 5.2]
		helo();
	}
#endif // VMIME_HAVE_TLS_SUPPORT

	// Authentication
	if (GET_PROPERTY(bool, PROPERTY_OPTIONS_NEEDAUTH))
		authenticate();
	else
		m_authentified = true;
}

void MAPISMTPTransport::helo()
{
	// First, try Extended SMTP (ESMTP)
	//
	// eg:  C: EHLO thismachine.ourdomain.com
	//      S: 250-smtp.theserver.com
	//      S: 250-AUTH CRAM-MD5 DIGEST-MD5
	//      S: 250-PIPELINING
	//      S: 250 SIZE 2555555555

	sendRequest("EHLO " + platform::getHandler()->getHostName());
	auto resp = readResponse();
	if (resp->getCode() != 250) {
		// Next, try "Basic" SMTP
		//
		// eg:  C: HELO thismachine.ourdomain.com
		//      S: 250 OK

		sendRequest("HELO " + platform::getHandler()->getHostName());
		resp = readResponse();
		if (resp->getCode() != 250) {
			internalDisconnect();
			throw exceptions::connection_greeting_error(resp->getLastLine().getText());
		}

		m_extendedSMTP = false;
		m_extensions.clear();
		return;
	}

	m_extendedSMTP = true;
	m_extensions.clear();

	// Get supported extensions from SMTP response
	// One extension per line, format is: EXT PARAM1 PARAM2...
	for (int i = 1, n = resp->getLineCount() ; i < n ; ++i)
	{
		const string line = resp->getLineAt(i).getText();
		std::istringstream iss(line);

		string ext;
		iss >> ext;

		std::vector <string> params;
		string param;

		// Special case: some servers send "AUTH=MECH [MECH MECH...]"
		if (ext.length() >= 5 && utility::stringUtils::toUpper(ext.substr(0, 5)) == "AUTH=")
		{
			params.emplace_back(utility::stringUtils::toUpper(ext.substr(5)));
			ext = "AUTH";
		}

		while (iss >> param)
			params.emplace_back(utility::stringUtils::toUpper(param));
		m_extensions[ext] = params;
	}
}

void MAPISMTPTransport::authenticate()
{
	if (!m_extendedSMTP)
	{
		internalDisconnect();
		throw exceptions::command_error("AUTH", "ESMTP not supported.");
	}

	getAuthenticator()->setService(vmime::dynamicCast<service>(shared_from_this()));

#if VMIME_HAVE_SASL_SUPPORT
	// First, try SASL authentication
	if (GET_PROPERTY(bool, PROPERTY_OPTIONS_SASL))
	{
		try
		{
			authenticateSASL();

			m_authentified = true;
			return;
		} catch (const exceptions::authentication_error &e) {
			if (!GET_PROPERTY(bool, PROPERTY_OPTIONS_SASL_FALLBACK))
			{
				// Can't fallback on normal authentication
				internalDisconnect();
				throw;
			}
			/* else: Ignore, will try normal authentication */
		} catch (const exception &e) {
			internalDisconnect();
			throw;
		}
	}
#endif // VMIME_HAVE_SASL_SUPPORT

	// No other authentication method is possible
	throw exceptions::authentication_error("All authentication methods failed");
}

#if VMIME_HAVE_SASL_SUPPORT

void MAPISMTPTransport::authenticateSASL()
{
	if (!vmime::dynamicCast<security::sasl::SASLAuthenticator>(getAuthenticator()))
		throw exceptions::authentication_error("No SASL authenticator available.");

	// Obtain SASL mechanisms supported by server from ESMTP extensions
	const std::vector <string> saslMechs =
		(m_extensions.find("AUTH") != m_extensions.end())
			? m_extensions["AUTH"] : std::vector <string>();

	if (saslMechs.empty())
		throw exceptions::authentication_error("No SASL mechanism available.");

	std::vector<vmime::shared_ptr<security::sasl::SASLMechanism> > mechList;
	auto saslContext = security::sasl::SASLContext::create();

	for (unsigned int i = 0 ; i < saslMechs.size() ; ++i)
	{
		try
		{
			mechList.emplace_back(saslContext->createMechanism(saslMechs[i]));
		} catch (const exceptions::no_such_mechanism &) {
			// Ignore mechanism
		}
	}

	if (mechList.empty())
		throw exceptions::authentication_error("No SASL mechanism available.");

	// Try to suggest a mechanism among all those supported
	auto suggestedMech = saslContext->suggestMechanism(mechList);
	if (!suggestedMech)
		throw exceptions::authentication_error("Unable to suggest SASL mechanism.");

	// Allow application to choose which mechanisms to use
	mechList = vmime::dynamicCast<security::sasl::SASLAuthenticator>(getAuthenticator())->
		getAcceptableMechanisms(mechList, suggestedMech);

	if (mechList.empty())
		throw exceptions::authentication_error("No SASL mechanism available.");

	// Try each mechanism in the list in turn
	for (unsigned int i = 0 ; i < mechList.size() ; ++i)
	{
		auto mech = mechList[i];
		auto saslSession = saslContext->createSession("smtp", getAuthenticator(), mech);
		saslSession->init();

		sendRequest("AUTH " + mech->getName());

		for (bool cont = true ; cont ; )
		{
			auto response = readResponse();
			switch (response->getCode())
			{
			case 235:
			{
				m_socket = saslSession->getSecuredSocket(m_socket);
				return;
			}
			case 334:
			{
				std::unique_ptr<byte_t[]> challenge, resp;
				size_t challengeLen = 0, respLen = 0;

				try
				{
					// Extract challenge
					saslContext->decodeB64(response->getText(), &unique_tie(challenge), &challengeLen);
					// Prepare response
					saslSession->evaluateChallenge(challenge.get(), challengeLen, &unique_tie(resp), &respLen);
					// Send response
					sendRequest(saslContext->encodeB64(resp.get(), respLen));
				} catch (const exceptions::sasl_exception &e) {
					// Cancel SASL exchange
					sendRequest("*");
				}
				break;
			}
			default:

				cont = false;
				break;
			}
		}
	}

	throw exceptions::authentication_error
		("Could not authenticate using SASL: all mechanisms failed.");
}

#endif // VMIME_HAVE_SASL_SUPPORT

#if VMIME_HAVE_TLS_SUPPORT

void MAPISMTPTransport::startTLS()
{
	try
	{
		sendRequest("STARTTLS");
		auto resp = readResponse();
		if (resp->getCode() != 220)
			throw exceptions::command_error("STARTTLS", resp->getText());

		auto tlsSession = tls::TLSSession::create(getCertificateVerifier(), getSession()->getTLSProperties());
		auto tlsSocket = tlsSession->getSocket(m_socket);
		tlsSocket->handshake();

		m_socket = tlsSocket;

		m_secured = true;
		m_cntInfos = vmime::make_shared<tls::TLSSecuredConnectionInfos>
			(m_cntInfos->getHost(), m_cntInfos->getPort(), tlsSession, tlsSocket);
	} catch (const exceptions::command_error &) {
		// Non-fatal error
		throw;
	} catch (const exception &) {
		// Fatal error
		internalDisconnect();
		throw;
	}
}

#endif // VMIME_HAVE_TLS_SUPPORT

bool MAPISMTPTransport::isConnected() const
{
	return (m_socket && m_socket->isConnected() && m_authentified);
}

void MAPISMTPTransport::disconnect()
{
	if (!isConnected())
		throw exceptions::not_connected();

	internalDisconnect();
}

void MAPISMTPTransport::internalDisconnect()
{
	try
	{
		sendRequest("QUIT");
	} catch (const exception &) {
		// Not important
	}

	m_socket->disconnect();
	m_socket = NULL;

	m_timeoutHandler = NULL;

	m_authentified = false;
	m_extendedSMTP = false;

	m_secured = false;
	m_cntInfos = NULL;
}

void MAPISMTPTransport::noop()
{
	if (!isConnected())
		throw exceptions::not_connected();

	sendRequest("NOOP");

	auto resp = readResponse();
	if (resp->getCode() != 250)
		throw exceptions::command_error("NOOP", resp->getText());
}

//                             
// Only this function is altered, to return per recipient failure.
//                             
void MAPISMTPTransport::send(const mailbox &expeditor,
    const mailboxList &recipients, utility::inputStream &is, size_t size,
    utility::progressListener *progress, const mailbox &sender)
{
	if (!isConnected())
		throw exceptions::not_connected();

	// If no recipient/expeditor was found, throw an exception
	if (recipients.isEmpty())
		throw exceptions::no_recipient();
	else if (expeditor.isEmpty())
		throw exceptions::no_expeditor();

	// Emit the "MAIL" command
	bool bDSN = m_bDSNRequest;
	
	if(bDSN && m_extensions.find("DSN") == m_extensions.end()) {
		ec_log_notice("SMTP server does not support Delivery Status Notifications (DSN)");
		bDSN = false; // Disable DSN because the server does not support this.
	}

	auto strSend = "MAIL FROM: <" + expeditor.getEmail().toString() + ">";
	if (bDSN) {
		strSend += " RET=HDRS";
		if (!m_strDSNTrackid.empty())
			strSend += " ENVID=" + m_strDSNTrackid;
	}

	sendRequest(strSend);
	auto resp = readResponse();
	if (resp->getCode() / 10 != 25) {
		internalDisconnect();
		throw exceptions::command_error("MAIL", resp->getText());
	}

	// Emit a "RCPT TO" command for each recipient
	mTemporaryFailedRecipients.clear();
	mPermanentFailedRecipients.clear();
	for (size_t i = 0 ; i < recipients.getMailboxCount(); ++i) {
		const mailbox& mbox = *recipients.getMailboxAt(i);
		unsigned int code;

		strSend = "RCPT TO: <" + mbox.getEmail().toString() + ">";
		if (bDSN)
			 strSend += " NOTIFY=SUCCESS,DELAY";

		sendRequest(strSend);
		resp = readResponse();
		code = resp->getCode();

		sFailedRecip entry;
		auto recip_name = mbox.getName().getConvertedText(charset(CHARSET_WCHAR));
		entry.strRecipName.assign(reinterpret_cast<const wchar_t *>(recip_name.c_str()), recip_name.length() / sizeof(wchar_t));
		entry.strRecipEmail = mbox.getEmail().toString();
		entry.ulSMTPcode = code;
		entry.strSMTPResponse = resp->getText();

		if (code / 10 == 25) {
			continue;
		} else if (code == 421) {
			/* 421 4.7.0 localhorse.lh Error: too many errors */
			ec_log_err("RCPT line gave SMTP error: %d %s. (and now?)",
				resp->getCode(), resp->getText().c_str());
			break;
		} else if (code / 100 == 5) {
			/*
			 * Example Postfix codes:
			 * 501 5.1.3 Bad recipient address syntax  (RCPT TO: <with spaces>)
			 * 550 5.1.1 <fox>: Recipient address rejected: User unknown in virtual mailbox table
			 * 550 5.7.1 REJECT action without code by means of e.g. /etc/postfix/header_checks
			 */
			mPermanentFailedRecipients.emplace_back(std::move(entry));
			ec_log_err("RCPT line gave SMTP error %d %s. (no retry)",
				resp->getCode(), resp->getText().c_str());
			continue;
		} else if (code / 100 != 4) {
			mPermanentFailedRecipients.emplace_back(std::move(entry));
			ec_log_err("RCPT line gave unexpected SMTP reply %d %s. (no retry)",
				resp->getCode(), resp->getText().c_str());
			continue;
		}

		/* Other 4xx codes (disk full, ... ?) */
		mTemporaryFailedRecipients.emplace_back(std::move(entry));
		ec_log_err("RCPT line gave SMTP error: %d %s. (will be retried)",
			resp->getCode(), resp->getText().c_str());
	}

	// Send the message data
	sendRequest("DATA");

	// we also stop here if all recipients failed before
	resp = readResponse();
	if (resp->getCode() != 354) {
		internalDisconnect();
		throw exceptions::command_error("DATA", format("%d %s", resp->getCode(), resp->getText().c_str()));
	}

	// Stream copy with "\n." to "\n.." transformation
	utility::outputStreamSocketAdapter sos(*m_socket);
	utility::dotFilteredOutputStream fos(sos);

	utility::bufferedStreamCopy(is, fos, size, progress);

	fos.flush();

	// Send end-of-data delimiter
	m_socket->sendRaw(reinterpret_cast<const vmime::byte_t *>("\r\n.\r\n"), 5);
	resp = readResponse();
	if (resp->getCode() != 250) {
		internalDisconnect();
		throw exceptions::command_error("DATA", format("%d %s", resp->getCode(), resp->getText().c_str()));
	}
	// postfix: 2.0.0 Ok: queued as B36E73608E
	// qmail: ok 1295860788 qp 29154
	// exim: OK id=1PhIZ9-0002Ko-Q8
	ec_log_debug("SMTP: %s", resp->getText().c_str());
}

void MAPISMTPTransport::requestDSN(BOOL bRequest, const std::string &strTrackid)
{
	m_bDSNRequest = bRequest;
	m_strDSNTrackid = strTrackid;
}

void MAPISMTPTransport::sendRequest(const string& buffer, const bool end)
{
	ec_log_debug("< %s", buffer.c_str());
	if (end)
		m_socket->send(buffer + "\r\n");
	else
		m_socket->send(buffer);
}

vmime::shared_ptr<SMTPResponse> MAPISMTPTransport::readResponse(void)
{
	vmime::shared_ptr<tracer> t;
	auto resp = SMTPResponse::readResponse(t, m_socket, m_timeoutHandler, m_response_state);
	m_response_state = resp->getCurrentState();
	ec_log_debug("> %d %s", resp->getCode(), resp->getText().c_str());
	return resp;
}

// Service infos

SMTPServiceInfos MAPISMTPTransport::sm_infos(false);

} // smtp
} // net
} // vmime
