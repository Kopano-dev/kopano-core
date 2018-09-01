/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
	VMIMEToMAPI(IAddrBook *, delivery_options &&);

	HRESULT convertVMIMEToMAPI(const std::string &input, IMessage *lpMessage);
	HRESULT createIMAPProperties(const std::string &input, std::string *envelope, std::string *body, std::string *bodystruct);
	HRESULT createIMAPBody(const std::string &input, vmime::shared_ptr<vmime::message>, IMessage *);
	HRESULT createIMAPEnvelope(vmime::shared_ptr<vmime::message>, IMessage *);

	public:
	vmime::parsingContext m_parsectx;
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
	int renovate_encoding(std::string &, const std::vector<std::string> &);

	HRESULT handleTextpart(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>, IMessage* lpMessage, bool bAppendBody);
	HRESULT handleHTMLTextpart(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>, IMessage* lpMessage, bool bAppendBody);
	HRESULT handleAttachment(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>, IMessage *lpMessage, const wchar_t *sugg_filename = nullptr, bool bAllowEmpty = true);
	HRESULT handleMessageToMeProps(IMessage *lpMessage, LPADRLIST lpRecipients);
	std::wstring getWideFromVmimeText(const vmime::text &vmText);
	std::string createIMAPEnvelope(vmime::shared_ptr<vmime::message>);
	HRESULT messagePartToStructure(const std::string &input, vmime::shared_ptr<vmime::bodyPart>, std::string *simple, std::string *extended);
	HRESULT bodyPartToStructure(const std::string &input, vmime::shared_ptr<vmime::bodyPart>, std::string *simple, std::string *extended);
	std::string getStructureExtendedFields(vmime::shared_ptr<vmime::header> part);
};

} /* namespace */

#endif
