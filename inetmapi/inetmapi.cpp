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

#include <mutex>
#include <kopano/platform.h>
#include <kopano/lockhelper.hpp>
#include <kopano/stringutil.h>

// Damn windows header defines max which break C++ header files
#undef max

#include <string>
#include <fstream>
#include <iostream>
#include <cstdlib>

// vmime
#include "vmime/vmime.hpp"
#include "vmime/textPartFactory.hpp"
#include "mapiTextPart.h"
#include "vmime/platforms/posix/posixHandler.hpp"

// mapi
#include <mapix.h>
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include <edkmdb.h>
#include <kopano/CommonUtil.h>
#include <kopano/charset/convert.h>
// inetmapi
#include <inetmapi/inetmapi.h>
#include "VMIMEToMAPI.h"
#include "MAPIToVMIME.h"
#include "ECVMIMEUtils.h"
#include "ECMapiUtils.h"
#include <kopano/ECLogger.h>
#include <kopano/mapi_ptr.h>

using namespace std;

bool ValidateCharset(const char *charset)
{
	iconv_t cd;
	cd = iconv_open(CHARSET_WCHAR, charset);
	if (cd == (iconv_t)(-1))
		return false;
		
	iconv_close(cd);
	return true;
}

ECSender::ECSender(const std::string &strSMTPHost, int port)
{
	smtpresult = 0;
	smtphost = strSMTPHost;
	smtpport = port;
}

int ECSender::getSMTPResult() {
	return smtpresult;
}

const WCHAR* ECSender::getErrorString() {
	return error.c_str();
}

void ECSender::setError(const std::wstring &newError) {
	error = newError;
}

void ECSender::setError(const std::string &newError) {
	error = convert_to<wstring>(newError);
}

bool ECSender::haveError() {
	return ! error.empty();
}

static std::mutex vmInitLock;
static void InitializeVMime()
{
	scoped_lock l_vm(vmInitLock);
	try {
		vmime::platform::getHandler();
	}
	catch (vmime::exceptions::no_platform_handler &) {
		vmime::platform::setHandler<vmime::platforms::posix::posixHandler>();
		// need to have a unique indentifier in the mediaType
		vmime::textPartFactory::getInstance()->registerType<vmime::mapiTextPart>(vmime::mediaType(vmime::mediaTypes::TEXT, "mapi"));
		// init our random engine for random message id generation
		rand_init();
	}
}

static string generateRandomMessageId()
{
#define IDLEN 38
	char id[IDLEN] = {0};
	// the same format as the vmime generator, but with more randomness
	snprintf(id, IDLEN, "kcim.%lx.%x.%08x%08x",
		static_cast<unsigned long>(time(NULL)), getpid(),
		rand_mt(), rand_mt());
	return string(id, strlen(id));
#undef IDLEN
}

INETMAPI_API ECSender *CreateSender(const std::string &smtp, int port)
{
	return new ECVMIMESender(smtp, port);
}

// parse rfc822 input, and set props in lpMessage
INETMAPI_API HRESULT IMToMAPI(IMAPISession *lpSession, IMsgStore *lpMsgStore,
    IAddrBook *lpAddrBook, IMessage *lpMessage, const string &input,
    delivery_options dopt)
{
	HRESULT hr = hrSuccess;
	VMIMEToMAPI *VMToM = NULL;
	
	// Sanitize options
	if(!ValidateCharset(dopt.default_charset)) {
		const char *charset = "iso-8859-15";
		ec_log_warn("Configured default_charset '%s' is invalid. Reverting to '%s'", dopt.default_charset, charset);
		dopt.default_charset = charset;
	}
	VMToM = new VMIMEToMAPI(lpAddrBook, dopt);
	InitializeVMime();

	// fill mapi object from buffer
	hr = VMToM->convertVMIMEToMAPI(input, lpMessage);

	delete VMToM;
	
	return hr;
}

// Read properties from lpMessage object and fill a buffer with internet rfc822 format message
INETMAPI_API HRESULT IMToINet(IMAPISession *lpSession, IAddrBook *lpAddrBook,
    IMessage *lpMessage, char **lppbuf, sending_options sopt)
{
	std::ostringstream oss;
	char *lpszData = NULL;
	HRESULT hr = IMToINet(lpSession, lpAddrBook, lpMessage, oss, sopt);
	if (hr != hrSuccess)
		return hr;
        
	lpszData = new char[oss.str().size()+1];
	strcpy(lpszData, oss.str().c_str());

	*lppbuf = lpszData;
	return hr;
}

