/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef UTIL_H
#define UTIL_H

#include <memory>
#include <kopano/zcdefs.h>
#include <mapix.h>
#include <edkmdb.h>
#include <string>
#include <kopano/ECDefs.h>
#include <kopano/memory.hpp>
#include <kopano/ustringutil.h>

namespace KC {

namespace Util {

extern KC_EXPORT HRESULT HrAddToPropertyArray(const SPropValue *src, unsigned int srcvals, const SPropValue *add, SPropValue **dest, unsigned int *ndestvals);
extern KC_EXPORT HRESULT HrMergePropertyArrays(const SPropValue *src, unsigned int srcvals, const SPropValue *adds, unsigned int naddvals, SPropValue **dest, unsigned int *destvals);
extern KC_EXPORT HRESULT HrCopyPropertyArray(const SPropValue *src, unsigned int srcvals, SPropValue **dest, unsigned int *destvals, bool excl_errors = false);
extern HRESULT HrCopyPropertyArrayByRef(const SPropValue *src, unsigned int srcvals, SPropValue **dest, unsigned int *destvals, bool excl_errors = false);
extern HRESULT HrCopyPropertyArray(const SPropValue *src, unsigned int srcvals, SPropValue *dest, void *base);
extern HRESULT HrCopyPropertyArrayByRef(const SPropValue *src, unsigned int srcvals, SPropValue *dest);
extern KC_EXPORT HRESULT HrCopyProperty(SPropValue *dest, const SPropValue *src, void *base, ALLOCATEMORE * = nullptr);
extern HRESULT HrCopyPropertyByRef(SPropValue *dest, const SPropValue *src);
extern KC_EXPORT HRESULT HrCopySRestriction(SRestriction *dst, const SRestriction *src, void *base);
extern KC_EXPORT HRESULT  HrCopySRestriction(SRestriction **dst, const SRestriction *src);
extern HRESULT HrCopySRowSet(SRowSet *dest, const SRowSet *src, void *base);
extern KC_EXPORT HRESULT HrCopySRow(SRow *dest, const SRow *src, void *base);
extern KC_EXPORT HRESULT HrCopyPropTagArray(const SPropTagArray *src, SPropTagArray **dst);
extern KC_EXPORT void proptag_change_unicode(unsigned int flags, SPropTagArray &);
extern KC_EXPORT HRESULT HrCopyBinary(unsigned int size, const BYTE *src, unsigned int *destsize, BYTE **dest, void *base = nullptr);
extern KC_EXPORT HRESULT HrCopyEntryId(unsigned int size, const ENTRYID *src, unsigned int *destsize, ENTRYID **dest, void *base = nullptr);
extern KC_EXPORT int CompareSBinary(const SBinary &, const SBinary &);
extern KC_EXPORT HRESULT CompareProp(const SPropValue *, const SPropValue *, const ECLocale &, int *res);
extern unsigned int PropSize(const SPropValue *);
extern KC_EXPORT int FindPropInArray(const SPropTagArray *proptags, unsigned int tag);
extern KC_EXPORT HRESULT HrStreamToString(IStream *in, std::string &out);
extern KC_EXPORT HRESULT HrStreamToString(IStream *in, std::wstring &out);
extern KC_EXPORT HRESULT HrTextToRtf(IStream *text, IStream *rtf);
extern KC_EXPORT HRESULT HrTextToHtml(IStream *text, IStream *html, unsigned int codepage);
extern KC_EXPORT HRESULT HrTextToHtml(const wchar_t *text, std::string &html, unsigned int codepage);
extern KC_EXPORT HRESULT HrHtmlToText(IStream *html, IStream *text, unsigned int codepage);
extern KC_EXPORT HRESULT HrHtmlToRtf(IStream *html, IStream *rtf, unsigned int codepage);
extern HRESULT HrHtmlToRtf(const wchar_t *whtml, std::string &rtf);
extern unsigned int GetBestBody(const SPropValue *body, const SPropValue *html, const SPropValue *rtfcomp, const SPropValue *rtfinsync, unsigned int flags);
extern KC_EXPORT unsigned int GetBestBody(IMAPIProp *obj, unsigned int flags);
extern KC_EXPORT unsigned int GetBestBody(SPropValue *props, unsigned int nvals, unsigned int flags);
extern KC_EXPORT bool IsBodyProp(unsigned int tag);
extern KC_EXPORT HRESULT HrMAPIErrorToText(HRESULT, TCHAR **err, void *base = nullptr);
extern KC_EXPORT bool ValidatePropTagArray(const SPropTagArray *);
extern KC_EXPORT HRESULT hex2bin(const char *input, size_t len, unsigned int *outlen, BYTE **output, void *parent = nullptr);
extern HRESULT hex2bin(const char *input, size_t len, BYTE *output);
extern KC_EXPORT HRESULT CopyAttachments(IMessage *src, IMessage *dst, SRestriction *r);
extern KC_EXPORT HRESULT DoCopyTo(const IID *src_intf, void *src_obj, unsigned int ciidExclude, const IID *rgiidExclude, const SPropTagArray *exclprop, unsigned int ui_param, IMAPIProgress *, const IID *dst_intf, void *dst_obj, unsigned int flags, SPropProblemArray **);
extern KC_EXPORT HRESULT DoCopyProps(const IID *src_intf, void *src_obj, const SPropTagArray *inclprop, unsigned int ui_param, IMAPIProgress *, const IID *dst_intf, void *dst_obj, unsigned int flags, SPropProblemArray **);
extern KC_EXPORT HRESULT HrCopyIMAPData(IMessage *src, IMessage *dst);
extern KC_EXPORT HRESULT HrDeleteIMAPData(IMessage *);
extern KC_EXPORT HRESULT HrGetQuotaStatus(IMsgStore *, ECQUOTA *, ECQUOTASTATUS **ret);
extern KC_EXPORT HRESULT HrDeleteAttachments(IMessage *);
extern KC_EXPORT HRESULT HrDeleteMessage(IMAPISession *, IMessage *);
extern KC_EXPORT HRESULT ReadProperty(IMAPIProp *, unsigned int tag, std::string &data);
extern KC_EXPORT HRESULT WriteProperty(IMAPIProp *, unsigned int tag, const std::string &data);
extern KC_EXPORT HRESULT ExtractSuggestedContactsEntryID(SPropValue *prop_blob, unsigned int *eid_size, ENTRYID **eid);
extern HRESULT ExtractAdditionalRenEntryID(SPropValue *prop_blob, unsigned short block_type, unsigned int *eid_size, ENTRYID **eid);

} /* namespace Util */

#define RTF_TAG_TYPE_TEXT	0x0000
#define RTF_TAG_TYPE_HTML	0x0010
#define RTF_TAG_TYPE_HEAD	0x0020
#define RTF_TAG_TYPE_BODY	0x0030
#define RTF_TAG_TYPE_P		0x0040
#define RTF_TAG_TYPE_STARTP	0x0050
#define RTF_TAG_TYPE_ENDP	0x0060
#define RTF_TAG_TYPE_BR		0x0070
#define RTF_TAG_TYPE_PRE	0x0080
#define RTF_TAG_TYPE_FONT	0x0090
#define RTF_TAG_TYPE_HEADER	0x00A0
#define RTF_TAG_TYPE_TITLE	0x00B0
#define RTF_TAG_TYPE_PLAIN	0x00C0
#define RTF_TAG_TYPE_UNK	0x00F0

#define RTF_INBODY			0x0000
#define RTF_INHEAD			0x0001
#define RTF_INHTML			0x0002
#define RTF_OUTHTML			0x0003

#define RTF_FLAG_INPAR		0x0004
#define RTF_FLAG_CLOSE		0x0008
#define RTF_FLAG_MHTML		0x0100

template<typename T> class alloc_wrap {
	private:
	object_ptr<T> obj;
	public:
	template<typename... ArgTp> alloc_wrap(ArgTp &&... args) :
	    obj(new(std::nothrow) T(std::forward<ArgTp>(args)...))
	{}
	template<typename U> HRESULT put(U **p)
	{
		if (obj == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		*p = obj.get();
		(*p)->AddRef(); /* what QueryInterface would have done */
		return hrSuccess;
	}
	template<typename Base> HRESULT as(const IID &iid, Base **p)
	{
		if (obj == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		return obj->QueryInterface(iid, reinterpret_cast<void **>(p));
	}
};

#define ALLOC_WRAP_FRIEND template<typename T> friend class ::KC::alloc_wrap

extern KC_EXPORT HRESULT qi_void_to_imapiprop(void *, const IID &, IMAPIProp **);

} /* namespace */

#endif // UTIL_H
