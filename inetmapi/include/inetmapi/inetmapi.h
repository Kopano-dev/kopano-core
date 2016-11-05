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

#ifndef INETMAPI_H
#define INETMAPI_H

/* WARNING */
/* mapidefs.h may not be included _before_ any vmime include! */

#include <mapix.h>
#include <mapidefs.h>
#include <vector>
#include <inetmapi/options.h>

# define INETMAPI_API

typedef struct _sFailedRecip {
	std::string strRecipEmail;
	std::wstring strRecipName;
	unsigned int ulSMTPcode;
	std::string strSMTPResponse;
} sFailedRecip;

// Sender Base class
// implementation of smtp sender in ECVMIMEUtils as ECVMIMESender
class ECSender {
protected:
	std::string smtphost;
	int smtpport;
	std::wstring error;
	int smtpresult;

	std::vector<sFailedRecip> mTemporaryFailedRecipients;
	std::vector<sFailedRecip> mPermanentFailedRecipients;

public:
	ECSender(const std::string &strSMTPHost, int port);
	virtual	~ECSender(void) {}
	virtual int getSMTPResult();
	virtual const WCHAR* getErrorString();
	virtual void setError(const std::wstring &newError);
	virtual void setError(const std::string &newError);
	virtual bool haveError();

	virtual const std::vector<sFailedRecip> &getPermanentFailedRecipients(void) const { return mPermanentFailedRecipients; }
	virtual const std::vector<sFailedRecip> &getTemporaryFailedRecipients(void) const { return mTemporaryFailedRecipients; }
};

extern "C" {

bool ValidateCharset(const char *charset);

/* c wrapper to create object */
extern INETMAPI_API ECSender *CreateSender(const std::string &smtp, int port);

// Read char Buffer and set properties on open lpMessage object
extern INETMAPI_API HRESULT IMToMAPI(IMAPISession *, IMsgStore *, IAddrBook *, IMessage *, const std::string &input, delivery_options dopt);

// Read properties from lpMessage object and fill a buffer with internet rfc822 format message
// Use this one for retrieving messages not in outgoing que, they already have PR_SENDER_EMAIL/NAME
// This can be used in making pop3 / imap server

} /* extern "C" */

// Read properties from lpMessage object and fill buffer with internet rfc822 format message
extern INETMAPI_API HRESULT IMToINet(IMAPISession *, IAddrBook *, IMessage *, char **lppbuf, sending_options);

// Read properties from lpMessage object and output to stream with internet rfc822 format message
extern INETMAPI_API HRESULT IMToINet(IMAPISession *, IAddrBook *, IMessage *, std::ostream &, sending_options);

// Read properties from lpMessage object and send using  lpSMTPHost
extern INETMAPI_API HRESULT IMToINet(IMAPISession *, IAddrBook *, IMessage *, ECSender *mailer, sending_options);

// Parse the RFC822 input and create IMAP Envelope, Body and Bodystructure property values
INETMAPI_API HRESULT createIMAPProperties(const std::string &input, std::string *lpEnvelope, std::string *lpBody, std::string *lpBodyStructure);


#endif // INETMAPI_H
