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

extern _kc_export HRESULT HrAddToPropertyArray(const SPropValue *src, unsigned int srcvals, const SPropValue *add, SPropValue **dest, unsigned int *ndestvals);
extern _kc_export HRESULT HrMergePropertyArrays(const SPropValue *src, unsigned int srcvals, const SPropValue *adds, unsigned int naddvals, SPropValue **dest, unsigned int *destvals);
extern _kc_export HRESULT HrCopyPropertyArray(const SPropValue *src, unsigned int srcvals, SPropValue **dest, unsigned int *destvals, bool excl_errors = false);
extern HRESULT HrCopyPropertyArrayByRef(const SPropValue *src, unsigned int srcvals, SPropValue **dest, unsigned int *destvals, bool excl_errors = false);
extern HRESULT HrCopyPropertyArray(const SPropValue *src, unsigned int srcvals, SPropValue *dest, void *base);
extern HRESULT HrCopyPropertyArrayByRef(const SPropValue *src, unsigned int srcvals, SPropValue *dest);
extern _kc_export HRESULT HrCopyProperty(SPropValue *dest, const SPropValue *src, void *base, ALLOCATEMORE * = nullptr);
extern HRESULT HrCopyPropertyByRef(SPropValue *dest, const SPropValue *src);
extern _kc_export HRESULT HrCopySRestriction(SRestriction *dst, const SRestriction *src, void *base);
extern _kc_export HRESULT  HrCopySRestriction(SRestriction **dst, const SRestriction *src);
extern HRESULT HrCopySRowSet(SRowSet *dest, const SRowSet *src, void *base);
extern _kc_export HRESULT HrCopySRow(SRow *dest, const SRow *src, void *base);
extern _kc_export HRESULT HrCopyPropTagArray(const SPropTagArray *src, SPropTagArray **dst);
extern _kc_export void proptag_change_unicode(unsigned int flags, SPropTagArray &);
extern _kc_export HRESULT HrCopyBinary(unsigned int size, const BYTE *src, unsigned int *destsize, BYTE **dest, void *base = nullptr);
extern _kc_export HRESULT HrCopyEntryId(unsigned int size, const ENTRYID *src, unsigned int *destsize, ENTRYID **dest, void *base = nullptr);
extern _kc_export int CompareSBinary(const SBinary &, const SBinary &);
extern _kc_export HRESULT CompareProp(const SPropValue *, const SPropValue *, const ECLocale &, int *res);
extern unsigned int PropSize(const SPropValue *);
extern _kc_export int FindPropInArray(const SPropTagArray *proptags, unsigned int tag);
extern _kc_export HRESULT HrStreamToString(IStream *in, std::string &out);
extern _kc_export HRESULT HrStreamToString(IStream *in, std::wstring &out);
extern _kc_export HRESULT HrTextToRtf(IStream *text, IStream *rtf);
extern _kc_export HRESULT HrTextToHtml(IStream *text, IStream *html, unsigned int codepage);
extern _kc_export HRESULT HrTextToHtml(const wchar_t *text, std::string &html, unsigned int codepage);
extern _kc_export HRESULT HrHtmlToText(IStream *html, IStream *text, unsigned int codepage);
extern _kc_export HRESULT HrHtmlToRtf(IStream *html, IStream *rtf, unsigned int codepage);
extern HRESULT HrHtmlToRtf(const wchar_t *whtml, std::string &rtf);
extern unsigned int GetBestBody(const SPropValue *body, const SPropValue *html, const SPropValue *rtfcomp, const SPropValue *rtfinsync, unsigned int flags);
extern _kc_export unsigned int GetBestBody(IMAPIProp *obj, unsigned int flags);
extern _kc_export unsigned int GetBestBody(SPropValue *props, unsigned int nvals, unsigned int flags);
extern _kc_export bool IsBodyProp(unsigned int tag);
extern _kc_export HRESULT HrMAPIErrorToText(HRESULT, TCHAR **err, void *base = nullptr);
extern _kc_export bool ValidatePropTagArray(const SPropTagArray *);
extern _kc_export HRESULT hex2bin(const char *input, size_t len, unsigned int *outlen, BYTE **output, void *parent = nullptr);
extern HRESULT hex2bin(const char *input, size_t len, BYTE *output);
extern _kc_export HRESULT CopyAttachments(IMessage *src, IMessage *dst, SRestriction *r);
extern _kc_export HRESULT DoCopyTo(const IID *src_intf, void *src_obj, unsigned int ciidExclude, const IID *rgiidExclude, const SPropTagArray *exclprop, unsigned int ui_param, IMAPIProgress *, const IID *dst_intf, void *dst_obj, unsigned int flags, SPropProblemArray **);
extern _kc_export HRESULT DoCopyProps(const IID *src_intf, void *src_obj, const SPropTagArray *inclprop, unsigned int ui_param, IMAPIProgress *, const IID *dst_intf, void *dst_obj, unsigned int flags, SPropProblemArray **);
extern _kc_export HRESULT HrCopyIMAPData(IMessage *src, IMessage *dst);
extern _kc_export HRESULT HrDeleteIMAPData(IMessage *);
extern _kc_export HRESULT HrGetQuotaStatus(IMsgStore *, ECQUOTA *, ECQUOTASTATUS **ret);
extern _kc_export HRESULT HrDeleteAttachments(IMessage *);
extern _kc_export HRESULT HrDeleteMessage(IMAPISession *, IMessage *);
extern _kc_export HRESULT ReadProperty(IMAPIProp *, unsigned int tag, std::string &data);
extern _kc_export HRESULT WriteProperty(IMAPIProp *, unsigned int tag, const std::string &data);
extern _kc_export HRESULT ExtractSuggestedContactsEntryID(SPropValue *prop_blob, unsigned int *eid_size, ENTRYID **eid);
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

extern _kc_export HRESULT qi_void_to_imapiprop(void *, const IID &, IMAPIProp **);

} /* namespace */

#endif // UTIL_H
