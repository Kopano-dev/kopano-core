/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef INETMAPI_H
#define INETMAPI_H

/* WARNING */
/* mapidefs.h may not be included _before_ any vmime include! */

#include <kopano/zcdefs.h>
#include <mapix.h>
#include <mapidefs.h>
#include <vector>
#include <inetmapi/options.h>

namespace KC {

struct sFailedRecip {
	std::string strRecipEmail, strSMTPResponse;
	std::wstring strRecipName;
	unsigned int ulSMTPcode;
};

// Sender Base class
// implementation of smtp sender in ECVMIMEUtils as ECVMIMESender
class _kc_export ECSender {
protected:
	std::string smtphost;
	std::wstring error;
	int smtpport, smtpresult = 0;
	std::vector<sFailedRecip> mTemporaryFailedRecipients, mPermanentFailedRecipients;

public:
	_kc_hidden ECSender(const std::string &smtphost, int port);
	_kc_hidden virtual ~ECSender(void) = default;
	_kc_hidden virtual int getSMTPResult() const { return smtpresult; }
	_kc_hidden virtual const wchar_t *getErrorString() const { return error.c_str(); }
	_kc_hidden virtual void setError(const std::wstring &e) { error = e; }
	_kc_hidden virtual void setError(const std::string &);
	_kc_hidden virtual bool haveError() const { return !error.empty(); }
	_kc_hidden virtual const std::vector<sFailedRecip> &getPermanentFailedRecipients(void) const { return mPermanentFailedRecipients; }
	_kc_hidden virtual const std::vector<sFailedRecip> &getTemporaryFailedRecipients(void) const { return mTemporaryFailedRecipients; }
};

/* c wrapper to create object */
extern _kc_export ECSender *CreateSender(const std::string &smtphost, int port);

// Read char Buffer and set properties on open lpMessage object
extern _kc_export HRESULT IMToMAPI(IMAPISession *, IMsgStore *, IAddrBook *, IMessage *, const std::string &input, delivery_options dopt);

// Read properties from lpMessage object and fill a buffer with internet rfc822 format message
// Use this one for retrieving messages not in outgoing que, they already have PR_SENDER_EMAIL/NAME
// This can be used in making pop3 / imap server

// Read properties from lpMessage object and fill buffer with internet rfc822 format message
extern _kc_export HRESULT IMToINet(IMAPISession *, IAddrBook *, IMessage *, char **lppbuf, sending_options);

// Read properties from lpMessage object and output to stream with internet rfc822 format message
extern _kc_export HRESULT IMToINet(IMAPISession *, IAddrBook *, IMessage *, std::ostream &, sending_options);

// Read properties from lpMessage object and send using  lpSMTPHost
extern _kc_export HRESULT IMToINet(IMAPISession *, IAddrBook *, IMessage *, ECSender *mailer, sending_options);

// Parse the RFC822 input and create IMAP Envelope, Body and Bodystructure property values
extern _kc_export HRESULT createIMAPProperties(const std::string &input, std::string *envelope, std::string *body, std::string *bodystruct);
extern _kc_export HRESULT createIMAPBody(const std::string &input, IMessage *lpMessage, bool envelope = false);

} /* namespace */

#endif // INETMAPI_H