INETMAPI_API HRESULT IMToINet(IMAPISession *lpSession, IAddrBook *lpAddrBook,
    IMessage *lpMessage, std::ostream &os, sending_options sopt)
{
	HRESULT			hr			= hrSuccess;
	LPSPropValue	lpTime		= NULL;
	LPSPropValue	lpMessageId	= NULL;
	auto mToVM = new MAPIToVMIME(lpSession, lpAddrBook, sopt);
	vmime::ref<vmime::message>	lpVMMessage	= NULL;
	vmime::utility::outputStreamAdapter adapter(os);

	InitializeVMime();

	hr = mToVM->convertMAPIToVMIME(lpMessage, &lpVMMessage);
	if (hr != hrSuccess)
		goto exit;

	try {
		// vmime messageBuilder has set Date header to now(), so we overwrite it.
		if (HrGetOneProp(lpMessage, PR_CLIENT_SUBMIT_TIME, &lpTime) == hrSuccess) {
			lpVMMessage->getHeader()->Date()->setValue(FiletimeTovmimeDatetime(lpTime->Value.ft));
		}
		// else, try PR_MESSAGE_DELIVERY_TIME, maybe other timestamps?

		if (HrGetOneProp(lpMessage, PR_INTERNET_MESSAGE_ID_A, &lpMessageId) == hrSuccess) {
			lpVMMessage->getHeader()->MessageId()->setValue(lpMessageId->Value.lpszA);
		}

		lpVMMessage->generate(adapter);
	}
	catch (vmime::exception&) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	catch (std::exception&) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	
exit:
	MAPIFreeBuffer(lpTime);
	MAPIFreeBuffer(lpMessageId);
	delete mToVM;
	return hr;
}

// Read properties from lpMessage object and to internet rfc2822 format message
// then send it using the provided ECSender object
INETMAPI_API HRESULT IMToINet(IMAPISession *lpSession, IAddrBook *lpAddrBook,
    IMessage *lpMessage, ECSender *mailer_base, sending_options sopt)
{
	HRESULT			hr	= hrSuccess;
	auto mToVM = new MAPIToVMIME(lpSession, lpAddrBook, sopt);
	vmime::ref<vmime::message>	vmMessage;
	ECVMIMESender		*mailer	= dynamic_cast<ECVMIMESender*>(mailer_base);
	wstring			wstrError;
	SPropArrayPtr	ptrProps;
	SizedSPropTagArray(2, sptaForwardProps) = { 2, { PR_AUTO_FORWARDED, PR_INTERNET_MESSAGE_ID_A } };
	ULONG cValues = 0;

	if (!mailer) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	InitializeVMime();

	hr = mToVM->convertMAPIToVMIME(lpMessage, &vmMessage);

	if (hr != hrSuccess) {
		wstrError = mToVM->getConversionError();
		if (wstrError.empty())
			wstrError = L"No error details specified";

		mailer->setError(L"Conversion error: " + wstringify(hr, true) + L". " + wstrError + L". Your email is not sent at all and cannot be retried.");
		goto exit;
	}

	try {
		vmime::messageId msgId;
		hr = lpMessage->GetProps(sptaForwardProps, 0, &cValues, &ptrProps);
		if (!FAILED(hr) && ptrProps[0].ulPropTag == PR_AUTO_FORWARDED && ptrProps[0].Value.b == TRUE && ptrProps[1].ulPropTag == PR_INTERNET_MESSAGE_ID_A) {
			// only allow mapi programs to set a messageId for an outgoing message when it comes from rules processing
			msgId = ptrProps[1].Value.lpszA;
		} else {
			// vmime::messageId::generateId() is not random enough since we use forking in the spooler
			msgId = vmime::messageId(generateRandomMessageId(), vmime::platform::getHandler()->getHostName());
		}
		hr = hrSuccess;
		vmMessage->getHeader()->MessageId()->setValue(msgId);
		ec_log_debug("Sending message with Message-ID: " + msgId.getId());
	}
	catch (vmime::exception& e) {
		mailer->setError(e.what());
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	catch (std::exception& e) {
		mailer->setError(e.what());
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	catch (...) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	
	hr = mailer->sendMail(lpAddrBook, lpMessage, vmMessage, sopt.allow_send_to_everyone, sopt.always_expand_distr_list);

exit:
	delete mToVM;

	return hr;
}

/** 
 * Create BODY and BODYSTRUCTURE strings for IMAP.
 * 
 * @param[in] input an RFC-822 email
 * @param[out] lpSimple optional BODY result
 * @param[out] lpExtended optional BODYSTRUCTURE result
 * 
 * @return MAPI Error code
 */
INETMAPI_API HRESULT createIMAPProperties(const std::string &input, std::string *lpEnvelope, std::string *lpBody, std::string *lpBodyStructure)
{
	InitializeVMime();

	VMIMEToMAPI VMToM;

	return VMToM.createIMAPProperties(input, lpEnvelope, lpBody, lpBodyStructure);
}

