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

#ifndef VMIMETOMAPI
#define VMIMETOMAPI

#include <list>
#include <kopano/zcdefs.h>
#include <vmime/vmime.hpp>
#include <mapix.h>
#include <mapidefs.h>
#include <inetmapi/options.h>
#include <kopano/charset/convert.h>
#include <kopano/memory.hpp>

namespace KC {

#define MAPI_CHARSET vmime::charset(vmime::charsets::UTF_8)
#define MAPI_CHARSET_STRING "UTF-8"

enum BODYLEVEL { BODY_NONE, BODY_PLAIN, BODY_HTML };
enum ATTACHLEVEL { ATTACH_NONE, ATTACH_INLINE, ATTACH_NORMAL };

struct sMailState {
	BODYLEVEL bodyLevel;		//!< the current body state. plain upgrades none, html upgrades plain and none.
	ULONG ulLastCP;
	ATTACHLEVEL attachLevel;	//!< the current attachment state
	unsigned int mime_vtag_nest;	//!< number of nested MIME-Version tags seen
	bool bAttachSignature;		//!< add a signed signature at the end
	ULONG ulMsgInMsg;			//!< counter for msg-in-msg level
	std::string strHTMLBody;	//!< cache for the current complete untouched HTML body, used for finding CIDs or locations (inline images)

	sMailState() {
		reset();
		ulMsgInMsg = 0;
	};
	void reset() {
		bodyLevel = BODY_NONE;
		ulLastCP = 0;
		attachLevel = ATTACH_NONE;
		mime_vtag_nest = 0;
		bAttachSignature = false;
		strHTMLBody.clear();
	};
};

void ignoreError(void *ctx, const char *msg, ...);

class VMIMEToMAPI _kc_final {
public:
	VMIMEToMAPI();
	VMIMEToMAPI(LPADRBOOK lpAdrBook, delivery_options dopt);

	HRESULT convertVMIMEToMAPI(const std::string &input, IMessage *lpMessage);
	HRESULT createIMAPProperties(const std::string &input, std::string *envelope, std::string *body, std::string *bodystruct);
	HRESULT createIMAPBody(const std::string &input, vmime::shared_ptr<vmime::message>, IMessage *);
	HRESULT createIMAPEnvelope(vmime::shared_ptr<vmime::message>, IMessage *);

private:
	vmime::generationContext m_genctx;
	delivery_options m_dopt;
	IAddrBook *m_lpAdrBook = nullptr;
	object_ptr<IABContainer> m_lpDefaultDir;
	sMailState m_mailState;
	convert_context m_converter;

	HRESULT fillMAPIMail(vmime::shared_ptr<vmime::message>, IMessage *lpMessage);
	HRESULT dissect_body(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>, IMessage *lpMessage, bool filterDouble = false, bool appendBody = false);
	void dissect_message(vmime::shared_ptr<vmime::body>, IMessage *);
	HRESULT dissect_multipart(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>, IMessage *, bool filterDouble = false, bool appendBody = false);
	HRESULT dissect_ical(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>, IMessage *, bool bIsAttachment);
	std::string generate_wrap(vmime::shared_ptr<vmime::headerFieldValue> &&);
	HRESULT handleHeaders(vmime::shared_ptr<vmime::header>, IMessage* lpMessage);
	HRESULT handleRecipients(vmime::shared_ptr<vmime::header>, IMessage* lpMessage);
	HRESULT modifyRecipientList(LPADRLIST lpRecipients, vmime::shared_ptr<vmime::addressList>, ULONG ulRecipType);
	HRESULT modifyFromAddressBook(LPSPropValue *lppPropVals, ULONG *lpulValues, const char *email, const wchar_t *fullname, ULONG ulRecipType, const SPropTagArray *lpPropsList);

	static std::string content_transfer_decode(vmime::shared_ptr<vmime::body>);
	static vmime::charset get_mime_encoding(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>);
	int renovate_encoding(std::string &, const std::vector<std::string> &);
	static int renovate_encoding(std::wstring &, std::string &, const std::vector<std::string> &);

	HRESULT handleTextpart(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>, IMessage* lpMessage, bool bAppendBody);
	HRESULT handleHTMLTextpart(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>, IMessage* lpMessage, bool bAppendBody);
	HRESULT handleAttachment(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>, IMessage *lpMessage, const wchar_t *sugg_filename = nullptr, bool bAllowEmpty = true);
	HRESULT handleMessageToMeProps(IMessage *lpMessage, LPADRLIST lpRecipients);
	static int getCharsetFromHTML(const std::string &html, vmime::charset *charset);
	static vmime::charset getCompatibleCharset(const vmime::charset &);
	std::wstring getWideFromVmimeText(const vmime::text &vmText);
	static HRESULT postWriteFixups(IMessage *lpMessage);
	static std::string mailboxToEnvelope(vmime::shared_ptr<vmime::mailbox>);
	static std::string addressListToEnvelope(vmime::shared_ptr<vmime::addressList> mbox);
	std::string createIMAPEnvelope(vmime::shared_ptr<vmime::message>);
	HRESULT messagePartToStructure(const std::string &input, vmime::shared_ptr<vmime::bodyPart>, std::string *simple, std::string *extended);
	HRESULT bodyPartToStructure(const std::string &input, vmime::shared_ptr<vmime::bodyPart>, std::string *simple, std::string *extended);
	std::string getStructureExtendedFields(vmime::shared_ptr<vmime::header> part);
	static std::string parameterizedFieldToStructure(vmime::shared_ptr<vmime::parameterizedHeaderField>);
	static std::string::size_type countBodyLines(const std::string &input, std::string::size_type start, std::string::size_type length);
	static bool filter_html(IMessage *, IStream *, ULONG, const std::string &);
};

} /* namespace */

#endif
