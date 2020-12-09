/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <string>
#include <vector>
#include <kopano/platform.h>
#include <sys/un.h>
#include "WSUtil.h"
#include <kopano/ECGuid.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include <kopano/mapiext.h>
#include "ECMAPIProp.h" /* static row getprop functions */
#include "ECMAPIFolder.h"
#include "ECMessage.h"
#include "ECMailUser.h"
#include "ECABContainer.h"
#include "SOAPUtils.h"
#include <kopano/CommonUtil.h>
#include <kopano/charset/convert.h>
#include <kopano/charset/utf8string.h>
#include "EntryPoint.h"
#include <kopano/ECGetText.h>
#include "SOAPSock.h"

using namespace KC;

#define CONVERT_TO(ctx, cset, ...) ((ctx) ? (ctx)->convert_to<cset>(__VA_ARGS__) : convert_to<cset>(__VA_ARGS__))

HRESULT CopyMAPIPropValToSOAPPropVal(propVal *dp, const SPropValue *sp,
    convert_context *lpConverter)
{
	dp->ulPropTag = sp->ulPropTag;
	dp->Value = propValData();

	switch(PROP_TYPE(sp->ulPropTag)) {
	case PT_I2:
		dp->__union = SOAP_UNION_propValData_i;
		dp->Value.i = sp->Value.i;
		break;
	case PT_LONG: // or PT_ULONG
		dp->__union = SOAP_UNION_propValData_ul;
		dp->Value.ul = sp->Value.ul;
		break;
	case PT_R4:
		dp->__union = SOAP_UNION_propValData_flt;
		dp->Value.flt = sp->Value.flt;
		break;
	case PT_DOUBLE:
		dp->__union = SOAP_UNION_propValData_dbl;
		dp->Value.dbl = sp->Value.dbl;
		break;
	case PT_CURRENCY:
		dp->__union = SOAP_UNION_propValData_hilo;
		dp->Value.hilo = soap_new_hiloLong(nullptr);
		dp->Value.hilo->hi = sp->Value.cur.Hi;
		dp->Value.hilo->lo = sp->Value.cur.Lo;
		break;
	case PT_APPTIME:
		dp->__union = SOAP_UNION_propValData_dbl;
		dp->Value.dbl = sp->Value.at;
		break;
	case PT_ERROR:
		dp->__union = SOAP_UNION_propValData_ul;
		dp->Value.ul = sp->Value.err;
		break;
	case PT_BOOLEAN:
		dp->__union = SOAP_UNION_propValData_b;
		dp->Value.b = sp->Value.b;
		break;
	case PT_OBJECT:
		// can never be transmitted over the wire!
		return MAPI_E_INVALID_TYPE;
	case PT_I8:
		dp->__union = SOAP_UNION_propValData_li;
		dp->Value.li = sp->Value.li.QuadPart;
		break;
	case PT_STRING8:
		dp->__union = SOAP_UNION_propValData_lpszA;
		dp->Value.lpszA = soap_strdup(nullptr, CONVERT_TO(lpConverter, utf8string, sp->Value.lpszA).z_str());
		break;
	case PT_UNICODE:
		dp->__union = SOAP_UNION_propValData_lpszA;
		dp->Value.lpszA = soap_strdup(nullptr, CONVERT_TO(lpConverter, utf8string, sp->Value.lpszW).z_str());
		break;
	case PT_SYSTIME:
		dp->__union = SOAP_UNION_propValData_hilo;
		dp->Value.hilo = soap_new_hiloLong(nullptr);
		dp->Value.hilo->hi = sp->Value.ft.dwHighDateTime;
		dp->Value.hilo->lo = sp->Value.ft.dwLowDateTime;
		break;
	case PT_CLSID:
		dp->__union = SOAP_UNION_propValData_bin;
		dp->Value.bin = soap_new_xsd__base64Binary(nullptr);
		dp->Value.bin->__ptr  = soap_new_unsignedByte(nullptr, sizeof(GUID));
		dp->Value.bin->__size = sizeof(GUID);
		memcpy(dp->Value.bin->__ptr, sp->Value.lpguid, sizeof(GUID));
		break;
	case PT_BINARY:
		dp->__union = SOAP_UNION_propValData_bin;
		dp->Value.bin = soap_new_xsd__base64Binary(nullptr);
		if (sp->Value.bin.cb == 0 || sp->Value.bin.lpb == nullptr) {
			dp->Value.bin->__ptr = nullptr;
			dp->Value.bin->__size = 0;
			break;
		}
		dp->Value.bin->__ptr  = soap_new_unsignedByte(nullptr, sp->Value.bin.cb);
		dp->Value.bin->__size = sp->Value.bin.cb;
		memcpy(dp->Value.bin->__ptr, sp->Value.bin.lpb, sp->Value.bin.cb);
		break;
	case PT_MV_I2:
		dp->__union = SOAP_UNION_propValData_mvi;
		dp->Value.mvi.__size = sp->Value.MVi.cValues;
		dp->Value.mvi.__ptr  = soap_new_short(nullptr, dp->Value.mvi.__size);
		memcpy(dp->Value.mvi.__ptr, sp->Value.MVi.lpi, sizeof(short int) * dp->Value.mvi.__size);
		break;
	case PT_MV_LONG:
		dp->__union = SOAP_UNION_propValData_mvl;
		dp->Value.mvl.__size = sp->Value.MVl.cValues;
		dp->Value.mvl.__ptr  = soap_new_unsignedInt(nullptr, dp->Value.mvl.__size);
		memcpy(dp->Value.mvl.__ptr, sp->Value.MVl.lpl, sizeof(unsigned int) * dp->Value.mvl.__size);
		break;
	case PT_MV_R4:
		dp->__union = SOAP_UNION_propValData_mvflt;
		dp->Value.mvflt.__size = sp->Value.MVflt.cValues;
		dp->Value.mvflt.__ptr  = soap_new_float(nullptr, dp->Value.mvflt.__size);
		memcpy(dp->Value.mvflt.__ptr, sp->Value.MVflt.lpflt, sizeof(float) * dp->Value.mvflt.__size);
		break;
	case PT_MV_DOUBLE:
		dp->__union = SOAP_UNION_propValData_mvdbl;
		dp->Value.mvdbl.__size = sp->Value.MVdbl.cValues;
		dp->Value.mvdbl.__ptr  = soap_new_double(nullptr, dp->Value.mvdbl.__size);
		memcpy(dp->Value.mvdbl.__ptr, sp->Value.MVdbl.lpdbl, sizeof(double) * dp->Value.mvdbl.__size);
		break;
	case PT_MV_CURRENCY:
		dp->__union = SOAP_UNION_propValData_mvhilo;
		dp->Value.mvhilo.__size = sp->Value.MVcur.cValues;
		dp->Value.mvhilo.__ptr  = soap_new_hiloLong(nullptr, dp->Value.mvhilo.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvhilo.__size; ++i) {
			dp->Value.mvhilo.__ptr[i].hi = sp->Value.MVcur.lpcur[i].Hi;
			dp->Value.mvhilo.__ptr[i].lo = sp->Value.MVcur.lpcur[i].Lo;
		}
		break;
	case PT_MV_APPTIME:
		dp->__union = SOAP_UNION_propValData_mvdbl;
		dp->Value.mvdbl.__size = sp->Value.MVat.cValues;
		dp->Value.mvdbl.__ptr  = soap_new_double(nullptr, dp->Value.mvdbl.__size);
		memcpy(dp->Value.mvdbl.__ptr, sp->Value.MVat.lpat, sizeof(double) * dp->Value.mvdbl.__size);
		break;
	case PT_MV_SYSTIME:
		dp->__union = SOAP_UNION_propValData_mvhilo;
		dp->Value.mvhilo.__size = sp->Value.MVft.cValues;
		dp->Value.mvhilo.__ptr  = soap_new_hiloLong(nullptr, dp->Value.mvhilo.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvhilo.__size; ++i) {
			dp->Value.mvhilo.__ptr[i].hi = sp->Value.MVft.lpft[i].dwHighDateTime;
			dp->Value.mvhilo.__ptr[i].lo = sp->Value.MVft.lpft[i].dwLowDateTime;
		}
		break;
	case PT_MV_BINARY:
		dp->__union = SOAP_UNION_propValData_mvbin;
		dp->Value.mvbin.__size = sp->Value.MVbin.cValues;
		dp->Value.mvbin.__ptr  = soap_new_xsd__base64Binary(nullptr, dp->Value.mvbin.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvbin.__size; ++i) {
			if (sp->Value.MVbin.lpbin[i].cb == 0 ||
			    sp->Value.MVbin.lpbin[i].lpb == nullptr) {
				dp->Value.mvbin.__ptr[i].__size = 0;
				dp->Value.mvbin.__ptr[i].__ptr = nullptr;
				continue;
			}
			dp->Value.mvbin.__ptr[i].__size = sp->Value.MVbin.lpbin[i].cb;
			dp->Value.mvbin.__ptr[i].__ptr  = soap_new_unsignedByte(nullptr, dp->Value.mvbin.__ptr[i].__size);
			memcpy(dp->Value.mvbin.__ptr[i].__ptr, sp->Value.MVbin.lpbin[i].lpb, dp->Value.mvbin.__ptr[i].__size);
		}
		break;
	case PT_MV_STRING8:
		if (lpConverter == NULL) {
			convert_context converter;
			CopyMAPIPropValToSOAPPropVal(dp, sp, &converter);
			break;
		}
		dp->__union = SOAP_UNION_propValData_mvszA;
		dp->Value.mvszA.__size = sp->Value.MVszA.cValues;
		dp->Value.mvszA.__ptr  = soap_new_string(nullptr, dp->Value.mvszA.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvszA.__size; ++i)
			dp->Value.mvszA.__ptr[i] = soap_strdup(nullptr, lpConverter->convert_to<utf8string>(sp->Value.MVszA.lppszA[i]).z_str());
		break;
	case PT_MV_UNICODE:
		if (lpConverter == NULL) {
			convert_context converter;
			CopyMAPIPropValToSOAPPropVal(dp, sp, &converter);
			break;
		}
		dp->__union = SOAP_UNION_propValData_mvszA;
		dp->Value.mvszA.__size = sp->Value.MVszA.cValues;
		dp->Value.mvszA.__ptr  = soap_new_string(nullptr, dp->Value.mvszA.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvszA.__size; ++i)
			dp->Value.mvszA.__ptr[i] = soap_strdup(nullptr, lpConverter->convert_to<utf8string>(sp->Value.MVszW.lppszW[i]).z_str());
		break;
	case PT_MV_CLSID:
		dp->__union = SOAP_UNION_propValData_mvbin;
		dp->Value.mvbin.__size = sp->Value.MVguid.cValues;
		dp->Value.mvbin.__ptr  = soap_new_xsd__base64Binary(nullptr, dp->Value.mvbin.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvbin.__size; ++i) {
			dp->Value.mvbin.__ptr[i].__size = sizeof(GUID);
			dp->Value.mvbin.__ptr[i].__ptr  = soap_new_unsignedByte(nullptr, dp->Value.mvbin.__ptr[i].__size);
			memcpy(dp->Value.mvbin.__ptr[i].__ptr, &sp->Value.MVguid.lpguid[i], dp->Value.mvbin.__ptr[i].__size);
		}
		break;
	case PT_MV_I8:
		dp->__union = SOAP_UNION_propValData_mvli;
		dp->Value.mvli.__size = sp->Value.MVli.cValues;
		dp->Value.mvli.__ptr  = soap_new_LONG64(nullptr, dp->Value.mvli.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvli.__size; ++i)
			dp->Value.mvli.__ptr[i] = sp->Value.MVli.lpli[i].QuadPart;
		break;
	case PT_SRESTRICTION:
		dp->__union = SOAP_UNION_propValData_res;
		// NOTE: we placed the object pointer in lpszA to make sure it is on the same offset as Value.x on 32-bit and 64-bit machines
		return CopyMAPIRestrictionToSOAPRestriction(&dp->Value.res,
		       reinterpret_cast<const SRestriction *>(sp->Value.lpszA), lpConverter);
	case PT_ACTIONS: {
		// NOTE: we placed the object pointer in lpszA to make sure it is on the same offset as Value.x on 32-bit and 64-bit machines
		auto lpSrcActions = reinterpret_cast<const ACTIONS *>(sp->Value.lpszA);
		dp->__union = SOAP_UNION_propValData_actions;
		dp->Value.actions = soap_new_actions(nullptr);
		dp->Value.actions->__ptr  = soap_new_action(nullptr, lpSrcActions->cActions);
		dp->Value.actions->__size = lpSrcActions->cActions;

		for (unsigned int i = 0; i < lpSrcActions->cActions; ++i) {
			auto sa = &lpSrcActions->lpAction[i];
			auto da = &dp->Value.actions->__ptr[i];

			da->acttype = sa->acttype;
			da->flavor = sa->ulActionFlavor;
			da->flags = sa->ulFlags;

			switch(lpSrcActions->lpAction[i].acttype) {
			case OP_MOVE:
			case OP_COPY:
				da->__union = SOAP_UNION__act_moveCopy;
				da->act.moveCopy.store.__ptr = soap_new_unsignedByte(nullptr, sa->actMoveCopy.cbStoreEntryId);
				memcpy(da->act.moveCopy.store.__ptr, sa->actMoveCopy.lpStoreEntryId, sa->actMoveCopy.cbStoreEntryId);
				da->act.moveCopy.store.__size = sa->actMoveCopy.cbStoreEntryId;
				da->act.moveCopy.folder.__ptr = soap_new_unsignedByte(nullptr, sa->actMoveCopy.cbFldEntryId);
				memcpy(da->act.moveCopy.folder.__ptr, sa->actMoveCopy.lpFldEntryId, sa->actMoveCopy.cbFldEntryId);
				da->act.moveCopy.folder.__size = sa->actMoveCopy.cbFldEntryId;

				break;
			case OP_REPLY:
			case OP_OOF_REPLY:
				da->__union = SOAP_UNION__act_reply;
				da->act.reply.message.__ptr = soap_new_unsignedByte(nullptr, sa->actReply.cbEntryId);
				memcpy(da->act.reply.message.__ptr, sa->actReply.lpEntryId, sa->actReply.cbEntryId);
				da->act.reply.message.__size = sa->actReply.cbEntryId;
				da->act.reply.guid.__size = sizeof(GUID);
				da->act.reply.guid.__ptr = soap_new_unsignedByte(nullptr, sizeof(GUID));
				memcpy(da->act.reply.guid.__ptr, &sa->actReply.guidReplyTemplate, sizeof(GUID));
				break;
			case OP_DEFER_ACTION:
				da->__union = SOAP_UNION__act_defer;
				da->act.defer.bin.__ptr = soap_new_unsignedByte(nullptr, sa->actDeferAction.cbData);
				da->act.defer.bin.__size = sa->actDeferAction.cbData;
				memcpy(da->act.defer.bin.__ptr,sa->actDeferAction.pbData, sa->actDeferAction.cbData);
				break;
			case OP_BOUNCE:
				da->__union = SOAP_UNION__act_bouncecode;
				da->act.bouncecode = sa->scBounceCode;
				break;
			case OP_FORWARD:
			case OP_DELEGATE: {
				if (sa->lpadrlist == nullptr) {
					da->act.adrlist = nullptr;
					break;
				}
				da->__union = SOAP_UNION__act_adrlist;
				auto hr = CopyMAPIRowSetToSOAPRowSet(reinterpret_cast<const SRowSet *>(sa->lpadrlist),
				          &da->act.adrlist, lpConverter);
				if(hr != hrSuccess)
					return hr;
				break;
			}
			case OP_TAG: {
				da->__union = SOAP_UNION__act_prop;
				da->act.prop = soap_new_propVal(nullptr);
				auto hr = CopyMAPIPropValToSOAPPropVal(da->act.prop, &sa->propTag, lpConverter);
				if (hr != hrSuccess)
					return hr;
				break;
			}
			case OP_DELETE:
			case OP_MARK_AS_READ:
				da->__union = INT_MAX; /* serialize neither member */
				// no other data needed
				break;
			}
		}
		break;
	}
	default:
		return MAPI_E_INVALID_TYPE;
	}
	return hrSuccess;
}

