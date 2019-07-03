/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef VMIMETOMAPI
#define VMIMETOMAPI

#include <list>
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
	BODYLEVEL bodyLevel = BODY_NONE; /* Is there a body, and is so, what type. */
	ATTACHLEVEL attachLevel = ATTACH_NONE; /* Current attachment state */
	unsigned int ulLastCP = 0; /* Character set of the body */
	unsigned int mime_vtag_nest = 0; /* Number of nested "MIME-Version" headers seen */
	unsigned int ulMsgInMsg = 0; /* Counter for msg-in-msg level */
	bool bAttachSignature = false; /* Add a signed signature at the end */
	/* Cache for the current complete untouched HTML body, used for finding CIDs or locations (inline images) */
	std::string strHTMLBody;
	std::vector<unsigned int> part_counter; /* stack used for counting MIME parts */
	std::vector<std::string> cvt_notes; /* conversion notices */

	std::string part_text() const;
};

void ignoreError(void *ctx, const char *msg, ...);

class VMIMEToMAPI final {
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
	HRESULT dissect_body(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>, IMessage *lpMessage, unsigned int flags = 0);
	void dissect_message(vmime::shared_ptr<vmime::body>, IMessage *);
	HRESULT dissect_multipart(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>, IMessage *, unsigned int flags = 0);
	HRESULT dissect_ical(vmime::shared_ptr<vmime::header>, vmime::shared_ptr<vmime::body>, IMessage *, bool bIsAttachment);
	std::string generate_wrap(vmime::shared_ptr<vmime::headerFieldValue> &&);
	HRESULT hreplyto(vmime::shared_ptr<vmime::mailboxList> &&, std::wstring &, memory_ptr<FLATENTRYLIST> &);
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
	HRESULT save_raw_smime(const std::string &input, size_t hdr_end, const vmime::shared_ptr<vmime::header> &in, IMessage *out);
};

} /* namespace */

#endif
