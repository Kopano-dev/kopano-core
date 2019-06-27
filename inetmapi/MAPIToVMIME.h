/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef MAPITOVMIME
#define MAPITOVMIME

#include <memory>
#include <mapix.h>
#include <string>
#include <vmime/vmime.hpp>
#include <vmime/mailbox.hpp>
#include <inetmapi/options.h>
#include <mapidefs.h>
#include <kopano/charset/convert.h>
#include <kopano/memory.hpp>
#include "SMIMEMessage.h"

namespace KC {

/**
 * %MTV_SPOOL:	add X-Mailer headers on message
 */
enum {
	MTV_NONE = 0,
	MTV_SPOOL = 1 << 0,
	MTV_SKIP_CONTENT = 1 << 1,
};

class MAPIToVMIME final {
public:
	MAPIToVMIME();
	MAPIToVMIME(IMAPISession *, IAddrBook *, sending_options);
	HRESULT convertMAPIToVMIME(IMessage *in, vmime::shared_ptr<vmime::message> *out, unsigned int = MTV_NONE);
	std::wstring getConversionError(void) const;

private:
	vmime::parsingContext m_parsectx;
	sending_options sopt;
	object_ptr<IAddrBook> m_lpAdrBook;
	object_ptr<IMAPISession> m_lpSession;
	std::wstring m_strError;
	convert_context m_converter;
	vmime::charset m_vmCharset;		//!< charset to use in email
	std::string m_strCharset;		//!< charset to use in email + //TRANSLIT tag
	std::string m_strHTMLCharset;	//!< HTML body charset in MAPI message (input)

	enum eBestBody { plaintext, html, realRTF };
	
	HRESULT fillVMIMEMail(IMessage *lpMessage, bool bSkipContent, vmime::messageBuilder* lpVMMessageBuilder);

	HRESULT handleTextparts(IMessage* lpMessage, vmime::messageBuilder* lpVMMessageBuilder, eBestBody *bestBody);
	HRESULT getMailBox(SRow *lpRow, vmime::shared_ptr<vmime::address> &mbox);
	HRESULT processRecipients(IMessage* lpMessage, vmime::messageBuilder* lpVMMessageBuilder);
	HRESULT handleExtraHeaders(IMessage *in, vmime::shared_ptr<vmime::header> out, unsigned int);
	HRESULT handleReplyTo(IMessage *in, vmime::shared_ptr<vmime::header> hdr);
	HRESULT handleContactEntryID(ULONG cValues, LPSPropValue lpProps, std::wstring &strName, std::wstring &strType, std::wstring &strEmail);
	HRESULT handleSenderInfo(IMessage* lpMessage, vmime::shared_ptr<vmime::header>);

	HRESULT handleAttachments(IMessage* lpMessage, vmime::messageBuilder* lpVMMessageBuilder);
	HRESULT handleSingleAttachment(IMessage* lpMessage, LPSRow lpRow, vmime::messageBuilder* lpVMMessageBuilder);
	HRESULT parseMimeTypeFromFilename(std::wstring strFilename, vmime::mediaType *lpMT, bool *lpbSendBinary);
	HRESULT setBoundaries(vmime::shared_ptr<vmime::header> hdr, vmime::shared_ptr<vmime::body> body, const std::string &boundary);
	HRESULT handleXHeaders(IMessage *in, vmime::shared_ptr<vmime::header> out, unsigned int);
	HRESULT handleTNEF(IMessage* lpMessage, vmime::messageBuilder* lpVMMessageBuilder, eBestBody bestBody);

	// build Messages
	HRESULT BuildNoteMessage(IMessage *in, vmime::shared_ptr<vmime::message> *out, unsigned int = MTV_NONE);
	HRESULT BuildMDNMessage(IMessage *in, vmime::shared_ptr<vmime::message> *out);

	// util
	void capitalize(char *s);
	void removeEnters(wchar_t *);
	vmime::text getVmimeTextFromWide(const wchar_t *);
	vmime::text getVmimeTextFromWide(const std::wstring &);
	bool is_voting_request(IMessage *lpMessage) const;
	bool has_reminder(IMessage *) const ;
};

} /* namespace */

#endif