HRESULT CopySOAPPropValToMAPIPropVal(SPropValue *dp, const struct propVal *sp,
    void *lpBase, convert_context *lpConverter)
{
	dp->ulPropTag = sp->ulPropTag;
	dp->dwAlignPad = 0;

	// FIXME check pointer is OK before using in (sp->Value.hilo may be NULL!)
	switch(PROP_TYPE(sp->ulPropTag)) {
	case PT_I2:
		dp->Value.i = sp->Value.i;
		break;
	case PT_LONG:
		dp->Value.ul = sp->Value.ul;
		break;
	case PT_R4:
		dp->Value.flt = sp->Value.flt;
		break;
	case PT_DOUBLE:
		dp->Value.dbl = sp->Value.dbl;
		break;
	case PT_CURRENCY:
		if (sp->__union != SOAP_UNION_propValData_hilo || sp->Value.hilo == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.cur.Hi = sp->Value.hilo->hi;
		dp->Value.cur.Lo = sp->Value.hilo->lo;
		break;
	case PT_APPTIME:
		dp->Value.at = sp->Value.dbl;
		break;
	case PT_ERROR:
		dp->Value.err = kcerr_to_mapierr(sp->Value.ul);
		break;
	case PT_BOOLEAN:
		dp->Value.b = sp->Value.b;
		break;
	case PT_OBJECT:
		// can never be transmitted over the wire!
		return MAPI_E_INVALID_TYPE;
	case PT_I8:
		dp->Value.li.QuadPart = sp->Value.li;
		break;
	case PT_STRING8: {
		if (sp->__union != SOAP_UNION_propValData_lpszA || sp->Value.lpszA == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		auto s = CONVERT_TO(lpConverter, std::string, sp->Value.lpszA, rawsize(sp->Value.lpszA), "UTF-8");
		auto hr = MAPIAllocateMore(s.length() + 1, lpBase, reinterpret_cast<void **>(&dp->Value.lpszA));
		if (hr != hrSuccess)
			return hr;
		strcpy(dp->Value.lpszA, s.c_str());
		break;
	}
	case PT_UNICODE: {
		if (sp->__union != SOAP_UNION_propValData_lpszA || sp->Value.lpszA == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		auto ws = CONVERT_TO(lpConverter, std::wstring, sp->Value.lpszA, rawsize(sp->Value.lpszA), "UTF-8");
		auto hr = MAPIAllocateMore(sizeof(wchar_t) * (ws.length() + 1),
		          lpBase, reinterpret_cast<void **>(&dp->Value.lpszW));
		if (hr != hrSuccess)
			return hr;
		wcscpy(dp->Value.lpszW, ws.c_str());
		break;
	}
	case PT_SYSTIME:
		if (sp->__union != SOAP_UNION_propValData_hilo || sp->Value.hilo == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.ft.dwHighDateTime = sp->Value.hilo->hi;
		dp->Value.ft.dwLowDateTime = sp->Value.hilo->lo;
		break;
	case PT_CLSID: {
		if (sp->__union != SOAP_UNION_propValData_bin || sp->Value.bin == nullptr ||
		    sp->Value.bin->__size != sizeof(MAPIUID)) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		auto hr = MAPIAllocateMore(sp->Value.bin->__size, lpBase,
		          reinterpret_cast<void **>(&dp->Value.lpguid));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.lpguid, sp->Value.bin->__ptr, sp->Value.bin->__size);
		break;
	}
	case PT_BINARY: {
		if (sp->__union != SOAP_UNION_propValData_bin) {
			dp->Value.bin.lpb = NULL;
			dp->Value.bin.cb = 0;
			break;
		} else if (sp->Value.bin == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		auto hr = MAPIAllocateMore(sp->Value.bin->__size, lpBase,
		     reinterpret_cast<void **>(&dp->Value.bin.lpb));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.bin.lpb, sp->Value.bin->__ptr, sp->Value.bin->__size);
		dp->Value.bin.cb = sp->Value.bin->__size;
		break;
	}
	case PT_MV_I2: {
		if (sp->__union != SOAP_UNION_propValData_mvi || sp->Value.mvi.__ptr == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVi.cValues = sp->Value.mvi.__size;
		auto hr = MAPIAllocateMore(sizeof(short int) * dp->Value.MVi.cValues,
		          lpBase, reinterpret_cast<void **>(&dp->Value.MVi.lpi));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.MVi.lpi, sp->Value.mvi.__ptr, sizeof(short int)*dp->Value.MVi.cValues);
		break;
	}
	case PT_MV_LONG: {
		if (sp->__union != SOAP_UNION_propValData_mvl || sp->Value.mvl.__ptr == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVl.cValues = sp->Value.mvl.__size;
		auto hr = MAPIAllocateMore(sizeof(unsigned int) * dp->Value.MVl.cValues,
		          lpBase, reinterpret_cast<void **>(&dp->Value.MVl.lpl));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.MVl.lpl, sp->Value.mvl.__ptr, sizeof(unsigned int)*dp->Value.MVl.cValues);
		break;
	}
	case PT_MV_R4: {
		if (sp->__union != SOAP_UNION_propValData_mvflt || sp->Value.mvflt.__ptr == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVflt.cValues = sp->Value.mvflt.__size;
		auto hr = MAPIAllocateMore(sizeof(float) * dp->Value.MVflt.cValues,
		          lpBase, reinterpret_cast<void **>(&dp->Value.MVflt.lpflt));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.MVflt.lpflt, sp->Value.mvflt.__ptr, sizeof(float)*dp->Value.MVflt.cValues);
		break;
	}
	case PT_MV_DOUBLE: {
		if (sp->__union != SOAP_UNION_propValData_mvdbl || sp->Value.mvdbl.__ptr == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVdbl.cValues = sp->Value.mvdbl.__size;
		auto hr = MAPIAllocateMore(sizeof(double) * dp->Value.MVdbl.cValues,
		          lpBase, reinterpret_cast<void **>(&dp->Value.MVdbl.lpdbl));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.MVdbl.lpdbl, sp->Value.mvdbl.__ptr, sizeof(double)*dp->Value.MVdbl.cValues);
		break;
	}
	case PT_MV_CURRENCY: {
		if (sp->__union != SOAP_UNION_propValData_mvhilo || sp->Value.mvhilo.__ptr == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVcur.cValues = sp->Value.mvhilo.__size;
		auto hr = MAPIAllocateMore(sizeof(hiloLong) * dp->Value.MVcur.cValues,
		          lpBase, reinterpret_cast<void **>(&dp->Value.MVcur.lpcur));
		if (hr != hrSuccess)
			return hr;
		for (unsigned int i = 0; i < dp->Value.MVcur.cValues; ++i) {
			dp->Value.MVcur.lpcur[i].Hi = sp->Value.mvhilo.__ptr[i].hi;
			dp->Value.MVcur.lpcur[i].Lo = sp->Value.mvhilo.__ptr[i].lo;
		}
		break;
	}
	case PT_MV_APPTIME: {
		if (sp->__union != SOAP_UNION_propValData_mvdbl || sp->Value.mvdbl.__ptr == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVat.cValues = sp->Value.mvdbl.__size;
		auto hr = MAPIAllocateMore(sizeof(double) * dp->Value.MVat.cValues,
		          lpBase, reinterpret_cast<void **>(&dp->Value.MVat.lpat));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.MVat.lpat, sp->Value.mvdbl.__ptr, sizeof(double)*dp->Value.MVat.cValues);
		break;
	}
	case PT_MV_SYSTIME: {
		if (sp->__union != SOAP_UNION_propValData_mvhilo || sp->Value.mvhilo.__ptr == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVft.cValues = sp->Value.mvhilo.__size;
		auto hr = MAPIAllocateMore(sizeof(hiloLong) * dp->Value.MVft.cValues,
		          lpBase, reinterpret_cast<void **>(&dp->Value.MVft.lpft));
		if (hr != hrSuccess)
			return hr;
		for (unsigned int i = 0; i < dp->Value.MVft.cValues; ++i) {
			dp->Value.MVft.lpft[i].dwHighDateTime = sp->Value.mvhilo.__ptr[i].hi;
			dp->Value.MVft.lpft[i].dwLowDateTime = sp->Value.mvhilo.__ptr[i].lo;
		}
		break;
	}
	case PT_MV_BINARY: {
		if (sp->__union != SOAP_UNION_propValData_mvbin || sp->Value.mvbin.__ptr == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVbin.cValues = sp->Value.mvbin.__size;
		auto hr = MAPIAllocateMore(sizeof(SBinary) * dp->Value.MVbin.cValues,
		          lpBase, reinterpret_cast<void **>(&dp->Value.MVbin.lpbin));
		if (hr != hrSuccess)
			return hr;
		for (unsigned int i = 0; i < dp->Value.MVbin.cValues; ++i) {
			dp->Value.MVbin.lpbin[i].cb = sp->Value.mvbin.__ptr[i].__size;
			if (dp->Value.MVbin.lpbin[i].cb == 0) {
				dp->Value.MVbin.lpbin[i].lpb = NULL;
				continue;
			}
			hr = MAPIAllocateMore(dp->Value.MVbin.lpbin[i].cb, lpBase,
			     reinterpret_cast<void **>(&dp->Value.MVbin.lpbin[i].lpb));
			if (hr != hrSuccess)
				return hr;
			memcpy(dp->Value.MVbin.lpbin[i].lpb, sp->Value.mvbin.__ptr[i].__ptr, dp->Value.MVbin.lpbin[i].cb);
		}
		break;
	}
	case PT_MV_STRING8: {
		if (sp->__union != SOAP_UNION_propValData_mvszA || sp->Value.mvszA.__ptr == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		if (lpConverter == NULL) {
			convert_context converter;
			CopySOAPPropValToMAPIPropVal(dp, sp, lpBase, &converter);
			break;
		}
		dp->Value.MVszA.cValues = sp->Value.mvszA.__size;
		auto hr = MAPIAllocateMore(sizeof(char *) * dp->Value.MVszA.cValues,
		          lpBase, reinterpret_cast<void **>(&dp->Value.MVszA.lppszA));

		for (unsigned int i = 0; i < dp->Value.MVszA.cValues; ++i) {
			if (sp->Value.mvszA.__ptr[i] != NULL) {
				auto s = lpConverter->convert_to<std::string>(sp->Value.mvszA.__ptr[i], rawsize(sp->Value.mvszA.__ptr[i]), "UTF-8");
				hr = MAPIAllocateMore(s.size() + 1, lpBase, reinterpret_cast<void **>(&dp->Value.MVszA.lppszA[i]));
				if (hr != hrSuccess)
					return hr;
				strcpy(dp->Value.MVszA.lppszA[i], s.c_str());
			} else {
				hr = MAPIAllocateMore(1, lpBase, reinterpret_cast<void **>(&dp->Value.MVszA.lppszA[i]));
				if (hr != hrSuccess)
					return hr;
				dp->Value.MVszA.lppszA[i][0] = '\0';
			}
		}
		break;
	}
	case PT_MV_UNICODE: {
		if (sp->__union != SOAP_UNION_propValData_mvszA || sp->Value.mvszA.__ptr == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		if (lpConverter == NULL) {
			convert_context converter;
			CopySOAPPropValToMAPIPropVal(dp, sp, lpBase, &converter);
			break;
		}
		dp->Value.MVszW.cValues = sp->Value.mvszA.__size;
		auto hr = MAPIAllocateMore(sizeof(wchar_t *) * dp->Value.MVszW.cValues,
		          lpBase, reinterpret_cast<void **>(&dp->Value.MVszW.lppszW));
		if (hr != hrSuccess)
			return hr;

		for (unsigned int i = 0; i < dp->Value.MVszW.cValues; ++i) {
			if (sp->Value.mvszA.__ptr[i] == nullptr) {
				hr = MAPIAllocateMore(1, lpBase, reinterpret_cast<void **>(&dp->Value.MVszW.lppszW[i]));
				if (hr != hrSuccess)
					return hr;
				dp->Value.MVszW.lppszW[i][0] = '\0';
				continue;
			}
			auto ws = lpConverter->convert_to<std::wstring>(sp->Value.mvszA.__ptr[i], rawsize(sp->Value.mvszA.__ptr[i]), "UTF-8");
			hr = MAPIAllocateMore(sizeof(std::wstring::value_type) * (ws.length() + 1), lpBase,
			     reinterpret_cast<void **>(&dp->Value.MVszW.lppszW[i]));
			if (hr != hrSuccess)
				return hr;
			wcscpy(dp->Value.MVszW.lppszW[i], ws.c_str());
		}
		break;
	}
	case PT_MV_CLSID: {
		if (sp->__union != SOAP_UNION_propValData_mvbin || sp->Value.mvbin.__ptr == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVguid.cValues = sp->Value.mvbin.__size;
		auto hr = MAPIAllocateMore(sizeof(GUID) * dp->Value.MVguid.cValues,
		          lpBase, reinterpret_cast<void **>(&dp->Value.MVguid.lpguid));
		if (hr != hrSuccess)
			return hr;
		for (unsigned int i = 0; i < dp->Value.MVguid.cValues; ++i)
			memcpy(&dp->Value.MVguid.lpguid[i], sp->Value.mvbin.__ptr[i].__ptr, sizeof(GUID));
		break;
	}
	case PT_MV_I8: {
		if (sp->__union != SOAP_UNION_propValData_mvli || sp->Value.mvli.__ptr == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVli.cValues = sp->Value.mvli.__size;
		auto hr = MAPIAllocateMore(sizeof(LARGE_INTEGER) * dp->Value.MVli.cValues,
		          lpBase, reinterpret_cast<void **>(&dp->Value.MVli.lpli));
		if (hr != hrSuccess)
			return hr;
		for (unsigned int i = 0; i < dp->Value.MVli.cValues; ++i)
			dp->Value.MVli.lpli[i].QuadPart = sp->Value.mvli.__ptr[i];
		break;
	}
	case PT_SRESTRICTION: {
		if (sp->__union != SOAP_UNION_propValData_res || sp->Value.res == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		// NOTE: we place the object pointer in lpszA to make sure it's on the same offset as Value.x on 32-bit and 64-bit machines
		auto hr = MAPIAllocateMore(sizeof(SRestriction), lpBase,
		          reinterpret_cast<void **>(&dp->Value.lpszA));
		if (hr != hrSuccess)
			return hr;
		return CopySOAPRestrictionToMAPIRestriction(reinterpret_cast<SRestriction *>(dp->Value.lpszA),
		       sp->Value.res, lpBase, lpConverter);
	}
	case PT_ACTIONS: {
		if (sp->__union != SOAP_UNION_propValData_actions || sp->Value.actions == nullptr) {
			dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		// NOTE: we place the object pointer in lpszA to make sure it is on the same offset as Value.x on 32-bit and 64-bit machines
		auto hr = MAPIAllocateMore(sizeof(ACTIONS), lpBase,
		          reinterpret_cast<void **>(&dp->Value.lpszA));
		if (hr != hrSuccess)
			return hr;
		auto lpDstActions = reinterpret_cast<ACTIONS *>(dp->Value.lpszA);
		lpDstActions->cActions = sp->Value.actions->__size;
		hr = MAPIAllocateMore(sizeof(ACTION) * sp->Value.actions->__size, lpBase,
		     reinterpret_cast<void **>(&lpDstActions->lpAction));
		if (hr != hrSuccess)
			return hr;

		lpDstActions->ulVersion = EDK_RULES_VERSION;
		for (gsoap_size_t i = 0; i < sp->Value.actions->__size; ++i) {
			auto da = &lpDstActions->lpAction[i];
			const auto sa = &sp->Value.actions->__ptr[i];

			da->acttype = (ACTTYPE)sa->acttype;
			da->ulActionFlavor = sa->flavor;
			da->ulFlags = sa->flags;
			da->lpRes = NULL;
			da->lpPropTagArray = NULL;

			switch(sa->acttype) {
			case OP_MOVE:
			case OP_COPY:
				da->actMoveCopy.cbStoreEntryId = sa->act.moveCopy.store.__size;
				hr = MAPIAllocateMore(sa->act.moveCopy.store.__size, lpBase, reinterpret_cast<void **>(&da->actMoveCopy.lpStoreEntryId));
				if (hr != hrSuccess)
					return hr;
				memcpy(da->actMoveCopy.lpStoreEntryId, sa->act.moveCopy.store.__ptr, sa->act.moveCopy.store.__size);
				da->actMoveCopy.cbFldEntryId = sa->act.moveCopy.folder.__size;
				hr = MAPIAllocateMore(sa->act.moveCopy.folder.__size, lpBase, reinterpret_cast<void **>(&da->actMoveCopy.lpFldEntryId));
				if (hr != hrSuccess)
					return hr;
				memcpy(da->actMoveCopy.lpFldEntryId, sa->act.moveCopy.folder.__ptr, sa->act.moveCopy.folder.__size);
				break;
			case OP_REPLY:
			case OP_OOF_REPLY:
				da->actReply.cbEntryId = sa->act.reply.message.__size;
				hr = MAPIAllocateMore(sa->act.reply.message.__size, lpBase, reinterpret_cast<void **>(&da->actReply.lpEntryId));
				if (hr != hrSuccess)
					return hr;
				memcpy(da->actReply.lpEntryId, sa->act.reply.message.__ptr, sa->act.reply.message.__size);
				if (sa->act.reply.guid.__size != sizeof(GUID))
					return MAPI_E_CORRUPT_DATA;
				memcpy(&da->actReply.guidReplyTemplate, sa->act.reply.guid.__ptr, sa->act.reply.guid.__size);
				break;
			case OP_DEFER_ACTION:
				hr = MAPIAllocateMore(sa->act.defer.bin.__size, lpBase, reinterpret_cast<void **>(&da->actDeferAction.pbData));
				if (hr != hrSuccess)
					return hr;
				da->actDeferAction.cbData = sa->act.defer.bin.__size;
				memcpy(da->actDeferAction.pbData, sa->act.defer.bin.__ptr,sa->act.defer.bin.__size);
				break;
			case OP_BOUNCE:
				da->scBounceCode = sa->act.bouncecode;
				break;
			case OP_FORWARD:
			case OP_DELEGATE:
				if (sa->act.adrlist == NULL)
					return MAPI_E_CORRUPT_DATA;
				hr = MAPIAllocateMore(CbNewADRLIST(sa->act.adrlist->__size), lpBase, reinterpret_cast<void **>(&da->lpadrlist));
				if (hr != hrSuccess)
					return hr;
				da->lpadrlist->cEntries = 0;
				for (gsoap_size_t j = 0; j < sa->act.adrlist->__size; ++j) {
					da->lpadrlist->aEntries[j].ulReserved1 = 0;
					da->lpadrlist->aEntries[j].cValues = sa->act.adrlist->__ptr[j].__size;

					// new rowset allocate more on old rowset, so we can just call FreeProws once
					hr = MAPIAllocateMore(sizeof(SPropValue) * sa->act.adrlist->__ptr[j].__size, lpBase,
					     reinterpret_cast<void **>(&da->lpadrlist->aEntries[j].rgPropVals));
					if (hr != hrSuccess)
						return hr;
					++da->lpadrlist->cEntries;
					hr = CopySOAPRowToMAPIRow(&sa->act.adrlist->__ptr[j], da->lpadrlist->aEntries[j].rgPropVals, lpBase, lpConverter);
					if (hr != hrSuccess)
						return hr;
				}
				// FIXME rowset is not coupled to action -> leaks!
				break;
			case OP_TAG:
				hr = CopySOAPPropValToMAPIPropVal(&da->propTag, sa->act.prop, lpBase, lpConverter);
				if (hr != hrSuccess)
					return hr;
				break;
			}
		}
		break;
	}
	default:
		dp->ulPropTag = CHANGE_PROP_TYPE(sp->ulPropTag, PT_ERROR);
		dp->Value.err = MAPI_E_NOT_FOUND;
		break;
	}
	return hrSuccess;
}

HRESULT CopySOAPRowToMAPIRow(void *lpProvider,
    const struct propValArray *lpsRowSrc, LPSPropValue lpsRowDst,
    void **lpBase, ULONG ulType, convert_context *lpConverter)
{
	if (lpConverter == NULL && lpsRowSrc->__size > 1) {
		// Try again with a converter to reuse the iconv instances
		convert_context converter;
		return CopySOAPRowToMAPIRow(lpProvider, lpsRowSrc, lpsRowDst, lpBase, ulType, &converter);
	}

	for (gsoap_size_t j = 0; j < lpsRowSrc->__size; ++j) {
		// First, try the default TableRowGetProp from ECMAPIProp
		if((ulType == MAPI_STORE || ulType == MAPI_FOLDER || ulType == MAPI_MESSAGE || ulType == MAPI_ATTACH) &&
			ECMAPIProp::TableRowGetProp(lpProvider, &lpsRowSrc->__ptr[j], &lpsRowDst[j], lpBase, ulType) == erSuccess)
			continue;
		else if((ulType == MAPI_MAILUSER || ulType == MAPI_ABCONT || ulType == MAPI_DISTLIST)&&
			ECABProp::TableRowGetProp(lpProvider, &lpsRowSrc->__ptr[j], &lpsRowDst[j], lpBase, ulType) == erSuccess)
			continue;

		switch(ulType) {
		case MAPI_FOLDER:
			// Then, try the specialized TableRowGetProp for the type of table we're handling
			if (ECMAPIFolder::TableRowGetProp(lpProvider, &lpsRowSrc->__ptr[j], &lpsRowDst[j], lpBase, ulType) == erSuccess)
				continue;
			break;
		case MAPI_MESSAGE:
			if (ECMessage::TableRowGetProp(lpProvider, &lpsRowSrc->__ptr[j], &lpsRowDst[j], lpBase, ulType) == erSuccess)
				continue;
			break;
		case MAPI_MAILUSER:
			if (ECMailUser::TableRowGetProp(lpProvider, &lpsRowSrc->__ptr[j], &lpsRowDst[j], lpBase, ulType) == erSuccess)
				continue;
			break;
		case MAPI_DISTLIST:
			if (ECDistList::TableRowGetProp(lpProvider, &lpsRowSrc->__ptr[j], &lpsRowDst[j], lpBase, ulType) == erSuccess)
				continue;
			break;
		case MAPI_ABCONT:
			if (ECABContainer::TableRowGetProp(lpProvider, &lpsRowSrc->__ptr[j], &lpsRowDst[j], lpBase, ulType) == erSuccess)
				continue;
			break;
		case MAPI_STORE:
			if (ECMsgStore::TableRowGetProp(lpProvider, &lpsRowSrc->__ptr[j], &lpsRowDst[j], lpBase, ulType) == erSuccess)
				continue;
			break;
		}

		if (ECGenericProp::TableRowGetProp(lpProvider, &lpsRowSrc->__ptr[j], &lpsRowDst[j], lpBase, ulType) == erSuccess)
			continue;

		// If all fails, get the actual data from the server
		CopySOAPPropValToMAPIPropVal(&lpsRowDst[j], &lpsRowSrc->__ptr[j], lpBase, lpConverter);
	}
	return hrSuccess;
}

HRESULT CopyMAPIEntryIdToSOAPEntryId(ULONG cbEntryIdSrc,
    const ENTRYID *lpEntryIdSrc, entryId **lppDest)
{
	auto lpDest = soap_new_entryId(nullptr);
	auto hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryIdSrc, lpEntryIdSrc, lpDest, false);
	if (hr != hrSuccess) {
		soap_del_PointerToentryId(&lpDest);
		return hr;
	}
	*lppDest = lpDest;
	return hrSuccess;
}

HRESULT CopyMAPIEntryIdToSOAPEntryId(ULONG cbEntryIdSrc,
    const ENTRYID *lpEntryIdSrc, entryId *lpDest, bool bCheapCopy)
{
	if ((cbEntryIdSrc > 0 && lpEntryIdSrc == NULL) || lpDest == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if(cbEntryIdSrc == 0)	{
		lpDest->__ptr = NULL;
		lpDest->__size = 0;
		return hrSuccess;
	}
	if (!bCheapCopy) {
		lpDest->__ptr = soap_new_unsignedByte(nullptr, cbEntryIdSrc);
		memcpy(lpDest->__ptr, lpEntryIdSrc, cbEntryIdSrc);
	}else{
		lpDest->__ptr = (LPBYTE)lpEntryIdSrc;
	}

	lpDest->__size = cbEntryIdSrc;
	return hrSuccess;
}

HRESULT CopySOAPEntryIdToMAPIEntryId(const entryId *lpSrc,
    unsigned int *lpcbDest, ENTRYID **lppEntryIdDest, void *lpBase)
{
	if (lpSrc == nullptr || lpcbDest == nullptr || lppEntryIdDest == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (static_cast<unsigned int>(lpSrc->__size) < CbNewABEID("") ||
	    lpSrc->__ptr == nullptr)
		return MAPI_E_INVALID_ENTRYID;

	ULONG		cbEntryId = 0;
	LPENTRYID	lpEntryId = NULL;
	auto hr = KAllocCopy(lpSrc->__ptr, lpSrc->__size, reinterpret_cast<void **>(&lpEntryId), lpBase);
	if (hr != hrSuccess)
		return hr;
	cbEntryId = lpSrc->__size;

	*lppEntryIdDest = lpEntryId;
	*lpcbDest = cbEntryId;
	return hrSuccess;
}

HRESULT CopyMAPIEntryListToSOAPEntryList(const ENTRYLIST *lpMsgList,
    struct entryList *lpsEntryList)
{
	if (lpMsgList == nullptr || lpsEntryList == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (lpMsgList->cValues == 0 || lpMsgList->lpbin == nullptr) {
		lpsEntryList->__ptr = NULL;
		lpsEntryList->__size = 0;
		return hrSuccess;
	}

	unsigned int i = 0;
	lpsEntryList->__ptr = soap_new_entryId(nullptr, lpMsgList->cValues);
	for (i = 0; i < lpMsgList->cValues; ++i) {
		lpsEntryList->__ptr[i].__ptr = soap_new_unsignedByte(nullptr, lpMsgList->lpbin[i].cb);
		memcpy(lpsEntryList->__ptr[i].__ptr, lpMsgList->lpbin[i].lpb, lpMsgList->lpbin[i].cb);

		lpsEntryList->__ptr[i].__size = lpMsgList->lpbin[i].cb;
	}

	lpsEntryList->__size = i;
	return hrSuccess;
}

HRESULT CopySOAPEntryListToMAPIEntryList(const struct entryList *lpsEntryList,
    LPENTRYLIST *lppMsgList)
{
	if (lpsEntryList == nullptr || lppMsgList == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	unsigned int	i = 0;
	memory_ptr<ENTRYLIST> lpMsgList;
	auto hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~lpMsgList);
	if(hr != hrSuccess)
		return hr;

	if(lpsEntryList->__size == 0) {
		lpMsgList->cValues = 0;
		lpMsgList->lpbin = NULL;
	} else {
		hr = MAPIAllocateMore(lpsEntryList->__size * sizeof(SBinary),
		     lpMsgList, reinterpret_cast<void **>(&lpMsgList->lpbin));
		if(hr != hrSuccess)
			return hr;
	}

	for (i = 0; i < lpsEntryList->__size; ++i) {
		hr = MAPIAllocateMore(lpsEntryList->__ptr[i].__size, lpMsgList,
		     reinterpret_cast<void **>(&lpMsgList->lpbin[i].lpb));
		if(hr != hrSuccess)
			return hr;
		memcpy(lpMsgList->lpbin[i].lpb, lpsEntryList->__ptr[i].__ptr, lpsEntryList->__ptr[i].__size);

		lpMsgList->lpbin[i].cb = lpsEntryList->__ptr[i].__size;
	}

	lpMsgList->cValues = i;
	*lppMsgList = lpMsgList.release();
	return hrSuccess;
}

HRESULT CopySOAPRowToMAPIRow(const struct propValArray *lpsRowSrc,
    LPSPropValue lpsRowDst, void *lpBase, convert_context *lpConverter)
{
	if (lpConverter == NULL && lpsRowSrc->__size > 1) {
		convert_context converter;
		return CopySOAPRowToMAPIRow(lpsRowSrc, lpsRowDst, lpBase, &converter);
	}

	for (gsoap_size_t j = 0; j < lpsRowSrc->__size; ++j) {
		// If all fails, get the actual data from the server
		auto hr = CopySOAPPropValToMAPIPropVal(&lpsRowDst[j], &lpsRowSrc->__ptr[j], lpBase, lpConverter);
		if(hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

HRESULT CopyMAPIRowToSOAPRow(const SRow *lpRowSrc,
    struct propValArray *lpsRowDst, convert_context *lpConverter)
{
	if (lpConverter == NULL && lpRowSrc->cValues > 1) {
		convert_context converter;
		return CopyMAPIRowToSOAPRow(lpRowSrc, lpsRowDst, &converter);
	}

	auto lpPropVal = soap_new_propVal(nullptr, lpRowSrc->cValues);
	lpsRowDst->__ptr = lpPropVal;
	lpsRowDst->__size = 0;

	for (unsigned int i = 0; i < lpRowSrc->cValues; ++i) {
		auto hr = CopyMAPIPropValToSOAPPropVal(&lpPropVal[i], &lpRowSrc->lpProps[i], lpConverter);
		if (hr != hrSuccess) {
			soap_del_propValArray(lpsRowDst);
			lpsRowDst->__ptr = nullptr;
			return hr;
		}
		++lpsRowDst->__size;
	}
	return hrSuccess;
}

HRESULT CopyMAPIRowSetToSOAPRowSet(const SRowSet *lpRowSetSrc,
    struct rowSet **lppsRowSetDst, convert_context *lpConverter)
{
	if (lpConverter == NULL && lpRowSetSrc->cRows > 1) {
		convert_context converter;
		return CopyMAPIRowSetToSOAPRowSet(lpRowSetSrc, lppsRowSetDst, &converter);
	}
	auto lpsRowSetDst = soap_new_rowSet(nullptr);
	lpsRowSetDst->__ptr = NULL;
	lpsRowSetDst->__size = 0;
	if (lpRowSetSrc->cRows > 0) {
		lpsRowSetDst->__ptr  = soap_new_propValArray(nullptr, lpRowSetSrc->cRows);
		lpsRowSetDst->__size = 0;

		for (unsigned int i = 0; i < lpRowSetSrc->cRows; ++i) {
			auto hr = CopyMAPIRowToSOAPRow(&lpRowSetSrc->aRow[i], &lpsRowSetDst->__ptr[i], lpConverter);
			if (hr != hrSuccess) {
				soap_del_PointerTorowSet(&lpsRowSetDst);
				return hr;
			}
			++lpsRowSetDst->__size;
		}
	}

	*lppsRowSetDst = lpsRowSetDst;
	return hrSuccess;
}

// Copies a row set, filling in client-side generated values on the fly
HRESULT CopySOAPRowSetToMAPIRowSet(void *lpProvider,
    const struct rowSet *lpsRowSetSrc, LPSRowSet *lppRowSetDst, ULONG ulType)
{
	ULONG ulRows = 0;
	rowset_ptr lpRowSet;
	convert_context converter;

	ulRows = lpsRowSetSrc->__size;

	// Allocate space for the rowset
	auto hr = MAPIAllocateBuffer(CbNewSRowSet(ulRows), &~lpRowSet);
	if (hr != hrSuccess)
		return hr;

	// Loop through all the rows and values, fill in any client-side generated values, or translate
	// some serverside values through TableRowGetProps

	for (lpRowSet->cRows = 0; lpRowSet->cRows < ulRows; ++lpRowSet->cRows) {
		auto i = lpRowSet->cRows;
		lpRowSet->aRow[i].ulAdrEntryPad = 0;
		lpRowSet->aRow[i].cValues = lpsRowSetSrc->__ptr[i].__size;
		hr = MAPIAllocateBuffer(sizeof(SPropValue) * lpsRowSetSrc->__ptr[i].__size, reinterpret_cast<void **>(&lpRowSet->aRow[i].lpProps));
		if (hr != hrSuccess)
			return hr;
		CopySOAPRowToMAPIRow(lpProvider, &lpsRowSetSrc->__ptr[i], lpRowSet->aRow[i].lpProps, reinterpret_cast<void **>(lpRowSet->aRow[i].lpProps), ulType, &converter);
	}

	*lppRowSetDst = lpRowSet.release();
	return hrSuccess;
}

HRESULT CopySOAPRestrictionToMAPIRestriction(LPSRestriction lpDst,
    const struct restrictTable *lpSrc, void *lpBase,
    convert_context *lpConverter)
{
	if (lpSrc == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (lpConverter == nullptr) {
		convert_context converter;
		CopySOAPRestrictionToMAPIRestriction(lpDst, lpSrc, lpBase, &converter);
		return hrSuccess;
	}

	memset(lpDst, 0, sizeof(SRestriction));
	lpDst->rt = lpSrc->ulType;

	switch(lpSrc->ulType) {
	case RES_OR: {
		if (lpSrc->lpOr == NULL)
			return MAPI_E_INVALID_PARAMETER;
		lpDst->res.resOr.cRes = lpSrc->lpOr->__size;
		auto hr = MAPIAllocateMore(sizeof(SRestriction) * lpSrc->lpOr->__size,
		          lpBase, reinterpret_cast<void **>(&lpDst->res.resOr.lpRes));
		if (hr != hrSuccess)
			return hr;
		for (gsoap_size_t i = 0; i < lpSrc->lpOr->__size; ++i) {
			hr = CopySOAPRestrictionToMAPIRestriction(&lpDst->res.resOr.lpRes[i], lpSrc->lpOr->__ptr[i], lpBase, lpConverter);

			if(hr != hrSuccess)
				return hr;
		}
		break;
	}
	case RES_AND: {
		if (lpSrc->lpAnd == NULL)
			return MAPI_E_INVALID_PARAMETER;
		lpDst->res.resAnd.cRes = lpSrc->lpAnd->__size;
		auto hr = MAPIAllocateMore(sizeof(SRestriction) * lpSrc->lpAnd->__size,
		          lpBase, reinterpret_cast<void **>(&lpDst->res.resAnd.lpRes));
		if (hr != hrSuccess)
			return hr;
		for (gsoap_size_t i = 0; i < lpSrc->lpAnd->__size; ++i) {
			hr = CopySOAPRestrictionToMAPIRestriction(&lpDst->res.resAnd.lpRes[i], lpSrc->lpAnd->__ptr[i], lpBase, lpConverter);

			if(hr != hrSuccess)
				return hr;
		}
		break;
	}
	case RES_BITMASK:
		if (lpSrc->lpBitmask == NULL)
			return MAPI_E_INVALID_PARAMETER;

		lpDst->res.resBitMask.relBMR = lpSrc->lpBitmask->ulType;
		lpDst->res.resBitMask.ulMask = lpSrc->lpBitmask->ulMask;
		lpDst->res.resBitMask.ulPropTag = lpSrc->lpBitmask->ulPropTag;
		break;

	case RES_COMMENT: {
		if (lpSrc->lpComment == NULL)
			return MAPI_E_INVALID_PARAMETER;
		auto hr = MAPIAllocateMore(sizeof(SRestriction), lpBase,
		          reinterpret_cast<void **>(&lpDst->res.resComment.lpRes));
		if (hr != hrSuccess)
			return hr;
		hr = CopySOAPRestrictionToMAPIRestriction(lpDst->res.resComment.lpRes, lpSrc->lpComment->lpResTable, lpBase, lpConverter);
		if (hr != hrSuccess)
			return hr;

		lpDst->res.resComment.cValues = lpSrc->lpComment->sProps.__size;
		hr = MAPIAllocateMore(sizeof(SPropValue) * lpSrc->lpComment->sProps.__size, lpBase, reinterpret_cast<void **>(&lpDst->res.resComment.lpProp));
		if (hr != hrSuccess)
			return hr;
		for (gsoap_size_t i = 0; i < lpSrc->lpComment->sProps.__size; ++i) {
			hr = CopySOAPPropValToMAPIPropVal(&lpDst->res.resComment.lpProp[i], &lpSrc->lpComment->sProps.__ptr[i], lpBase, lpConverter);
			if (hr != hrSuccess)
				return hr;
		}
		break;
	}
	case RES_COMPAREPROPS:
		if (lpSrc->lpCompare == NULL)
			return MAPI_E_INVALID_PARAMETER;
		lpDst->res.resCompareProps.relop = lpSrc->lpCompare->ulType;
		lpDst->res.resCompareProps.ulPropTag1 = lpSrc->lpCompare->ulPropTag1;
		lpDst->res.resCompareProps.ulPropTag2 = lpSrc->lpCompare->ulPropTag2;
		break;

	case RES_CONTENT: {
		if (lpSrc->lpContent == NULL || lpSrc->lpContent->lpProp == NULL)
			return MAPI_E_INVALID_PARAMETER;

		lpDst->res.resContent.ulFuzzyLevel = lpSrc->lpContent->ulFuzzyLevel;
		lpDst->res.resContent.ulPropTag = lpSrc->lpContent->ulPropTag;
		auto hr = MAPIAllocateMore(sizeof(SPropValue), lpBase,
		          reinterpret_cast<void **>(&lpDst->res.resContent.lpProp));
		if(hr != hrSuccess)
			return hr;
		return CopySOAPPropValToMAPIPropVal(lpDst->res.resContent.lpProp,
		       lpSrc->lpContent->lpProp, lpBase, lpConverter);
	}
	case RES_EXIST:
		if (lpSrc->lpExist == NULL)
			return MAPI_E_INVALID_PARAMETER;
		lpDst->res.resExist.ulPropTag = lpSrc->lpExist->ulPropTag;
		break;

	case RES_NOT: {
		if (lpSrc->lpNot == NULL || lpSrc->lpNot->lpNot == NULL)
			return MAPI_E_INVALID_PARAMETER;
		auto hr = MAPIAllocateMore(sizeof(SRestriction), lpBase,
		          reinterpret_cast<void **>(&lpDst->res.resNot.lpRes));
		if (hr != hrSuccess)
			return hr;
		return CopySOAPRestrictionToMAPIRestriction(lpDst->res.resNot.lpRes,
		       lpSrc->lpNot->lpNot, lpBase, lpConverter);
	}
	case RES_PROPERTY: {
		if (lpSrc->lpProp == NULL || lpSrc->lpProp->lpProp == NULL)
			return MAPI_E_INVALID_PARAMETER;
		auto hr = MAPIAllocateMore(sizeof(SPropValue), lpBase,
		          reinterpret_cast<void **>(&lpDst->res.resProperty.lpProp));
		if (hr != hrSuccess)
			return hr;
		lpDst->res.resProperty.relop = lpSrc->lpProp->ulType;
		lpDst->res.resProperty.ulPropTag = lpSrc->lpProp->ulPropTag;
		return CopySOAPPropValToMAPIPropVal(lpDst->res.resProperty.lpProp,
		       lpSrc->lpProp->lpProp, lpBase, lpConverter);
	}
	case RES_SIZE:
		if (lpSrc->lpSize == NULL)
			return MAPI_E_INVALID_PARAMETER;
		lpDst->res.resSize.cb = lpSrc->lpSize->cb;
		lpDst->res.resSize.relop = lpSrc->lpSize->ulType;
		lpDst->res.resSize.ulPropTag = lpSrc->lpSize->ulPropTag;
		break;

	case RES_SUBRESTRICTION: {
		if (lpSrc->lpSub == NULL || lpSrc->lpSub->lpSubObject == NULL)
			return MAPI_E_INVALID_PARAMETER;
		lpDst->res.resSub.ulSubObject = lpSrc->lpSub->ulSubObject;
		auto hr = MAPIAllocateMore(sizeof(SRestriction), lpBase,
		          reinterpret_cast<void **>(&lpDst->res.resSub.lpRes));
		if (hr != hrSuccess)
			return hr;
		return CopySOAPRestrictionToMAPIRestriction(lpDst->res.resSub.lpRes,
		       lpSrc->lpSub->lpSubObject, lpBase, lpConverter);
	}
	default:
		return MAPI_E_INVALID_PARAMETER;
	}
	return hrSuccess;
}

HRESULT CopyMAPIRestrictionToSOAPRestriction(struct restrictTable **lppDst,
    const SRestriction *lpSrc, convert_context *lpConverter)
{
	HRESULT hr = hrSuccess;
	struct restrictTable *lpDst = NULL;
	auto laters = make_scope_success([&]() {
		if (hr != hrSuccess)
			soap_del_PointerTorestrictTable(&lpDst);
	});

	if (lpConverter == NULL) {
		convert_context converter;
		return CopyMAPIRestrictionToSOAPRestriction(lppDst, lpSrc, &converter);
	}

	lpDst = soap_new_restrictTable(nullptr);
	lpDst->ulType = lpSrc->rt;

	switch(lpSrc->rt) {
	case RES_OR:
		lpDst->lpOr = soap_new_restrictOr(nullptr);
		lpDst->lpOr->__ptr  = reinterpret_cast<restrictTable **>(soap_malloc(nullptr, sizeof(restrictTable *) * lpSrc->res.resOr.cRes));
		lpDst->lpOr->__size = lpSrc->res.resOr.cRes;
		for (unsigned int i = 0; i < lpSrc->res.resOr.cRes; ++i) {
			hr = CopyMAPIRestrictionToSOAPRestriction(&(lpDst->lpOr->__ptr[i]), &lpSrc->res.resOr.lpRes[i], lpConverter);

			if(hr != hrSuccess)
				return hr;
		}
		break;

	case RES_AND:
		lpDst->lpAnd = soap_new_restrictAnd(nullptr);
		lpDst->lpAnd->__ptr  = reinterpret_cast<restrictTable **>(soap_malloc(nullptr, sizeof(restrictTable *) * lpSrc->res.resAnd.cRes));
		lpDst->lpAnd->__size = lpSrc->res.resAnd.cRes;
		for (unsigned int i = 0; i < lpSrc->res.resAnd.cRes; ++i) {
			hr = CopyMAPIRestrictionToSOAPRestriction(&lpDst->lpAnd->__ptr[i], &lpSrc->res.resAnd.lpRes[i], lpConverter);

			if(hr != hrSuccess)
				return hr;
		}
		break;

	case RES_BITMASK:
		lpDst->lpBitmask = soap_new_restrictBitmask(nullptr);
		lpDst->lpBitmask->ulMask = lpSrc->res.resBitMask.ulMask;
		lpDst->lpBitmask->ulPropTag = lpSrc->res.resBitMask.ulPropTag;
		lpDst->lpBitmask->ulType = lpSrc->res.resBitMask.relBMR;
		break;

	case RES_COMMENT:
		lpDst->lpComment = soap_new_restrictComment(nullptr);
		lpDst->lpComment->sProps.__ptr  = soap_new_propVal(nullptr, lpSrc->res.resComment.cValues);
		lpDst->lpComment->sProps.__size = lpSrc->res.resComment.cValues;
		for (unsigned int i = 0; i < lpSrc->res.resComment.cValues; ++i) {
			hr = CopyMAPIPropValToSOAPPropVal(&lpDst->lpComment->sProps.__ptr[i], &lpSrc->res.resComment.lpProp[i], lpConverter);
			if(hr != hrSuccess)
				return hr;
		}

		hr = CopyMAPIRestrictionToSOAPRestriction(&lpDst->lpComment->lpResTable, lpSrc->res.resComment.lpRes, lpConverter);
		if (hr != hrSuccess)
			return hr;
		break;

	case RES_COMPAREPROPS:
		lpDst->lpCompare = soap_new_restrictCompare(nullptr);
		lpDst->lpCompare->ulPropTag1 = lpSrc->res.resCompareProps.ulPropTag1;
		lpDst->lpCompare->ulPropTag2 = lpSrc->res.resCompareProps.ulPropTag2;
		lpDst->lpCompare->ulType = lpSrc->res.resCompareProps.relop;
		break;

	case RES_CONTENT:
		lpDst->lpContent = soap_new_restrictContent(nullptr);
		if( (PROP_TYPE(lpSrc->res.resContent.lpProp->ulPropTag) != PT_BINARY &&
			PROP_TYPE(lpSrc->res.resContent.lpProp->ulPropTag) != PT_MV_BINARY &&
			PROP_TYPE(lpSrc->res.resContent.lpProp->ulPropTag) != PT_STRING8 &&
			PROP_TYPE(lpSrc->res.resContent.lpProp->ulPropTag) != PT_MV_STRING8 &&
			PROP_TYPE(lpSrc->res.resContent.lpProp->ulPropTag) != PT_UNICODE &&
			PROP_TYPE(lpSrc->res.resContent.lpProp->ulPropTag) != PT_MV_UNICODE) ||
			(PROP_TYPE(lpSrc->res.resContent.lpProp->ulPropTag) == PT_BINARY && lpSrc->res.resContent.lpProp->Value.bin.cb >0 && lpSrc->res.resContent.lpProp->Value.bin.lpb == NULL) ||
			(PROP_TYPE(lpSrc->res.resContent.lpProp->ulPropTag) == PT_STRING8 && lpSrc->res.resContent.lpProp->Value.lpszA == NULL) ||
			(PROP_TYPE(lpSrc->res.resContent.lpProp->ulPropTag) == PT_UNICODE && lpSrc->res.resContent.lpProp->Value.lpszW == NULL))
			return MAPI_E_INVALID_PARAMETER;

		lpDst->lpContent->ulFuzzyLevel = lpSrc->res.resContent.ulFuzzyLevel;
		lpDst->lpContent->ulPropTag = lpSrc->res.resContent.ulPropTag;
		lpDst->lpContent->lpProp = soap_new_propVal(nullptr);
		hr = CopyMAPIPropValToSOAPPropVal(lpDst->lpContent->lpProp, lpSrc->res.resContent.lpProp, lpConverter);
		if(hr != hrSuccess)
			return hr;
		break;

	case RES_EXIST:
		lpDst->lpExist = soap_new_restrictExist(nullptr);
		lpDst->lpExist->ulPropTag = lpSrc->res.resExist.ulPropTag;
		break;

	case RES_NOT:
		lpDst->lpNot = soap_new_restrictNot(nullptr);
		hr = CopyMAPIRestrictionToSOAPRestriction(&lpDst->lpNot->lpNot, lpSrc->res.resNot.lpRes, lpConverter);
		if(hr != hrSuccess)
			return hr;
		break;

	case RES_PROPERTY:
		lpDst->lpProp = soap_new_restrictProp(nullptr);
		lpDst->lpProp->ulType = lpSrc->res.resProperty.relop;
		lpDst->lpProp->lpProp = soap_new_propVal(nullptr);
		lpDst->lpProp->ulPropTag = lpSrc->res.resProperty.ulPropTag;

		hr = CopyMAPIPropValToSOAPPropVal(lpDst->lpProp->lpProp, lpSrc->res.resProperty.lpProp, lpConverter);
		if(hr != hrSuccess)
			return hr;
		break;

	case RES_SIZE:
		lpDst->lpSize = soap_new_restrictSize(nullptr);
		lpDst->lpSize->cb = lpSrc->res.resSize.cb;
		lpDst->lpSize->ulPropTag = lpSrc->res.resSize.ulPropTag;
		lpDst->lpSize->ulType = lpSrc->res.resSize.relop;
		break;

	case RES_SUBRESTRICTION:
		lpDst->lpSub = soap_new_restrictSub(nullptr);
		lpDst->lpSub->ulSubObject = lpSrc->res.resSub.ulSubObject;
		hr = CopyMAPIRestrictionToSOAPRestriction(&lpDst->lpSub->lpSubObject, lpSrc->res.resSub.lpRes, lpConverter);
		if(hr != hrSuccess)
			return hr;
		break;

	default:
		hr = MAPI_E_INVALID_PARAMETER;
		return hr;
	}

	*lppDst = lpDst;
	return hr;
}

static HRESULT CopySOAPPropTagArrayToMAPIPropTagArray(
    const struct propTagArray *lpsPropTagArray,
    LPSPropTagArray *lppPropTagArray, void *lpBase)
{
	if (lpsPropTagArray == nullptr || lppPropTagArray == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	LPSPropTagArray	lpPropTagArray = NULL;
	auto hr = MAPIAllocateMore(CbNewSPropTagArray(lpsPropTagArray->__size), lpBase, reinterpret_cast<void **>(&lpPropTagArray));
	if(hr != hrSuccess)
		return hr;

	lpPropTagArray->cValues = lpsPropTagArray->__size;

	if(lpsPropTagArray->__size > 0)
		memcpy(lpPropTagArray->aulPropTag, lpsPropTagArray->__ptr, sizeof(unsigned int)*lpsPropTagArray->__size);

	*lppPropTagArray = lpPropTagArray;
	return hr;
}

HRESULT Utf8ToTString(LPCSTR lpszUtf8, ULONG ulFlags, LPVOID lpBase, convert_context *lpConverter, LPTSTR *lppszTString)
{
	if (lpszUtf8 == nullptr || lppszTString == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	std::string strDest = CONVERT_TO(lpConverter, std::string, ((ulFlags & MAPI_UNICODE) ? CHARSET_WCHAR : CHARSET_CHAR), lpszUtf8, rawsize(lpszUtf8), "UTF-8");
	size_t cbDest = strDest.length() + ((ulFlags & MAPI_UNICODE) ? sizeof(wchar_t) : sizeof(CHAR));
	auto hr = MAPIAllocateMore(cbDest, lpBase, reinterpret_cast<void **>(lppszTString));
	if (hr != hrSuccess)
		return hr;

	memset(*lppszTString, 0, cbDest);
	memcpy(*lppszTString, strDest.c_str(), strDest.length());
	return hrSuccess;
}

static HRESULT TStringToUtf8(struct soap *alloc, const TCHAR *lpszTstring,
    unsigned int ulFlags, convert_context *lpConverter, char **lppszUtf8)
{
	if (lpszTstring == nullptr || lppszUtf8 == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	std::string strDest;

	if (ulFlags & MAPI_UNICODE)
		strDest = CONVERT_TO(lpConverter, std::string, "UTF-8",
		          reinterpret_cast<const wchar_t *>(lpszTstring),
		          rawsize(reinterpret_cast<const wchar_t *>(lpszTstring)), CHARSET_WCHAR);
	else
		strDest = CONVERT_TO(lpConverter, std::string, "UTF-8",
		          reinterpret_cast<const char *>(lpszTstring),
		          rawsize(reinterpret_cast<const char *>(lpszTstring)), CHARSET_CHAR);

	*lppszUtf8 = soap_strdup(alloc, strDest.c_str());
	return *lppszUtf8 != nullptr ? hrSuccess : MAPI_E_NOT_ENOUGH_MEMORY;
}

HRESULT CopyABPropsFromSoap(const struct propmapPairArray *lpsoapPropmap,
    const struct propmapMVPairArray *lpsoapMVPropmap, SPROPMAP *lpPropmap,
    MVPROPMAP *lpMVPropmap, void *lpBase, ULONG ulFlags)
{
	convert_context converter;
	ULONG ulConvFlags;

	if (lpsoapPropmap != NULL) {
		lpPropmap->cEntries = lpsoapPropmap->__size;
		unsigned int nLen = sizeof(*lpPropmap->lpEntries) * lpPropmap->cEntries;
		auto hr = MAPIAllocateMore(nLen, lpBase, reinterpret_cast<void **>(&lpPropmap->lpEntries));
		if (hr != hrSuccess)
			return hr;

		for (gsoap_size_t i = 0; i < lpsoapPropmap->__size; ++i) {
			if (PROP_TYPE(lpsoapPropmap->__ptr[i].ulPropId) != PT_BINARY) {
				lpPropmap->lpEntries[i].ulPropId = CHANGE_PROP_TYPE(lpsoapPropmap->__ptr[i].ulPropId, ((ulFlags & MAPI_UNICODE) ? PT_UNICODE : PT_STRING8));
				ulConvFlags = ulFlags;
			} else {
				lpPropmap->lpEntries[i].ulPropId = lpsoapPropmap->__ptr[i].ulPropId;
				ulConvFlags = 0;
			}

			hr = Utf8ToTString(lpsoapPropmap->__ptr[i].lpszValue, ulConvFlags, lpBase, &converter, &lpPropmap->lpEntries[i].lpszValue);
			if (hr != hrSuccess)
				return hr;
		}
	}

	if (lpsoapMVPropmap != NULL) {
		lpMVPropmap->cEntries = lpsoapMVPropmap->__size;
		auto hr = MAPIAllocateMore(sizeof(*lpMVPropmap->lpEntries) * lpMVPropmap->cEntries,
		          lpBase, reinterpret_cast<void **>(&lpMVPropmap->lpEntries));
		if (hr != hrSuccess)
			return hr;

		for (gsoap_size_t i = 0; i < lpsoapMVPropmap->__size; ++i) {
			if (PROP_TYPE(lpsoapMVPropmap->__ptr[i].ulPropId) != PT_MV_BINARY) {
				lpMVPropmap->lpEntries[i].ulPropId = CHANGE_PROP_TYPE(lpsoapMVPropmap->__ptr[i].ulPropId, ((ulFlags & MAPI_UNICODE) ? PT_MV_UNICODE : PT_MV_STRING8));
				ulConvFlags = ulFlags;
			} else {
				lpMVPropmap->lpEntries[i].ulPropId = lpsoapMVPropmap->__ptr[i].ulPropId;
				ulConvFlags = 0;
			}

			lpMVPropmap->lpEntries[i].cValues = lpsoapMVPropmap->__ptr[i].sValues.__size;
			unsigned int nLen = sizeof(*lpMVPropmap->lpEntries[i].lpszValues) * lpMVPropmap->lpEntries[i].cValues;
			hr = MAPIAllocateMore(nLen, lpBase, reinterpret_cast<void **>(&lpMVPropmap->lpEntries[i].lpszValues));
			if (hr != hrSuccess)
				return hr;

			for (gsoap_size_t j = 0; j < lpsoapMVPropmap->__ptr[i].sValues.__size; ++j) {
				hr = Utf8ToTString(lpsoapMVPropmap->__ptr[i].sValues.__ptr[j], ulConvFlags, lpBase, &converter, &lpMVPropmap->lpEntries[i].lpszValues[j]);
				if (hr != hrSuccess)
					return hr;
			}
		}
	}
	return hrSuccess;
}

HRESULT CopyABPropsToSoap(struct soap *alloc, const SPROPMAP *lpPropmap,
    const MVPROPMAP *lpMVPropmap, ULONG ulFlags,
    struct propmapPairArray *&soapPropmap,
    struct propmapMVPairArray *&soapMVPropmap)
{
	convert_context	converter;
	ULONG ulConvFlags;

	if (lpPropmap && lpPropmap->cEntries) {
		soapPropmap = soap_new_propmapPairArray(alloc);
		if (soapPropmap == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		soapPropmap->__size = lpPropmap->cEntries;
		soapPropmap->__ptr = soap_new_propmapPair(alloc, lpPropmap->cEntries);
		if (soapPropmap->__ptr == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;

		for (gsoap_size_t i = 0; i < soapPropmap->__size; ++i) {
			if (PROP_TYPE(lpPropmap->lpEntries[i].ulPropId) != PT_BINARY) {
				soapPropmap->__ptr[i].ulPropId = CHANGE_PROP_TYPE(lpPropmap->lpEntries[i].ulPropId, PT_STRING8);
				ulConvFlags = ulFlags;
			} else {
				soapPropmap->__ptr[i].ulPropId = lpPropmap->lpEntries[i].ulPropId;
				ulConvFlags = 0;
			}
			auto hr = TStringToUtf8(alloc, lpPropmap->lpEntries[i].lpszValue,
			          ulConvFlags, &converter, &soapPropmap->__ptr[i].lpszValue);
			if (hr != hrSuccess)
				return hr;
		}
	}

	if (lpMVPropmap && lpMVPropmap->cEntries) {
		soapMVPropmap = soap_new_propmapMVPairArray(alloc);
		if (soapMVPropmap == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		soapMVPropmap->__size = lpMVPropmap->cEntries;
		soapMVPropmap->__ptr = soap_new_propmapMVPair(alloc, lpMVPropmap->cEntries);
		if (soapMVPropmap->__ptr == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;

		for (gsoap_size_t i = 0; i < soapMVPropmap->__size; ++i) {
			if (PROP_TYPE(lpMVPropmap->lpEntries[i].ulPropId) != PT_MV_BINARY) {
				soapMVPropmap->__ptr[i].ulPropId = CHANGE_PROP_TYPE(lpMVPropmap->lpEntries[i].ulPropId, PT_MV_STRING8);
				ulConvFlags = ulFlags;
			} else {
				soapMVPropmap->__ptr[i].ulPropId = lpMVPropmap->lpEntries[i].ulPropId;
				ulConvFlags = 0;
			}

			soapMVPropmap->__ptr[i].sValues.__size = lpMVPropmap->lpEntries[i].cValues;
			soapMVPropmap->__ptr[i].sValues.__ptr = soap_new_string(alloc, lpMVPropmap->lpEntries[i].cValues);
			if (soapMVPropmap->__ptr[i].sValues.__ptr == nullptr)
				return MAPI_E_NOT_ENOUGH_MEMORY;

			for (gsoap_size_t j = 0; j < soapMVPropmap->__ptr[i].sValues.__size; ++j) {
				auto hr = TStringToUtf8(alloc, lpMVPropmap->lpEntries[i].lpszValues[j],
				          ulConvFlags, &converter, &soapMVPropmap->__ptr[i].sValues.__ptr[j]);
				if (hr != hrSuccess)
					return hr;
			}
		}
	}

	return hrSuccess;
}

static HRESULT SoapUserToUser(const struct user *lpUser, ECUSER *lpsUser,
    ULONG ulFlags, void *lpBase, convert_context &converter)
{
	if (lpUser == NULL || lpsUser == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (lpBase == NULL)
		lpBase = lpsUser;

	memset(lpsUser, 0, sizeof(*lpsUser));
	auto hr = Utf8ToTString(lpUser->lpszUsername, ulFlags, lpBase, &converter, &lpsUser->lpszUsername);
	if (hr == hrSuccess && lpUser->lpszFullName != NULL)
		hr = Utf8ToTString(lpUser->lpszFullName, ulFlags, lpBase, &converter, &lpsUser->lpszFullName);

	if (hr == hrSuccess && lpUser->lpszMailAddress != NULL)
		hr = Utf8ToTString(lpUser->lpszMailAddress, ulFlags, lpBase, &converter, &lpsUser->lpszMailAddress);

	if (hr == hrSuccess && lpUser->lpszServername != NULL)
		hr = Utf8ToTString(lpUser->lpszServername, ulFlags, lpBase, &converter, &lpsUser->lpszServername);

	if (hr != hrSuccess)
		return hr;

	hr = CopyABPropsFromSoap(lpUser->lpsPropmap, lpUser->lpsMVPropmap,
							 &lpsUser->sPropmap, &lpsUser->sMVPropmap, lpBase, ulFlags);
	if (hr != hrSuccess)
		return hr;
	hr = CopySOAPEntryIdToMAPIEntryId(&lpUser->sUserId,
	     reinterpret_cast<unsigned int *>(&lpsUser->sUserId.cb),
	     reinterpret_cast<ENTRYID **>(&lpsUser->sUserId.lpb), lpBase);
	if (hr != hrSuccess)
		return hr;

	lpsUser->ulIsAdmin		= lpUser->ulIsAdmin;
	lpsUser->ulIsABHidden	= lpUser->ulIsABHidden;
	lpsUser->ulCapacity		= lpUser->ulCapacity;
	lpsUser->ulObjClass = static_cast<objectclass_t>(lpUser->ulObjClass);
	return hrSuccess;
}

HRESULT SoapUserArrayToUserArray(const struct userArray *lpUserArray,
    ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	if (lpUserArray == nullptr || lpcUsers == nullptr ||
	    lppsUsers == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	memory_ptr<ECUSER> lpECUsers;
	convert_context	converter;
	auto hr = MAPIAllocateBuffer(sizeof(ECUSER) * lpUserArray->__size, &~lpECUsers);
	if (hr != hrSuccess)
		return hr;
	memset(lpECUsers, 0, sizeof(ECUSER) * lpUserArray->__size);

	for (gsoap_size_t i = 0; i < lpUserArray->__size; ++i) {
		hr = SoapUserToUser(lpUserArray->__ptr + i, lpECUsers + i, ulFlags, lpECUsers, converter);
		if (hr != hrSuccess)
			return hr;
	}

	*lppsUsers = lpECUsers.release();
	*lpcUsers = lpUserArray->__size;
	return hrSuccess;
}

HRESULT SoapUserToUser(const struct user *lpUser, ULONG ulFlags,
    ECUSER **lppsUser)
{
	if (lpUser == nullptr || lppsUser == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	memory_ptr<ECUSER> lpsUser;
	convert_context	converter;
	auto hr = MAPIAllocateBuffer(sizeof *lpsUser, &~lpsUser);
	if (hr != hrSuccess)
		return hr;
	hr = SoapUserToUser(lpUser, lpsUser, ulFlags, NULL, converter);
	if (hr != hrSuccess)
		return hr;
	*lppsUser = lpsUser.release();
	return hrSuccess;
}

static HRESULT SoapGroupToGroup(const struct group *lpGroup,
    ECGROUP *lpsGroup, ULONG ulFlags, void *lpBase, convert_context &converter)
{
	if (lpGroup == NULL || lpsGroup == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (lpGroup->lpszGroupname == nullptr)
		return MAPI_E_INVALID_OBJECT;

	if (lpBase == NULL)
		lpBase = lpsGroup;

	memset(lpsGroup, 0, sizeof(*lpsGroup));

	auto hr = Utf8ToTString(lpGroup->lpszGroupname, ulFlags, lpBase, &converter, &lpsGroup->lpszGroupname);
	if (hr == hrSuccess && lpGroup->lpszFullname)
		hr = Utf8ToTString(lpGroup->lpszFullname, ulFlags, lpBase, &converter, &lpsGroup->lpszFullname);

	if (hr == hrSuccess && lpGroup->lpszFullEmail)
		hr = Utf8ToTString(lpGroup->lpszFullEmail, ulFlags, lpBase, &converter, &lpsGroup->lpszFullEmail);

	if (hr != hrSuccess)
		return hr;

	hr = CopyABPropsFromSoap(lpGroup->lpsPropmap, lpGroup->lpsMVPropmap,
							 &lpsGroup->sPropmap, &lpsGroup->sMVPropmap, lpBase, ulFlags);
	if (hr != hrSuccess)
		return hr;

	hr = CopySOAPEntryIdToMAPIEntryId(&lpGroup->sGroupId,
	     reinterpret_cast<ULONG *>(&lpsGroup->sGroupId.cb),
	     reinterpret_cast<ENTRYID **>(&lpsGroup->sGroupId.lpb), lpBase);
	if (hr != hrSuccess)
		return hr;

	lpsGroup->ulIsABHidden	= lpGroup->ulIsABHidden;
	return hrSuccess;
}

HRESULT SoapGroupArrayToGroupArray(const struct groupArray *lpGroupArray,
    ULONG ulFlags, ULONG *lpcGroups, ECGROUP **lppsGroups)
{
	if (lpGroupArray == nullptr || lpcGroups == nullptr ||
	    lppsGroups == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	memory_ptr<ECGROUP> lpECGroups;
	convert_context	converter;
	auto hr = MAPIAllocateBuffer(sizeof(ECGROUP) * lpGroupArray->__size, &~lpECGroups);
	if (hr != hrSuccess)
		return hr;
	memset(lpECGroups, 0, sizeof(ECGROUP) * lpGroupArray->__size);

	for (gsoap_size_t i = 0; i < lpGroupArray->__size; ++i) {
		hr = SoapGroupToGroup(lpGroupArray->__ptr + i, lpECGroups + i, ulFlags, lpECGroups, converter);
		if (hr != hrSuccess)
			return hr;
	}

	*lppsGroups = lpECGroups.release();
	*lpcGroups = lpGroupArray->__size;
	return hrSuccess;
}

HRESULT SoapGroupToGroup(const struct group *lpGroup, ULONG ulFlags,
    ECGROUP **lppsGroup)
{
	if (lpGroup == nullptr || lppsGroup == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	memory_ptr<ECGROUP> lpsGroup;
	convert_context	converter;
	auto hr = MAPIAllocateBuffer(sizeof(*lpsGroup), &~lpsGroup);
	if (hr != hrSuccess)
		return hr;
	hr = SoapGroupToGroup(lpGroup, lpsGroup, ulFlags, NULL, converter);
	if (hr != hrSuccess)
		return hr;
	*lppsGroup = lpsGroup.release();
	return hrSuccess;
}

static HRESULT SoapCompanyToCompany(const struct company *lpCompany,
    ECCOMPANY *lpsCompany, ULONG ulFlags, void *lpBase,
    convert_context &converter)
{
	if (lpCompany == NULL || lpsCompany == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if (lpBase == NULL)
		lpBase = lpsCompany;

	memset(lpsCompany, 0, sizeof(*lpsCompany));

	auto hr = Utf8ToTString(lpCompany->lpszCompanyname, ulFlags, lpBase, &converter, &lpsCompany->lpszCompanyname);
	if (hr == hrSuccess && lpCompany->lpszServername != NULL)
		hr = Utf8ToTString(lpCompany->lpszServername, ulFlags, lpBase, &converter, &lpsCompany->lpszServername);

	if (hr != hrSuccess)
		return hr;

	hr = CopyABPropsFromSoap(lpCompany->lpsPropmap, lpCompany->lpsMVPropmap,
							 &lpsCompany->sPropmap, &lpsCompany->sMVPropmap, lpBase, ulFlags);
	if (hr != hrSuccess)
		return hr;
	hr = CopySOAPEntryIdToMAPIEntryId(&lpCompany->sAdministrator,
	     reinterpret_cast<unsigned int *>(&lpsCompany->sAdministrator.cb),
	     reinterpret_cast<ENTRYID **>(&lpsCompany->sAdministrator.lpb), lpBase);
	if (hr != hrSuccess)
		return hr;
	hr = CopySOAPEntryIdToMAPIEntryId(&lpCompany->sCompanyId,
	     reinterpret_cast<unsigned int *>(&lpsCompany->sCompanyId.cb),
	     reinterpret_cast<ENTRYID **>(&lpsCompany->sCompanyId.lpb), lpBase);
	if (hr != hrSuccess)
		return hr;

	lpsCompany->ulIsABHidden	= lpCompany->ulIsABHidden;
	return hrSuccess;
}

HRESULT SoapCompanyArrayToCompanyArray(
    const struct companyArray *lpCompanyArray, ULONG ulFlags,
    ULONG *lpcCompanies, ECCOMPANY **lppsCompanies)
{
	if (lpCompanyArray == nullptr || lpcCompanies == nullptr ||
	    lppsCompanies == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	memory_ptr<ECCOMPANY> lpECCompanies;
	convert_context	converter;
	auto hr = MAPIAllocateBuffer(sizeof(ECCOMPANY) * lpCompanyArray->__size, &~lpECCompanies);
	if (hr != hrSuccess)
		return hr;
	memset(lpECCompanies, 0, sizeof(ECCOMPANY) * lpCompanyArray->__size);

	for (gsoap_size_t i = 0; i < lpCompanyArray->__size; ++i) {
		hr = SoapCompanyToCompany(&lpCompanyArray->__ptr[i], lpECCompanies + i, ulFlags, lpECCompanies, converter);
		if (hr != hrSuccess)
			return hr;
	}

	*lppsCompanies = lpECCompanies.release();
	*lpcCompanies = lpCompanyArray->__size;
	return hrSuccess;
}

HRESULT SoapCompanyToCompany(const struct company *lpCompany, ULONG ulFlags,
    ECCOMPANY **lppsCompany)
{
	if (lpCompany == nullptr || lppsCompany == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	memory_ptr<ECCOMPANY> lpsCompany;
	convert_context	converter;
	auto hr = MAPIAllocateBuffer(sizeof(*lpsCompany), &~lpsCompany);
	if (hr != hrSuccess)
		return hr;
	hr = SoapCompanyToCompany(lpCompany, lpsCompany, ulFlags, NULL, converter);
	if (hr != hrSuccess)
		return hr;
	*lppsCompany = lpsCompany.release();
	return hrSuccess;
}

HRESULT SvrNameListToSoapMvString8(struct soap *alloc,
    ECSVRNAMELIST *lpSvrNameList, unsigned int ulFlags,
    struct mv_string8 *&lpsSvrNameList)
{
	if (lpSvrNameList == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	convert_context		converter;
	lpsSvrNameList = soap_new_mv_string8(alloc);
	if (lpsSvrNameList == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;

	if (lpSvrNameList->cServers > 0) {
		lpsSvrNameList->__size = lpSvrNameList->cServers;
		lpsSvrNameList->__ptr = soap_new_string(alloc, lpSvrNameList->cServers);
		if (lpsSvrNameList->__ptr == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;

		for (unsigned i = 0; i < lpSvrNameList->cServers; ++i) {
			auto hr = TStringToUtf8(alloc, lpSvrNameList->lpszaServer[i],
			          ulFlags, &converter, &lpsSvrNameList->__ptr[i]);
			if (hr != hrSuccess)
				return hr;
		}
	}

	return hrSuccess;
}

HRESULT SoapServerListToServerList(const struct serverList *lpsServerList,
    ULONG ulFLags, ECSERVERLIST **lppServerList)
{
	if (lpsServerList == nullptr || lppServerList == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	memory_ptr<ECSERVERLIST> lpServerList;
	convert_context	converter;
	auto hr = MAPIAllocateBuffer(sizeof(*lpServerList), &~lpServerList);
	if (hr != hrSuccess)
		return hr;
	memset(lpServerList, 0, sizeof *lpServerList);
	if (lpsServerList->__size == 0 || lpsServerList->__ptr == nullptr) {
		*lppServerList = lpServerList.release();
		return hrSuccess;
	}
	lpServerList->cServers = lpsServerList->__size;
	hr = MAPIAllocateMore(lpsServerList->__size * sizeof(*lpServerList->lpsaServer), lpServerList, reinterpret_cast<void **>(&lpServerList->lpsaServer));
	if (hr != hrSuccess)
		return hr;
	memset(lpServerList->lpsaServer, 0, lpsServerList->__size * sizeof *lpServerList->lpsaServer);

	for (gsoap_size_t i = 0; i < lpsServerList->__size; ++i) {
		// Flags
		lpServerList->lpsaServer[i].ulFlags = lpsServerList->__ptr[i].ulFlags;

		// Name
		if (lpsServerList->__ptr[i].lpszName != NULL) {
			hr = Utf8ToTString(lpsServerList->__ptr[i].lpszName, ulFLags, lpServerList, &converter, &lpServerList->lpsaServer[i].lpszName);
			if (hr != hrSuccess)
				return hr;
		}

		// FilePath
		if (lpsServerList->__ptr[i].lpszFilePath != NULL) {
			hr = Utf8ToTString(lpsServerList->__ptr[i].lpszFilePath, ulFLags, lpServerList, &converter, &lpServerList->lpsaServer[i].lpszFilePath);
			if (hr != hrSuccess)
				return hr;
		}

		// HttpPath
		if (lpsServerList->__ptr[i].lpszHttpPath != NULL) {
			hr = Utf8ToTString(lpsServerList->__ptr[i].lpszHttpPath, ulFLags, lpServerList, &converter, &lpServerList->lpsaServer[i].lpszHttpPath);
			if (hr != hrSuccess)
				return hr;
		}

		// SslPath
		if (lpsServerList->__ptr[i].lpszSslPath != NULL) {
			hr = Utf8ToTString(lpsServerList->__ptr[i].lpszSslPath, ulFLags, lpServerList, &converter, &lpServerList->lpsaServer[i].lpszSslPath);
			if (hr != hrSuccess)
				return hr;
		}

		// PreferedPath
		if (lpsServerList->__ptr[i].lpszPreferedPath != NULL) {
			hr = Utf8ToTString(lpsServerList->__ptr[i].lpszPreferedPath, ulFLags, lpServerList, &converter, &lpServerList->lpsaServer[i].lpszPreferedPath);
			if (hr != hrSuccess)
				return hr;
		}
	}
	*lppServerList = lpServerList.release();
	return hrSuccess;
}

// Wrap the server store entryid to client store entry. (Add a servername)
HRESULT WrapServerClientStoreEntry(const char *lpszServerName,
    const entryId *lpsStoreId, ULONG *lpcbStoreID, ENTRYID **lppStoreID)
{
	if (lpsStoreId == nullptr || lpszServerName == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (lpsStoreId->__size < 4) {
		ec_log_crit("Assertion lpsStoreId->__size >= 4 failed");
		return MAPI_E_INVALID_PARAMETER;
	}

	LPENTRYID	lpStoreID = NULL;
	// The new entryid size is, current size + servername size + 1 byte term 0 - 4 bytes padding
	unsigned int ulSize = lpsStoreId->__size+strlen(lpszServerName)+1-4;
	auto hr = MAPIAllocateBuffer(ulSize, reinterpret_cast<void **>(&lpStoreID));
	if(hr != hrSuccess)
		return hr;

	memset(lpStoreID, 0, ulSize );

	//Copy the entryid without servername
	memcpy(lpStoreID, lpsStoreId->__ptr, lpsStoreId->__size);

	// Add the server name
	strcpy(reinterpret_cast<char *>(lpStoreID) + lpsStoreId->__size - 4, lpszServerName);
	*lpcbStoreID = ulSize;
	*lppStoreID = lpStoreID;
	return hrSuccess;
}

// Un wrap the client store entryid to server store entry. (remove a servername)
HRESULT UnWrapServerClientStoreEntry(ULONG cbWrapStoreID,
    const ENTRYID *lpWrapStoreID, ULONG *lpcbUnWrapStoreID,
    ENTRYID **lppUnWrapStoreID)
{
	if (lpWrapStoreID == nullptr || lppUnWrapStoreID == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	LPENTRYID lpUnWrapStoreID = NULL;
	ULONG	ulSize = 0;
	auto peid = reinterpret_cast<const EID *>(lpWrapStoreID);
	if (peid->ulVersion == 0)
		ulSize = SIZEOF_EID_V0_FIXED;
	else if (peid->ulVersion == 1)
		ulSize = sizeof(EID_FIXED);
	else
		return MAPI_E_INVALID_ENTRYID;

	if (cbWrapStoreID < ulSize)
		return MAPI_E_INVALID_ENTRYID;

	auto hr = MAPIAllocateBuffer(ulSize, reinterpret_cast<void **>(&lpUnWrapStoreID));
	if(hr != hrSuccess)
		return hr;

	memset(lpUnWrapStoreID, 0, ulSize);

	// Remove servername
	memcpy(lpUnWrapStoreID, lpWrapStoreID, ulSize-4);

	*lppUnWrapStoreID = lpUnWrapStoreID;
	*lpcbUnWrapStoreID = ulSize;
	return hrSuccess;
}

HRESULT UnWrapServerClientABEntry(ULONG cbWrapABID, const ENTRYID *lpWrapABID,
    ULONG *lpcbUnWrapABID, ENTRYID **lppUnWrapABID)
{
	if (lpWrapABID == nullptr || lppUnWrapABID == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto ret = KAllocCopy(lpWrapABID, cbWrapABID, reinterpret_cast<void **>(lppUnWrapABID));
	if (ret != hrSuccess)
		return ret;
	*lpcbUnWrapABID = cbWrapABID;
	return hrSuccess;
}

HRESULT CopySOAPNotificationToMAPINotification(void *lpProvider,
    const struct notification *lpSrc, NOTIFICATION **lppDst,
    convert_context *lpConverter)
{
	memory_ptr<NOTIFICATION> lpNotification;
	auto hr = MAPIAllocateBuffer(sizeof(NOTIFICATION), &~lpNotification);
	if (hr != hrSuccess)
		return hr;
	memset(lpNotification, 0, sizeof(NOTIFICATION));

	lpNotification->ulEventType = lpSrc->ulEventType;

	switch(lpSrc->ulEventType){
	case fnevCriticalError:// ERROR_NOTIFICATION
		hr = MAPI_E_INVALID_PARAMETER;
		break;
	case fnevNewMail: { // NEWMAIL_NOTIFICATION
		auto &dst = lpNotification->info.newmail;
		if (lpSrc->newmail->pEntryId != nullptr)
			// Ignore error now
			// FIXME: This must exist, so maybe give an error or skip them
			CopySOAPEntryIdToMAPIEntryId(lpSrc->newmail->pEntryId, &dst.cbEntryID, &dst.lpEntryID, lpNotification);
		if (lpSrc->newmail->pParentId != nullptr)
			// Ignore error
			CopySOAPEntryIdToMAPIEntryId(lpSrc->newmail->pParentId, &dst.cbParentID, &dst.lpParentID, lpNotification);
		if(lpSrc->newmail->lpszMessageClass != NULL) {
			int nLen = strlen(lpSrc->newmail->lpszMessageClass)+1;
			hr = MAPIAllocateMore(nLen, lpNotification, reinterpret_cast<void **>(&dst.lpszMessageClass));
			if (hr != hrSuccess)
				break;
			memcpy(dst.lpszMessageClass, lpSrc->newmail->lpszMessageClass, nLen);
		}
		dst.ulFlags = 0;
		dst.ulMessageFlags = lpSrc->newmail->ulMessageFlags;
		break;
	}
	case fnevObjectCreated:// OBJECT_NOTIFICATION
	case fnevObjectDeleted:
	case fnevObjectModified:
	case fnevObjectCopied:
	case fnevObjectMoved:
	case fnevSearchComplete: {
		auto &dst = lpNotification->info.obj;
		// FIXME for each if statement below, check the ELSE .. we can't send a TABLE_ROW_ADDED without lpProps for example ..
		dst.ulObjType = lpSrc->obj->ulObjType;

		// All errors of CopySOAPEntryIdToMAPIEntryId are ignored
		if (lpSrc->obj->pEntryId != NULL)
			CopySOAPEntryIdToMAPIEntryId(lpSrc->obj->pEntryId, &dst.cbEntryID, &dst.lpEntryID, lpNotification);
		if (lpSrc->obj->pParentId != NULL)
			CopySOAPEntryIdToMAPIEntryId(lpSrc->obj->pParentId, &dst.cbParentID, &dst.lpParentID, lpNotification);
		if (lpSrc->obj->pOldId != NULL)
			CopySOAPEntryIdToMAPIEntryId(lpSrc->obj->pOldId, &dst.cbOldID, &dst.lpOldID, lpNotification);
		if (lpSrc->obj->pOldParentId != NULL)
			CopySOAPEntryIdToMAPIEntryId(lpSrc->obj->pOldParentId, &dst.cbOldParentID, &dst.lpOldParentID, lpNotification);
		if (lpSrc->obj->pPropTagArray != nullptr)
			// ignore errors
			CopySOAPPropTagArrayToMAPIPropTagArray(lpSrc->obj->pPropTagArray, &dst.lpPropTagArray, lpNotification);
		break;
	}
	case fnevTableModified: { // TABLE_NOTIFICATION
		auto &dst = lpNotification->info.tab;
		dst.ulTableEvent = lpSrc->tab->ulTableEvent;
		dst.propIndex.ulPropTag = lpSrc->tab->propIndex.ulPropTag;

		if (lpSrc->tab->propIndex.__union == SOAP_UNION_propValData_bin &&
		    lpSrc->tab->propIndex.Value.bin != nullptr) {
			auto &bin = dst.propIndex.Value.bin;
			bin.cb = lpSrc->tab->propIndex.Value.bin->__size;
			hr = MAPIAllocateMore(bin.cb, lpNotification,
			     reinterpret_cast<void **>(&bin.lpb));
			if (hr != hrSuccess)
				break;
			memcpy(bin.lpb, lpSrc->tab->propIndex.Value.bin->__ptr, lpSrc->tab->propIndex.Value.bin->__size);
		}

		dst.propPrior.ulPropTag = lpSrc->tab->propPrior.ulPropTag;

		if (lpSrc->tab->propPrior.__union == SOAP_UNION_propValData_bin &&
		    lpSrc->tab->propPrior.Value.bin != nullptr) {
			auto &bin = dst.propPrior.Value.bin;
			bin.cb = lpSrc->tab->propPrior.Value.bin->__size;
			hr = MAPIAllocateMore(bin.cb, lpNotification,
			     reinterpret_cast<void **>(&bin.lpb));
			if (hr != hrSuccess)
				break;
			memcpy(bin.lpb, lpSrc->tab->propPrior.Value.bin->__ptr, lpSrc->tab->propPrior.Value.bin->__size);
		}

		if (lpSrc->tab->pRow == nullptr)
			break;
		dst.row.cValues = lpSrc->tab->pRow->__size;
		hr = MAPIAllocateMore(sizeof(SPropValue) * dst.row.cValues, lpNotification,
		     reinterpret_cast<void **>(&dst.row.lpProps));
		if (hr != hrSuccess)
			break;
		CopySOAPRowToMAPIRow(lpProvider, lpSrc->tab->pRow, dst.row.lpProps,
			reinterpret_cast<void **>(lpNotification.get()),
			lpSrc->tab->ulObjType, lpConverter);
		break;
	}
	case fnevStatusObjectModified: // STATUS_OBJECT_NOTIFICATION
		hr = MAPI_E_INVALID_PARAMETER;
		break;
	case fnevExtended: // EXTENDED_NOTIFICATION
		hr = MAPI_E_INVALID_PARAMETER;
		break;
	default:
		hr = MAPI_E_INVALID_PARAMETER;
		break;
	}

	if(hr != hrSuccess)
		return hr;

	*lppDst = lpNotification.release();
	return hrSuccess;
}

HRESULT CopySOAPChangeNotificationToSyncState(const struct notification *lpSrc,
    SBinary **lppDst, void *lpBase)
{
	if (lpSrc->ulEventType != fnevKopanoIcsChange)
		return MAPI_E_INVALID_PARAMETER;

	SBinary *lpSBinary = nullptr;
	auto hr = MAPIAllocateMore(sizeof(*lpSBinary), lpBase, reinterpret_cast<void **>(&lpSBinary));
	if (hr != hrSuccess)
		return hr;
	memset(lpSBinary, 0, sizeof *lpSBinary);

	lpSBinary->cb = lpSrc->ics->pSyncState->__size;
	hr = MAPIAllocateMore(lpSBinary->cb, lpBase != nullptr ? lpBase : lpSBinary, reinterpret_cast<void **>(&lpSBinary->lpb));
	if (hr != hrSuccess) {
		MAPIFreeBuffer(lpSBinary);
		return hr;
	}

	memcpy(lpSBinary->lpb, lpSrc->ics->pSyncState->__ptr, lpSBinary->cb);

	*lppDst = lpSBinary;
	return hr;
}

static HRESULT CopyMAPISourceKeyToSoapSourceKey(const SBinary *lpsMAPISourceKey,
    struct xsd__base64Binary *lpsSoapSourceKey)
{
	if (lpsMAPISourceKey == nullptr || lpsSoapSourceKey == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	lpsSoapSourceKey->__ptr  = soap_new_unsignedByte(nullptr, lpsSoapSourceKey->__size);
	lpsSoapSourceKey->__size = lpsMAPISourceKey->cb;
	memcpy(lpsSoapSourceKey->__ptr, lpsMAPISourceKey->lpb, lpsSoapSourceKey->__size);
	return hrSuccess;
}

HRESULT CopyICSChangeToSOAPSourceKeys(ULONG cbChanges,
    const ICSCHANGE *lpsChanges, sourceKeyPairArray **lppsSKPA)
{
	if (lpsChanges == nullptr || lppsSKPA == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	auto lpsSKPA = *lppsSKPA = soap_new_sourceKeyPairArray(nullptr);
	if (cbChanges > 0) {
		lpsSKPA->__size = cbChanges;
		lpsSKPA->__ptr  = soap_new_sourceKeyPair(nullptr, cbChanges);

		for (unsigned i = 0; i < cbChanges; ++i) {
			auto hr = CopyMAPISourceKeyToSoapSourceKey(&lpsChanges[i].sSourceKey, &lpsSKPA->__ptr[i].sObjectKey);
			if (hr != hrSuccess)
				return hr;
			hr = CopyMAPISourceKeyToSoapSourceKey(&lpsChanges[i].sParentSourceKey, &lpsSKPA->__ptr[i].sParentKey);
			if (hr != hrSuccess)
				return hr;
		}
	}
	return hrSuccess;
}

static HRESULT ConvertString8ToUnicode(const char *lpszA, wchar_t **lppszW,
    void *base, convert_context &converter)
{
	if (lpszA == nullptr || lppszW == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	std::wstring wide;
	wchar_t *lpszW = nullptr;
	TryConvert(lpszA, wide);
	auto hr = MAPIAllocateMore((wide.length() + 1) * sizeof(std::wstring::value_type),
	          base, reinterpret_cast<void **>(&lpszW));
	if (hr != hrSuccess)
		return hr;
	wcscpy(lpszW, wide.c_str());
	*lppszW = lpszW;
	return hrSuccess;
}

static HRESULT ConvertString8ToUnicode(LPSRestriction lpRestriction,
    void *base, convert_context &converter)
{
	if (lpRestriction == NULL)
		return hrSuccess;

	switch (lpRestriction->rt) {
	case RES_OR:
		for (unsigned int i = 0; i < lpRestriction->res.resOr.cRes; ++i) {
			auto hr = ConvertString8ToUnicode(&lpRestriction->res.resOr.lpRes[i], base, converter);
			if (hr != hrSuccess)
				return hr;
		}
		break;
	case RES_AND:
		for (unsigned int i = 0; i < lpRestriction->res.resAnd.cRes; ++i) {
			auto hr = ConvertString8ToUnicode(&lpRestriction->res.resAnd.lpRes[i], base, converter);
			if (hr != hrSuccess)
				return hr;
		}
		break;
	case RES_NOT: {
		auto hr = ConvertString8ToUnicode(lpRestriction->res.resNot.lpRes, base, converter);
		if (hr != hrSuccess)
			return hr;
		break;
	}
	case RES_COMMENT:
		if (lpRestriction->res.resComment.lpRes) {
			auto hr = ConvertString8ToUnicode(lpRestriction->res.resComment.lpRes, base, converter);
			if (hr != hrSuccess)
				return hr;
		}
		for (unsigned int i = 0; i < lpRestriction->res.resComment.cValues; ++i) {
			if (PROP_TYPE(lpRestriction->res.resComment.lpProp[i].ulPropTag) != PT_STRING8)
				continue;
			auto hr = ConvertString8ToUnicode(lpRestriction->res.resComment.lpProp[i].Value.lpszA, &lpRestriction->res.resComment.lpProp[i].Value.lpszW, base, converter);
			if (hr != hrSuccess)
				return hr;
			lpRestriction->res.resComment.lpProp[i].ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resComment.lpProp[i].ulPropTag, PT_UNICODE);
		}
		break;
	case RES_COMPAREPROPS:
		break;
	case RES_CONTENT: {
		if (PROP_TYPE(lpRestriction->res.resContent.ulPropTag) != PT_STRING8)
			break;
		auto hr = ConvertString8ToUnicode(lpRestriction->res.resContent.lpProp->Value.lpszA, &lpRestriction->res.resContent.lpProp->Value.lpszW, base, converter);
		if (hr != hrSuccess)
			return hr;
		lpRestriction->res.resContent.lpProp->ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resContent.lpProp->ulPropTag, PT_UNICODE);
		lpRestriction->res.resContent.ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resContent.ulPropTag, PT_UNICODE);
		break;
	}
	case RES_PROPERTY: {
		if (PROP_TYPE(lpRestriction->res.resProperty.ulPropTag) != PT_STRING8)
			break;
		auto hr = ConvertString8ToUnicode(lpRestriction->res.resProperty.lpProp->Value.lpszA, &lpRestriction->res.resProperty.lpProp->Value.lpszW, base, converter);
		if (hr != hrSuccess)
			return hr;
		lpRestriction->res.resProperty.lpProp->ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resProperty.lpProp->ulPropTag, PT_UNICODE);
		lpRestriction->res.resProperty.ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resProperty.ulPropTag, PT_UNICODE);
		break;
	}
	case RES_SUBRESTRICTION: {
		auto hr = ConvertString8ToUnicode(lpRestriction->res.resSub.lpRes, base, converter);
		if (hr != hrSuccess)
			return hr;
		break;
	}
	};
	return hrSuccess;
}

static HRESULT ConvertString8ToUnicode(ADRLIST *lpAdrList, void *base,
    convert_context &converter)
{
	if (lpAdrList == NULL)
		return hrSuccess;

	for (ULONG c = 0; c < lpAdrList->cEntries; ++c) {
		// treat as row
		auto hr = ConvertString8ToUnicode(reinterpret_cast<SRow *>(&lpAdrList->aEntries[c]), base, converter);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

static HRESULT ConvertString8ToUnicode(const ACTIONS *lpActions, void *base,
    convert_context &converter)
{
	if (lpActions == NULL)
		return hrSuccess;

	for (ULONG c = 0; c < lpActions->cActions; ++c) {
		if (lpActions->lpAction[c].acttype != OP_FORWARD &&
		    lpActions->lpAction[c].acttype != OP_DELEGATE)
			continue;
		auto hr = ConvertString8ToUnicode(lpActions->lpAction[c].lpadrlist, base, converter);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

HRESULT ConvertString8ToUnicode(LPSRow lpRow, void *base, convert_context &converter)
{
	if (lpRow == NULL)
		return hrSuccess;

	for (ULONG c = 0; c < lpRow->cValues; ++c) {
		HRESULT hr = hrSuccess;
		if (PROP_TYPE(lpRow->lpProps[c].ulPropTag) == PT_SRESTRICTION) {
			hr = ConvertString8ToUnicode(reinterpret_cast<SRestriction *>(lpRow->lpProps[c].Value.lpszA),
			     base != nullptr ? base : lpRow->lpProps, converter);
		} else if (PROP_TYPE(lpRow->lpProps[c].ulPropTag) == PT_ACTIONS) {
			hr = ConvertString8ToUnicode(reinterpret_cast<ACTIONS *>(lpRow->lpProps[c].Value.lpszA),
			     base != nullptr ? base : lpRow->lpProps, converter);
		} else if (base && PROP_TYPE(lpRow->lpProps[c].ulPropTag) == PT_STRING8) {
			// only for "base" items: e.g. the lpadrlist data, not the PR_RULE_NAME from the top-level
			hr = ConvertString8ToUnicode(lpRow->lpProps[c].Value.lpszA, &lpRow->lpProps[c].Value.lpszW, base, converter);
			if (hr != hrSuccess)
				return hr;
			lpRow->lpProps[c].ulPropTag = CHANGE_PROP_TYPE(lpRow->lpProps[c].ulPropTag, PT_UNICODE);
		}
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

/**
 * Converts PT_STRING8 to PT_UNICODE inside PT_SRESTRICTION and
 * PT_ACTION properties inside the rows
 *
 * @param[in,out] lpRowSet Rowset to modify
 *
 * @return MAPI Error code
 */
HRESULT ConvertString8ToUnicode(LPSRowSet lpRowSet)
{
	convert_context converter;

	if (lpRowSet == NULL)
		return hrSuccess;

	for (ULONG c = 0; c < lpRowSet->cRows; ++c) {
		auto hr = ConvertString8ToUnicode(&lpRowSet->aRow[c], NULL, converter);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

HRESULT convert_wsfolder_to_soapfolder(const std::vector<WSMAPIFolderOps::WSFolder> &source,
    std::vector<new_folder> &destination)
{
	const auto count = source.size();
	destination.resize(count);
	for (unsigned int i = 0; i < count; ++i) {
		const auto &src = source[i];
		auto &dst       = destination[i];

		dst.type           = src.folder_type;
		dst.name           = src.name.z_str();
		dst.comment        = src.comment.z_str();
		dst.open_if_exists = src.open_if_exists;
		dst.sync_id        = src.sync_id;

		if (src.m_lpNewEntryId != nullptr) {
			auto ret = CopyMAPIEntryIdToSOAPEntryId(src.m_cbNewEntryId, src.m_lpNewEntryId, &dst.entryid);
			if (ret != hrSuccess)
				return ret;
		}
		if (src.sourcekey != nullptr) {
			dst.original_sourcekey.__ptr  = src.sourcekey->lpb;
			dst.original_sourcekey.__size = src.sourcekey->cb;
		} else {
			dst.original_sourcekey.__ptr  = nullptr;
			dst.original_sourcekey.__size = 0;
		}
	}
	return hrSuccess;
}

HRESULT convert_soapfolders_to_wsfolder(const struct create_folders_response &source,
    std::vector<WSMAPIFolderOps::WSFolder> &destination)
{
	const auto count = source.entryids->__size;
	destination.resize(count);
	for (unsigned int i = 0; i < count; ++i) {
		const auto &dst = destination[i];
		auto ret = CopySOAPEntryIdToMAPIEntryId(&source.entryids->__ptr[i],
		           dst.m_lpcbEntryId, dst.m_lppEntryId);
		if (ret != hrSuccess)
			return ret;
	}
	return hrSuccess;
}
