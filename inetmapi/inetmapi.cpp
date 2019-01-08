/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <exception>
#include <mutex>
#include <utility>
#include <kopano/platform.h>
#include <kopano/stringutil.h>
#include <string>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <vmime/vmime.hpp>
#include <vmime/textPartFactory.hpp>
#include "mapiTextPart.h"
#include <vmime/platforms/posix/posixHandler.hpp>
#include <mapix.h>
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include <edkmdb.h>
#include <kopano/CommonUtil.h>
#include <kopano/charset/convert.h>
#include <inetmapi/inetmapi.h>
#include "VMIMEToMAPI.h"
#include "MAPIToVMIME.h"
#include "ECVMIMEUtils.h"
#include "ECMapiUtils.h"
#include <kopano/ECLogger.h>
#include <kopano/mapi_ptr.h>

using std::string;
using std::wstring;

namespace KC {

ECSender::ECSender(const std::string &strSMTPHost, int port) :
	smtphost(strSMTPHost), smtpport(port)
{}

void ECSender::setError(const std::string &newError) {
	error = convert_to<wstring>(newError);
}

static std::mutex vmInitLock;
static bool vmimeInitialized = false;

static void InitializeVMime()
{
	scoped_lock l_vm(vmInitLock);
	try {
		vmime::platform::getHandler();
	} catch (const vmime::exceptions::no_platform_handler &) {
		vmime::platform::setHandler<vmime::platforms::posix::posixHandler>();
	}
	if (vmimeInitialized)
		return;

	vmime::generationContext::getDefaultContext().setWrapMessageId(false);
	/* Sucky Outlook _still_ does not know RFC 2231. */
	vmime::generationContext::getDefaultContext().setEncodedParameterValueMode(vmime::generationContext::EncodedParameterValueModes::PARAMETER_VALUE_RFC2231_AND_RFC2047);
	// need to have a unique indentifier in the mediaType
	vmime::textPartFactory::getInstance()->registerType<vmime::mapiTextPart>(vmime::mediaType(vmime::mediaTypes::TEXT, "mapi"));
	// init our random engine for random message id generation
	rand_init();
	vmimeInitialized = true;
}

static std::string generate_message_id(IMessage *msg)
{
#define IDLEN 38
	memory_ptr<SPropValue> prop;
	auto hr = HrGetOneProp(msg, PR_SEARCH_KEY, &~prop);
	if (hr != hrSuccess) {
		char id[IDLEN] = {0};
		// Fallback: the same format as the vmime generator
		// but with more randomness
		snprintf(id, IDLEN, "kcim.%lx.%x.%08x%08x",
			static_cast<unsigned long>(time(NULL)), getpid(),
			rand_mt(), rand_mt());
		return string(id, strlen(id));
	}
	return "kcis." + bin2hex(prop->Value.bin);
#undef IDLEN
}

ECSender *CreateSender(const std::string &smtp, int port)
{
	return new ECVMIMESender(smtp, port);
}

/*
 * Because it calls iconv_open() with @s in at least one of iconv_open's two
 * argument, this function also implicitly checks whether @s is valid.
 */
static bool vtm_ascii_compatible(const char *s)
{
	static const char in[] = {
		0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,
		24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,
		45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,
		66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,
		87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,
		106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,
		121,122,123,124,125,126,127,
	};
	char out[sizeof(in)];
	iconv_t cd = iconv_open(s, "us-ascii");
	if (cd == reinterpret_cast<iconv_t>(-1))
		return false;
	auto inbuf = const_cast<char *>(in), outbuf = out;
	size_t insize = sizeof(in), outsize = sizeof(out);
	bool mappable = iconv(cd, &inbuf, &insize, &outbuf, &outsize) != static_cast<size_t>(-1);
	iconv_close(cd);
	return mappable && memcmp(in, out, sizeof(in)) == 0;
}

// parse rfc822 input, and set props in lpMessage
HRESULT IMToMAPI(IMAPISession *lpSession, IMsgStore *lpMsgStore,
    IAddrBook *lpAddrBook, IMessage *lpMessage, const string &input,
    delivery_options dopt)
{
	// Sanitize options
	if (dopt.ascii_upgrade == nullptr || *dopt.ascii_upgrade == '\0') {
		dopt.ascii_upgrade = "us-ascii";
	} else if (!vtm_ascii_compatible(dopt.ascii_upgrade)) {
		ec_log_warn("Configured default_charset \"%s\" is unknown, or not ASCII compatible. "
			"Disabling forced charset upgrades.",
			dopt.ascii_upgrade);
		dopt.ascii_upgrade = "us-ascii";
	}
	InitializeVMime();

	static bool vmime_once;
	if (!vmime_once) {
		vmime_once = true;
		vmime::mailbox mbox;
		mbox.parse("=?UTF-8?Q?a=c2=a0b_=28c@d.e=29?= <f@g.h>");
		if (*mbox.getName().getWholeBuffer().c_str() == '\0')
			ec_log_notice("K-1242: Detected old libvmime (< 0.9.2.42). "
			"Consider having it upgraded to be able to parse more broken mails (KC-1124).");
		vmime::header hdr;
		if (hdr.ReplyTo()->getValue<vmime::mailboxList>() == nullptr)
			ec_log_notice("K-1243: Detected old libvmime that "
			"is unable to parse multi-address Reply-To (KC-434).");
	}

	// fill mapi object from buffer
	return VMIMEToMAPI(lpAddrBook, std::move(dopt)).convertVMIMEToMAPI(input, lpMessage);
}

// Read properties from lpMessage object and fill a buffer with internet rfc822 format message
HRESULT IMToINet(IMAPISession *lpSession, IAddrBook *lpAddrBook,
    IMessage *lpMessage, char **lppbuf, sending_options sopt)
{
	std::ostringstream oss;
	HRESULT hr = IMToINet(lpSession, lpAddrBook, lpMessage, oss, sopt);
	if (hr != hrSuccess)
		return hr;
	auto lpszData = new char[oss.str().size()+1];
	strcpy(lpszData, oss.str().c_str());

	*lppbuf = lpszData;
	return hr;
}

HRESULT IMToINet(IMAPISession *lpSession, IAddrBook *lpAddrBook,
    IMessage *lpMessage, std::ostream &os, sending_options sopt)
{
	memory_ptr<SPropValue> lpTime, lpMessageId;
	MAPIToVMIME mToVM(lpSession, lpAddrBook, sopt);
	vmime::shared_ptr<vmime::message> lpVMMessage;
	vmime::utility::outputStreamAdapter adapter(os);

	InitializeVMime();
	auto hr = mToVM.convertMAPIToVMIME(lpMessage, &lpVMMessage);
	if (hr != hrSuccess)
		return hr;

	try {
		// vmime messageBuilder has set Date header to now(), so we overwrite it.
		if (HrGetOneProp(lpMessage, PR_CLIENT_SUBMIT_TIME, &~lpTime) == hrSuccess)
			lpVMMessage->getHeader()->Date()->setValue(FiletimeTovmimeDatetime(lpTime->Value.ft));
		// else, try PR_MESSAGE_DELIVERY_TIME, maybe other timestamps?

		vmime::messageId msgid;
		if (HrGetOneProp(lpMessage, PR_INTERNET_MESSAGE_ID_A, &~lpMessageId) == hrSuccess)
			msgid = lpMessageId->Value.lpszA;
		else
			msgid = vmime::messageId(generate_message_id(lpMessage), vmime::platform::getHandler()->getHostName());
		lpVMMessage->getHeader()->MessageId()->setValue(msgid);

		lpVMMessage->generate(adapter);
	} catch (const vmime::exception &) {
		return MAPI_E_NOT_FOUND;
	} catch (const std::exception &) {
		return MAPI_E_NOT_FOUND;
	}
	return hrSuccess;
}

// Read properties from lpMessage object and to internet rfc2822 format message
// then send it using the provided ECSender object
HRESULT IMToINet(IMAPISession *lpSession, IAddrBook *lpAddrBook,
    IMessage *lpMessage, ECSender *mailer_base, sending_options sopt)
{
	MAPIToVMIME mToVM(lpSession, lpAddrBook, sopt);
	vmime::shared_ptr<vmime::message> vmMessage;
	auto mailer = dynamic_cast<ECVMIMESender *>(mailer_base);
	SPropArrayPtr	ptrProps;
	static constexpr const SizedSPropTagArray(2, sptaForwardProps) =
		{2, {PR_AUTO_FORWARDED, PR_INTERNET_MESSAGE_ID_A}};
	ULONG cValues = 0;

	if (mailer == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	InitializeVMime();
	auto hr = mToVM.convertMAPIToVMIME(lpMessage, &vmMessage, MTV_SPOOL);
	if (hr != hrSuccess) {
		std::wstring wstrError = mToVM.getConversionError();
		if (wstrError.empty())
			wstrError = L"No error details specified";
		mailer->setError(L"Conversion error: " + wstringify_hex(hr) + L". " + wstrError + L". Your email is not sent at all and cannot be retried.");
		return hr;
	}

	try {
		vmime::messageId msgId;
		hr = lpMessage->GetProps(sptaForwardProps, 0, &cValues, &~ptrProps);
		if (!FAILED(hr) && ptrProps[0].ulPropTag == PR_AUTO_FORWARDED &&
		    ptrProps[0].Value.b && ptrProps[1].ulPropTag == PR_INTERNET_MESSAGE_ID_A)
			// only allow mapi programs to set a messageId for an outgoing message when it comes from rules processing
			msgId = ptrProps[1].Value.lpszA;
		else
			// vmime::messageId::generateId() is not random enough since we use forking in the spooler
			msgId = vmime::messageId(generate_message_id(lpMessage), vmime::platform::getHandler()->getHostName());
		vmMessage->getHeader()->MessageId()->setValue(msgId);
		ec_log_debug("Sending message with Message-ID: " + msgId.getId());
	} catch (const vmime::exception &e) {
		mailer->setError(e.what());
		return MAPI_E_NOT_FOUND;
	} catch (const std::exception &e) {
		mailer->setError(e.what());
		return MAPI_E_NOT_FOUND;
	}
	catch (...) {
		return MAPI_E_NOT_FOUND;
	}
	return mailer->sendMail(lpAddrBook, lpMessage, vmMessage,
	       sopt.allow_send_to_everyone, sopt.always_expand_distr_list);
}

/** 
 * Create BODY and BODYSTRUCTURE strings for IMAP.
 * 
 * @param[in] input an RFC 2822 email
 * @param[out] lpSimple optional BODY result
 * @param[out] lpExtended optional BODYSTRUCTURE result
 * 
 * @return MAPI Error code
 */
HRESULT createIMAPProperties(const std::string &input, std::string *lpEnvelope,
    std::string *lpBody, std::string *lpBodyStructure)
{
	InitializeVMime();
	return VMIMEToMAPI().createIMAPProperties(input, lpEnvelope, lpBody, lpBodyStructure);
}

HRESULT createIMAPBody(const std::string &input, IMessage *lpMessage, bool envelope)
{
	InitializeVMime();

	VMIMEToMAPI obj;
	auto vmMessage = vmime::make_shared<vmime::message>();
	vmMessage->parse(obj.m_parsectx, input);
	auto hr = obj.createIMAPBody(input, vmMessage, lpMessage);
	if (hr != hrSuccess || !envelope)
		return hr;
	return obj.createIMAPEnvelope(vmMessage, lpMessage);
}

} /* namespace */
