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
#include <string>
#include <kopano/platform.h>
#include <sys/un.h>
#include "WSUtil.h"
#include <kopano/ECIConv.h>
#include <kopano/ECGuid.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include "Mem.h"

#include <kopano/mapiext.h>

// For the static row getprop functions
#include "ECMAPIProp.h"
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

#define CONVERT_TO(_context, _charset, ...) ((_context) ? (_context)->convert_to<_charset>(__VA_ARGS__) : convert_to<_charset>(__VA_ARGS__))

HRESULT CopyMAPIPropValToSOAPPropVal(propVal *dp, const SPropValue *sp,
    convert_context *lpConverter)
{
	HRESULT hr = hrSuccess;

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
		dp->Value.hilo = s_alloc<hiloLong>(nullptr);
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
		dp->Value.b = sp->Value.b == 0 ? false : true;
		break;
	case PT_OBJECT:
		// can never be transmitted over the wire!
		hr = MAPI_E_INVALID_TYPE;
		break;
	case PT_I8:
		dp->__union = SOAP_UNION_propValData_li;
		dp->Value.li = sp->Value.li.QuadPart;
		break;
	case PT_STRING8: {
		utf8string u8 = CONVERT_TO(lpConverter, utf8string, sp->Value.lpszA);	// SOAP lpszA = UTF-8, MAPI lpszA = current locale charset
		dp->__union = SOAP_UNION_propValData_lpszA;
		dp->Value.lpszA = s_alloc<char>(nullptr, u8.size() + 1);
		strcpy(dp->Value.lpszA, u8.c_str());
		break;
	}
	case PT_UNICODE: {
		utf8string u8 = CONVERT_TO(lpConverter, utf8string, sp->Value.lpszW);
		dp->__union = SOAP_UNION_propValData_lpszA;
		dp->Value.lpszA = s_alloc<char>(nullptr, u8.size() + 1);
		strcpy(dp->Value.lpszA, u8.c_str());
		break;
	}
	case PT_SYSTIME:
		dp->__union = SOAP_UNION_propValData_hilo;
		dp->Value.hilo = s_alloc<hiloLong>(nullptr);
		dp->Value.hilo->hi = sp->Value.ft.dwHighDateTime;
		dp->Value.hilo->lo = sp->Value.ft.dwLowDateTime;
		break;
	case PT_CLSID:
		dp->__union = SOAP_UNION_propValData_bin;
		dp->Value.bin = s_alloc<xsd__base64Binary>(nullptr);
		dp->Value.bin->__ptr = s_alloc<unsigned char>(nullptr, sizeof(GUID));
		dp->Value.bin->__size = sizeof(GUID);
		memcpy(dp->Value.bin->__ptr, sp->Value.lpguid, sizeof(GUID));
		break;
	case PT_BINARY:
		dp->__union = SOAP_UNION_propValData_bin;
		dp->Value.bin = s_alloc<xsd__base64Binary>(nullptr);
		if (sp->Value.bin.cb == 0 || sp->Value.bin.lpb == nullptr) {
			dp->Value.bin->__ptr = nullptr;
			dp->Value.bin->__size = 0;
			break;
		}
		dp->Value.bin->__ptr = s_alloc<unsigned char>(nullptr, sp->Value.bin.cb);
		dp->Value.bin->__size = sp->Value.bin.cb;
		memcpy(dp->Value.bin->__ptr, sp->Value.bin.lpb, sp->Value.bin.cb);
		break;
	case PT_MV_I2:
		dp->__union = SOAP_UNION_propValData_mvi;
		dp->Value.mvi.__size = sp->Value.MVi.cValues;
		dp->Value.mvi.__ptr = s_alloc<short int>(nullptr, dp->Value.mvi.__size);
		memcpy(dp->Value.mvi.__ptr, sp->Value.MVi.lpi, sizeof(short int) * dp->Value.mvi.__size);
		break;
	case PT_MV_LONG:
		dp->__union = SOAP_UNION_propValData_mvl;
		dp->Value.mvl.__size = sp->Value.MVl.cValues;
		dp->Value.mvl.__ptr = s_alloc<unsigned int>(nullptr, dp->Value.mvl.__size);
		memcpy(dp->Value.mvl.__ptr, sp->Value.MVl.lpl, sizeof(unsigned int) * dp->Value.mvl.__size);
		break;
	case PT_MV_R4:
		dp->__union = SOAP_UNION_propValData_mvflt;
		dp->Value.mvflt.__size = sp->Value.MVflt.cValues;
		dp->Value.mvflt.__ptr = s_alloc<float>(nullptr, dp->Value.mvflt.__size);
		memcpy(dp->Value.mvflt.__ptr, sp->Value.MVflt.lpflt, sizeof(float) * dp->Value.mvflt.__size);
		break;
	case PT_MV_DOUBLE:
		dp->__union = SOAP_UNION_propValData_mvdbl;
		dp->Value.mvdbl.__size = sp->Value.MVdbl.cValues;
		dp->Value.mvdbl.__ptr = s_alloc<double>(nullptr, dp->Value.mvdbl.__size);
		memcpy(dp->Value.mvdbl.__ptr, sp->Value.MVdbl.lpdbl, sizeof(double) * dp->Value.mvdbl.__size);
		break;
	case PT_MV_CURRENCY:
		dp->__union = SOAP_UNION_propValData_mvhilo;
		dp->Value.mvhilo.__size = sp->Value.MVcur.cValues;
		dp->Value.mvhilo.__ptr = s_alloc<hiloLong>(nullptr, dp->Value.mvhilo.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvhilo.__size; ++i) {
			dp->Value.mvhilo.__ptr[i].hi = sp->Value.MVcur.lpcur[i].Hi;
			dp->Value.mvhilo.__ptr[i].lo = sp->Value.MVcur.lpcur[i].Lo;
		}
		break;
	case PT_MV_APPTIME:
		dp->__union = SOAP_UNION_propValData_mvdbl;
		dp->Value.mvdbl.__size = sp->Value.MVat.cValues;
		dp->Value.mvdbl.__ptr = s_alloc<double>(nullptr, dp->Value.mvdbl.__size);
		memcpy(dp->Value.mvdbl.__ptr, sp->Value.MVat.lpat, sizeof(double) * dp->Value.mvdbl.__size);
		break;
	case PT_MV_SYSTIME:
		dp->__union = SOAP_UNION_propValData_mvhilo;
		dp->Value.mvhilo.__size = sp->Value.MVft.cValues;
		dp->Value.mvhilo.__ptr = s_alloc<hiloLong>(nullptr, dp->Value.mvhilo.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvhilo.__size; ++i) {
			dp->Value.mvhilo.__ptr[i].hi = sp->Value.MVft.lpft[i].dwHighDateTime;
			dp->Value.mvhilo.__ptr[i].lo = sp->Value.MVft.lpft[i].dwLowDateTime;
		}
		break;
	case PT_MV_BINARY:
		dp->__union = SOAP_UNION_propValData_mvbin;
		dp->Value.mvbin.__size = sp->Value.MVbin.cValues;
		dp->Value.mvbin.__ptr = s_alloc<xsd__base64Binary>(nullptr, dp->Value.mvbin.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvbin.__size; ++i) {
			if (sp->Value.MVbin.lpbin[i].cb == 0 ||
			    sp->Value.MVbin.lpbin[i].lpb == nullptr) {
				dp->Value.mvbin.__ptr[i].__size = 0;
				dp->Value.mvbin.__ptr[i].__ptr = nullptr;
				continue;
			}
			dp->Value.mvbin.__ptr[i].__size = sp->Value.MVbin.lpbin[i].cb;
			dp->Value.mvbin.__ptr[i].__ptr = s_alloc<unsigned char>(nullptr, dp->Value.mvbin.__ptr[i].__size);
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
		dp->Value.mvszA.__ptr = s_alloc<char *>(nullptr, dp->Value.mvszA.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvszA.__size; ++i) {
			utf8string u8 = lpConverter->convert_to<utf8string>(sp->Value.MVszA.lppszA[i]);
			dp->Value.mvszA.__ptr[i] = s_alloc<char>(nullptr, u8.size() + 1);
			strcpy(dp->Value.mvszA.__ptr[i], u8.c_str());
		}
		break;
	case PT_MV_UNICODE:
		if (lpConverter == NULL) {
			convert_context converter;
			CopyMAPIPropValToSOAPPropVal(dp, sp, &converter);
			break;
		}
		dp->__union = SOAP_UNION_propValData_mvszA;
		dp->Value.mvszA.__size = sp->Value.MVszA.cValues;
		dp->Value.mvszA.__ptr = s_alloc<char *>(nullptr, dp->Value.mvszA.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvszA.__size; ++i) {
			utf8string u8 = lpConverter->convert_to<utf8string>(sp->Value.MVszW.lppszW[i]);
			dp->Value.mvszA.__ptr[i] = s_alloc<char>(nullptr, u8.size() + 1);
			strcpy(dp->Value.mvszA.__ptr[i], u8.c_str());
		}
		break;
	case PT_MV_CLSID:
		dp->__union = SOAP_UNION_propValData_mvbin;
		dp->Value.mvbin.__size = sp->Value.MVguid.cValues;
		dp->Value.mvbin.__ptr = s_alloc<xsd__base64Binary>(nullptr, dp->Value.mvbin.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvbin.__size; ++i) {
			dp->Value.mvbin.__ptr[i].__size = sizeof(GUID);
			dp->Value.mvbin.__ptr[i].__ptr = s_alloc<unsigned char>(nullptr, dp->Value.mvbin.__ptr[i].__size);
			memcpy(dp->Value.mvbin.__ptr[i].__ptr, &sp->Value.MVguid.lpguid[i], dp->Value.mvbin.__ptr[i].__size);
		}
		break;
	case PT_MV_I8:
		dp->__union = SOAP_UNION_propValData_mvli;
		dp->Value.mvli.__size = sp->Value.MVli.cValues;
		dp->Value.mvli.__ptr = s_alloc<LONG64>(nullptr, dp->Value.mvli.__size);
		for (gsoap_size_t i = 0; i < dp->Value.mvli.__size; ++i)
			dp->Value.mvli.__ptr[i] = sp->Value.MVli.lpli[i].QuadPart;
		break;
	case PT_SRESTRICTION:
		dp->__union = SOAP_UNION_propValData_res;
		// NOTE: we placed the object pointer in lpszA to make sure it is on the same offset as Value.x on 32-bit and 64-bit machines
		hr = CopyMAPIRestrictionToSOAPRestriction(&dp->Value.res, (LPSRestriction)sp->Value.lpszA, lpConverter);
		break;
	case PT_ACTIONS: {
		// NOTE: we placed the object pointer in lpszA to make sure it is on the same offset as Value.x on 32-bit and 64-bit machines
		auto lpSrcActions = reinterpret_cast<const ACTIONS *>(sp->Value.lpszA);
		dp->__union = SOAP_UNION_propValData_actions;
		dp->Value.actions = s_alloc<actions>(nullptr);
		dp->Value.actions->__ptr = s_alloc<action>(nullptr, lpSrcActions->cActions);
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

				da->act.moveCopy.store.__ptr = s_alloc<unsigned char>(nullptr, sa->actMoveCopy.cbStoreEntryId);
				memcpy(da->act.moveCopy.store.__ptr, sa->actMoveCopy.lpStoreEntryId, sa->actMoveCopy.cbStoreEntryId);
				da->act.moveCopy.store.__size = sa->actMoveCopy.cbStoreEntryId;

				da->act.moveCopy.folder.__ptr = s_alloc<unsigned char>(nullptr, sa->actMoveCopy.cbFldEntryId);
				memcpy(da->act.moveCopy.folder.__ptr, sa->actMoveCopy.lpFldEntryId, sa->actMoveCopy.cbFldEntryId);
				da->act.moveCopy.folder.__size = sa->actMoveCopy.cbFldEntryId;

				break;
			case OP_REPLY:
			case OP_OOF_REPLY:
				da->__union = SOAP_UNION__act_reply;
				da->act.reply.message.__ptr = s_alloc<unsigned char>(nullptr, sa->actReply.cbEntryId);
				memcpy(da->act.reply.message.__ptr, sa->actReply.lpEntryId, sa->actReply.cbEntryId);
				da->act.reply.message.__size = sa->actReply.cbEntryId;
				da->act.reply.guid.__size = sizeof(GUID);
				da->act.reply.guid.__ptr = s_alloc<unsigned char>(nullptr, sizeof(GUID));
				memcpy(da->act.reply.guid.__ptr, &sa->actReply.guidReplyTemplate, sizeof(GUID));
				break;
			case OP_DEFER_ACTION:
				da->__union = SOAP_UNION__act_defer;
				da->act.defer.bin.__ptr = s_alloc<unsigned char>(nullptr, sa->actDeferAction.cbData);
				da->act.defer.bin.__size = sa->actDeferAction.cbData;
				memcpy(da->act.defer.bin.__ptr,sa->actDeferAction.pbData, sa->actDeferAction.cbData);
				break;
			case OP_BOUNCE:
				da->__union = SOAP_UNION__act_bouncecode;
				da->act.bouncecode = sa->scBounceCode;
				break;
			case OP_FORWARD:
			case OP_DELEGATE:
				da->__union = SOAP_UNION__act_adrlist;
				hr = CopyMAPIRowSetToSOAPRowSet((LPSRowSet)sa->lpadrlist, &da->act.adrlist, lpConverter);
				if(hr != hrSuccess)
					return hr;
				break;
			case OP_TAG:
				da->__union = SOAP_UNION__act_prop;
				da->act.prop = s_alloc<propVal>(nullptr);
				hr = CopyMAPIPropValToSOAPPropVal(da->act.prop, &sa->propTag, lpConverter);
				break;
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
		hr = MAPI_E_INVALID_TYPE;
		break;
	}
	return hr;
}

HRESULT CopySOAPPropValToMAPIPropVal(SPropValue *dp, const struct propVal *sp,
    void *lpBase, convert_context *lpConverter)
{
	HRESULT hr = hrSuccess;

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
		if (sp->__union == 0 || sp->Value.hilo == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
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
		hr = MAPI_E_INVALID_TYPE;
		break;

	case PT_I8:
		dp->Value.li.QuadPart = sp->Value.li;
		break;
	case PT_STRING8: {
		if (sp->__union == 0 || sp->Value.lpszA == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		auto s = CONVERT_TO(lpConverter, std::string, sp->Value.lpszA, rawsize(sp->Value.lpszA), "UTF-8");
		hr = ECAllocateMore(s.length() + 1, lpBase, reinterpret_cast<void **>(&dp->Value.lpszA));
		if (hr != hrSuccess)
			return hr;
		strcpy(dp->Value.lpszA, s.c_str());
		break;
	}
	case PT_UNICODE: {
		if (sp->__union == 0 || sp->Value.lpszA == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		auto ws = CONVERT_TO(lpConverter, std::wstring, sp->Value.lpszA, rawsize(sp->Value.lpszA), "UTF-8");
		hr = ECAllocateMore(sizeof(std::wstring::value_type) * (ws.length() + 1), lpBase, reinterpret_cast<void **>(&dp->Value.lpszW));
		if (hr != hrSuccess)
			return hr;
		wcscpy(dp->Value.lpszW, ws.c_str());
		break;
	}
	case PT_SYSTIME:
		if (sp->__union == 0 || sp->Value.hilo == 0) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.ft.dwHighDateTime = sp->Value.hilo->hi;
		dp->Value.ft.dwLowDateTime = sp->Value.hilo->lo;
		break;
	case PT_CLSID:
		if (sp->__union == 0 || sp->Value.bin == nullptr ||
		    sp->Value.bin->__size != sizeof(MAPIUID)) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		hr = ECAllocateMore(sp->Value.bin->__size, lpBase,
		     reinterpret_cast<void **>(&dp->Value.lpguid));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.lpguid, sp->Value.bin->__ptr, sp->Value.bin->__size);
		break;
	case PT_BINARY:
		if (sp->__union == 0) {
			dp->Value.bin.lpb = NULL;
			dp->Value.bin.cb = 0;
			break;
		} else if (sp->Value.bin == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		hr = ECAllocateMore(sp->Value.bin->__size, lpBase,
		     reinterpret_cast<void **>(&dp->Value.bin.lpb));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.bin.lpb, sp->Value.bin->__ptr, sp->Value.bin->__size);
		dp->Value.bin.cb = sp->Value.bin->__size;
		break;
	case PT_MV_I2:
		if (sp->__union == 0 || sp->Value.mvi.__ptr == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVi.cValues = sp->Value.mvi.__size;
		hr = ECAllocateMore(sizeof(short int) * dp->Value.MVi.cValues, lpBase,
		     reinterpret_cast<void **>(&dp->Value.MVi.lpi));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.MVi.lpi, sp->Value.mvi.__ptr, sizeof(short int)*dp->Value.MVi.cValues);
		break;
	case PT_MV_LONG:
		if (sp->__union == 0 || sp->Value.mvl.__ptr == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVl.cValues = sp->Value.mvl.__size;
		hr = ECAllocateMore(sizeof(unsigned int) * dp->Value.MVl.cValues, lpBase,
		     reinterpret_cast<void **>(&dp->Value.MVl.lpl));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.MVl.lpl, sp->Value.mvl.__ptr, sizeof(unsigned int)*dp->Value.MVl.cValues);
		break;
	case PT_MV_R4:
		if (sp->__union == 0 || sp->Value.mvflt.__ptr == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVflt.cValues = sp->Value.mvflt.__size;
		hr = ECAllocateMore(sizeof(float) * dp->Value.MVflt.cValues, lpBase,
		     reinterpret_cast<void **>(&dp->Value.MVflt.lpflt));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.MVflt.lpflt, sp->Value.mvflt.__ptr, sizeof(float)*dp->Value.MVflt.cValues);
		break;
	case PT_MV_DOUBLE:
		if (sp->__union == 0 || sp->Value.mvdbl.__ptr == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVdbl.cValues = sp->Value.mvdbl.__size;
		hr = ECAllocateMore(sizeof(double) * dp->Value.MVdbl.cValues, lpBase,
		     reinterpret_cast<void **>(&dp->Value.MVdbl.lpdbl));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.MVdbl.lpdbl, sp->Value.mvdbl.__ptr, sizeof(double)*dp->Value.MVdbl.cValues);
		break;
	case PT_MV_CURRENCY:
		if (sp->__union == 0 || sp->Value.mvhilo.__ptr == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVcur.cValues = sp->Value.mvhilo.__size;
		hr = ECAllocateMore(sizeof(hiloLong) * dp->Value.MVcur.cValues, lpBase,
		     reinterpret_cast<void **>(&dp->Value.MVcur.lpcur));
		if (hr != hrSuccess)
			return hr;
		for (unsigned int i = 0; i < dp->Value.MVcur.cValues; ++i) {
			dp->Value.MVcur.lpcur[i].Hi = sp->Value.mvhilo.__ptr[i].hi;
			dp->Value.MVcur.lpcur[i].Lo = sp->Value.mvhilo.__ptr[i].lo;
		}
		break;
	case PT_MV_APPTIME:
		if (sp->__union == 0 || sp->Value.mvdbl.__ptr == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVat.cValues = sp->Value.mvdbl.__size;
		hr = ECAllocateMore(sizeof(double) * dp->Value.MVat.cValues, lpBase,
		     reinterpret_cast<void **>(&dp->Value.MVat.lpat));
		if (hr != hrSuccess)
			return hr;
		memcpy(dp->Value.MVat.lpat, sp->Value.mvdbl.__ptr, sizeof(double)*dp->Value.MVat.cValues);
		break;
	case PT_MV_SYSTIME:
		if (sp->__union == 0 || sp->Value.mvhilo.__ptr == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVft.cValues = sp->Value.mvhilo.__size;
		hr = ECAllocateMore(sizeof(hiloLong) * dp->Value.MVft.cValues, lpBase,
		     reinterpret_cast<void **>(&dp->Value.MVft.lpft));
		if (hr != hrSuccess)
			return hr;
		for (unsigned int i = 0; i < dp->Value.MVft.cValues; ++i) {
			dp->Value.MVft.lpft[i].dwHighDateTime = sp->Value.mvhilo.__ptr[i].hi;
			dp->Value.MVft.lpft[i].dwLowDateTime = sp->Value.mvhilo.__ptr[i].lo;
		}
		break;
	case PT_MV_BINARY:
		if (sp->__union == 0 || sp->Value.mvbin.__ptr == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVbin.cValues = sp->Value.mvbin.__size;
		hr = ECAllocateMore(sizeof(SBinary) * dp->Value.MVbin.cValues, lpBase,
		     reinterpret_cast<void **>(&dp->Value.MVbin.lpbin));
		if (hr != hrSuccess)
			return hr;
		for (unsigned int i = 0; i < dp->Value.MVbin.cValues; ++i) {
			dp->Value.MVbin.lpbin[i].cb = sp->Value.mvbin.__ptr[i].__size;
			if (dp->Value.MVbin.lpbin[i].cb == 0) {
				dp->Value.MVbin.lpbin[i].lpb = NULL;
				continue;
			}
			hr = ECAllocateMore(dp->Value.MVbin.lpbin[i].cb, lpBase,
			     reinterpret_cast<void **>(&dp->Value.MVbin.lpbin[i].lpb));
			if (hr != hrSuccess)
				return hr;
			memcpy(dp->Value.MVbin.lpbin[i].lpb, sp->Value.mvbin.__ptr[i].__ptr, dp->Value.MVbin.lpbin[i].cb);
		}
		break;
	case PT_MV_STRING8:
		if (sp->__union == 0 || sp->Value.mvszA.__ptr == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		if (lpConverter == NULL) {
			convert_context converter;
			CopySOAPPropValToMAPIPropVal(dp, sp, lpBase, &converter);
			break;
		}
		dp->Value.MVszA.cValues = sp->Value.mvszA.__size;
		hr = ECAllocateMore(sizeof(LPSTR) * dp->Value.MVszA.cValues, lpBase, (void **)&dp->Value.MVszA.lppszA);

		for (unsigned int i = 0; i < dp->Value.MVszA.cValues; ++i) {
			if (sp->Value.mvszA.__ptr[i] != NULL) {
				auto s = lpConverter->convert_to<std::string>(sp->Value.mvszA.__ptr[i], rawsize(sp->Value.mvszA.__ptr[i]), "UTF-8");
				hr = ECAllocateMore(s.size() + 1, lpBase, reinterpret_cast<void **>(&dp->Value.MVszA.lppszA[i]));
				if (hr != hrSuccess)
					return hr;
				strcpy(dp->Value.MVszA.lppszA[i], s.c_str());
			} else {
				hr = ECAllocateMore(1, lpBase, reinterpret_cast<void **>(&dp->Value.MVszA.lppszA[i]));
				if (hr != hrSuccess)
					return hr;
				dp->Value.MVszA.lppszA[i][0] = '\0';
			}
		}
		break;
	case PT_MV_UNICODE:
		if (sp->__union == 0 || sp->Value.mvszA.__ptr == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		if (lpConverter == NULL) {
			convert_context converter;
			CopySOAPPropValToMAPIPropVal(dp, sp, lpBase, &converter);
			break;
		}
		dp->Value.MVszW.cValues = sp->Value.mvszA.__size;
		hr = ECAllocateMore(sizeof(LPWSTR) * dp->Value.MVszW.cValues, lpBase,
		     reinterpret_cast<void **>(&dp->Value.MVszW.lppszW));
		if (hr != hrSuccess)
			return hr;

		for (unsigned int i = 0; i < dp->Value.MVszW.cValues; ++i) {
			if (sp->Value.mvszA.__ptr[i] == nullptr) {
				hr = ECAllocateMore(1, lpBase, reinterpret_cast<void **>(&dp->Value.MVszW.lppszW[i]));
				if (hr != hrSuccess)
					return hr;
				dp->Value.MVszW.lppszW[i][0] = '\0';
				continue;
			}
			auto ws = lpConverter->convert_to<std::wstring>(sp->Value.mvszA.__ptr[i], rawsize(sp->Value.mvszA.__ptr[i]), "UTF-8");
			hr = ECAllocateMore(sizeof(std::wstring::value_type) * (ws.length() + 1), lpBase,
			     reinterpret_cast<void **>(&dp->Value.MVszW.lppszW[i]));
			if (hr != hrSuccess)
				return hr;
			wcscpy(dp->Value.MVszW.lppszW[i], ws.c_str());
		}
		break;
	case PT_MV_CLSID:
		if (sp->__union == 0 || sp->Value.mvbin.__ptr == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVguid.cValues = sp->Value.mvbin.__size;
		hr = ECAllocateMore(sizeof(GUID) * dp->Value.MVguid.cValues, lpBase,
		     reinterpret_cast<void **>(&dp->Value.MVguid.lpguid));
		if (hr != hrSuccess)
			return hr;
		for (unsigned int i = 0; i < dp->Value.MVguid.cValues; ++i)
			memcpy(&dp->Value.MVguid.lpguid[i], sp->Value.mvbin.__ptr[i].__ptr, sizeof(GUID));
		break;
	case PT_MV_I8:
		if (sp->__union == 0 || sp->Value.mvli.__ptr == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		dp->Value.MVli.cValues = sp->Value.mvli.__size;
		hr = ECAllocateMore(sizeof(LARGE_INTEGER) * dp->Value.MVli.cValues, lpBase,
		     reinterpret_cast<void **>(&dp->Value.MVli.lpli));
		if (hr != hrSuccess)
			return hr;
		for (unsigned int i = 0; i < dp->Value.MVli.cValues; ++i)
			dp->Value.MVli.lpli[i].QuadPart = sp->Value.mvli.__ptr[i];
		break;
	case PT_SRESTRICTION:
		if (sp->__union == 0 || sp->Value.res == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		// NOTE: we place the object pointer in lpszA to make sure it's on the same offset as Value.x on 32-bit and 64-bit machines
		hr = ECAllocateMore(sizeof(SRestriction), lpBase, reinterpret_cast<void **>(&dp->Value.lpszA));
		if (hr != hrSuccess)
			return hr;
		hr = CopySOAPRestrictionToMAPIRestriction((LPSRestriction)dp->Value.lpszA, sp->Value.res, lpBase, lpConverter);
		break;
	case PT_ACTIONS: {
		if (sp->__union == 0 || sp->Value.actions == nullptr) {
			dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
			dp->Value.err = MAPI_E_NOT_FOUND;
			break;
		}
		// NOTE: we place the object pointer in lpszA to make sure it is on the same offset as Value.x on 32-bit and 64-bit machines
		hr = ECAllocateMore(sizeof(ACTIONS), lpBase, reinterpret_cast<void **>(&dp->Value.lpszA));
		if (hr != hrSuccess)
			return hr;
		auto lpDstActions = reinterpret_cast<ACTIONS *>(dp->Value.lpszA);
		lpDstActions->cActions = sp->Value.actions->__size;
		hr = ECAllocateMore(sizeof(ACTION) * sp->Value.actions->__size, lpBase,
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
				hr = ECAllocateMore(sa->act.moveCopy.store.__size, lpBase, reinterpret_cast<void **>(&da->actMoveCopy.lpStoreEntryId));
				if (hr != hrSuccess)
					return hr;
				memcpy(da->actMoveCopy.lpStoreEntryId, sa->act.moveCopy.store.__ptr, sa->act.moveCopy.store.__size);
				da->actMoveCopy.cbFldEntryId = sa->act.moveCopy.folder.__size;
				hr = ECAllocateMore(sa->act.moveCopy.folder.__size, lpBase, reinterpret_cast<void **>(&da->actMoveCopy.lpFldEntryId));
				if (hr != hrSuccess)
					return hr;
				memcpy(da->actMoveCopy.lpFldEntryId, sa->act.moveCopy.folder.__ptr, sa->act.moveCopy.folder.__size);
				break;
			case OP_REPLY:
			case OP_OOF_REPLY:
				da->actReply.cbEntryId = sa->act.reply.message.__size;
				hr = ECAllocateMore(sa->act.reply.message.__size, lpBase, reinterpret_cast<void **>(&da->actReply.lpEntryId));
				if (hr != hrSuccess)
					return hr;
				memcpy(da->actReply.lpEntryId, sa->act.reply.message.__ptr, sa->act.reply.message.__size);
				if (sa->act.reply.guid.__size != sizeof(GUID))
					return MAPI_E_CORRUPT_DATA;
				memcpy(&da->actReply.guidReplyTemplate, sa->act.reply.guid.__ptr, sa->act.reply.guid.__size);
				break;
			case OP_DEFER_ACTION:
				hr = ECAllocateMore(sa->act.defer.bin.__size, lpBase, reinterpret_cast<void **>(&da->actDeferAction.pbData));
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
				hr = ECAllocateMore(CbNewADRLIST(sa->act.adrlist->__size), lpBase, reinterpret_cast<void **>(&da->lpadrlist));
				if (hr != hrSuccess)
					return hr;
				da->lpadrlist->cEntries = 0;
				for (gsoap_size_t j = 0; j < sa->act.adrlist->__size; ++j) {
					da->lpadrlist->aEntries[j].ulReserved1 = 0;
					da->lpadrlist->aEntries[j].cValues = sa->act.adrlist->__ptr[j].__size;

					// new rowset allocate more on old rowset, so we can just call FreeProws once
					hr = ECAllocateMore(sizeof(SPropValue) * sa->act.adrlist->__ptr[j].__size, lpBase,
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
				break;
			}
		}
		break;
	}
	default:
		dp->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(sp->ulPropTag));
		dp->Value.err = MAPI_E_NOT_FOUND;
		break;
	}
	return hr;
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
	auto lpDest = s_alloc<entryId>(nullptr);
	auto hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryIdSrc, lpEntryIdSrc, lpDest, false);
	if (hr != hrSuccess) {
		s_free(nullptr, lpDest);
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

	if(bCheapCopy == false) {
		lpDest->__ptr = s_alloc<unsigned char>(nullptr, cbEntryIdSrc);
		memcpy(lpDest->__ptr, lpEntryIdSrc, cbEntryIdSrc);
	}else{
		lpDest->__ptr = (LPBYTE)lpEntryIdSrc;
	}

	lpDest->__size = cbEntryIdSrc;
	return hrSuccess;
}

HRESULT CopySOAPEntryIdToMAPIEntryId(const entryId *lpSrc, ULONG *lpcbDest,
    LPENTRYID *lppEntryIdDest, void *lpBase)
{
	HRESULT hr;
	LPENTRYID	lpEntryId = NULL;

	if (lpSrc == NULL || lpcbDest == NULL || lppEntryIdDest == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (lpSrc->__size == 0)
		return MAPI_E_INVALID_ENTRYID;
	if(lpBase)
		hr = ECAllocateMore(lpSrc->__size, lpBase, (void**)&lpEntryId);
	else
		hr = ECAllocateBuffer(lpSrc->__size, (void**)&lpEntryId);
	if(hr != hrSuccess)
		return hr;

	memcpy(lpEntryId, lpSrc->__ptr, lpSrc->__size);

	*lppEntryIdDest = lpEntryId;
	*lpcbDest = lpSrc->__size;
	return hrSuccess;
}

HRESULT CopySOAPEntryIdToMAPIEntryId(const entryId *lpSrc, ULONG ulObjId,
    ULONG ulType, ULONG *lpcbDest, LPENTRYID *lppEntryIdDest, void *lpBase)
{
	HRESULT hr;
	ULONG		cbEntryId = 0;
	LPENTRYID	lpEntryId = NULL;

	if (lpSrc == NULL || lpcbDest == NULL || lppEntryIdDest == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if((unsigned int)lpSrc->__size < CbNewABEID("") || lpSrc->__ptr == NULL)
		return MAPI_E_INVALID_ENTRYID;
	hr = KAllocCopy(lpSrc->__ptr, lpSrc->__size, reinterpret_cast<void **>(&lpEntryId), lpBase);
	if (hr != hrSuccess)
		return hr;
	cbEntryId = lpSrc->__size;

	*lppEntryIdDest = lpEntryId;
	*lpcbDest = cbEntryId;
	return hrSuccess;
}

HRESULT CopySOAPEntryIdToMAPIEntryId(const entryId *lpSrc, ULONG ulObjId,
    ULONG *lpcbDest, LPENTRYID *lppEntryIdDest, void *lpBase)
{
	return CopySOAPEntryIdToMAPIEntryId(lpSrc, ulObjId, MAPI_MAILUSER, lpcbDest, lppEntryIdDest, lpBase);
}

HRESULT CopyMAPIEntryListToSOAPEntryList(const ENTRYLIST *lpMsgList,
    struct entryList *lpsEntryList)
{
	unsigned int i = 0;

	if (lpMsgList == NULL || lpsEntryList == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if(lpMsgList->cValues == 0 || lpMsgList->lpbin == NULL)	{
		lpsEntryList->__ptr = NULL;
		lpsEntryList->__size = 0;
		return hrSuccess;
	}

	lpsEntryList->__ptr = s_alloc<entryId>(nullptr, lpMsgList->cValues);
	for (i = 0; i < lpMsgList->cValues; ++i) {
		lpsEntryList->__ptr[i].__ptr = s_alloc<unsigned char>(nullptr, lpMsgList->lpbin[i].cb);
		memcpy(lpsEntryList->__ptr[i].__ptr, lpMsgList->lpbin[i].lpb, lpMsgList->lpbin[i].cb);

		lpsEntryList->__ptr[i].__size = lpMsgList->lpbin[i].cb;
	}

	lpsEntryList->__size = i;
	return hrSuccess;
}

HRESULT CopySOAPEntryListToMAPIEntryList(const struct entryList *lpsEntryList,
    LPENTRYLIST *lppMsgList)
{
	unsigned int	i = 0;
	ecmem_ptr<ENTRYLIST> lpMsgList;

	if (lpsEntryList == nullptr || lppMsgList == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = ECAllocateBuffer(sizeof(ENTRYLIST), &~lpMsgList);
	if(hr != hrSuccess)
		return hr;

	if(lpsEntryList->__size == 0) {
		lpMsgList->cValues = 0;
		lpMsgList->lpbin = NULL;
	} else {

		hr = ECAllocateMore(lpsEntryList->__size * sizeof(SBinary), lpMsgList, (void**)&lpMsgList->lpbin);
		if(hr != hrSuccess)
			return hr;
	}

	for (i = 0; i < lpsEntryList->__size; ++i) {
		hr = ECAllocateMore(lpsEntryList->__ptr[i].__size, lpMsgList, (void**)&lpMsgList->lpbin[i].lpb);
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

	auto lpPropVal = s_alloc<propVal>(nullptr, lpRowSrc->cValues);
	memset(lpPropVal, 0, sizeof(struct propVal) *lpRowSrc->cValues);
	lpsRowDst->__ptr = lpPropVal;
	lpsRowDst->__size = 0;

	for (unsigned int i = 0; i < lpRowSrc->cValues; ++i) {
		auto hr = CopyMAPIPropValToSOAPPropVal(&lpPropVal[i], &lpRowSrc->lpProps[i], lpConverter);
		if (hr != hrSuccess) {
			FreePropValArray(lpsRowDst, false);
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
	auto lpsRowSetDst = s_alloc<rowSet>(nullptr);
	lpsRowSetDst->__ptr = NULL;
	lpsRowSetDst->__size = 0;
	if (lpRowSetSrc->cRows > 0) {
		lpsRowSetDst->__ptr = s_alloc<propValArray>(nullptr, lpRowSetSrc->cRows);
		lpsRowSetDst->__size = 0;

		for (unsigned int i = 0; i < lpRowSetSrc->cRows; ++i) {
			auto hr = CopyMAPIRowToSOAPRow(&lpRowSetSrc->aRow[i], &lpsRowSetDst->__ptr[i], lpConverter);
			if (hr != hrSuccess) {
				FreeRowSet(lpsRowSetDst, false);
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
	auto hr = ECAllocateBuffer(CbNewSRowSet(ulRows), &~lpRowSet);
	if (hr != hrSuccess)
		return hr;

	// Loop through all the rows and values, fill in any client-side generated values, or translate
	// some serverside values through TableRowGetProps

	for (lpRowSet->cRows = 0; lpRowSet->cRows < ulRows; ++lpRowSet->cRows) {
		auto i = lpRowSet->cRows;
		lpRowSet->aRow[i].ulAdrEntryPad = 0;
		lpRowSet->aRow[i].cValues = lpsRowSetSrc->__ptr[i].__size;
		hr = ECAllocateBuffer(sizeof(SPropValue) * lpsRowSetSrc->__ptr[i].__size, reinterpret_cast<void **>(&lpRowSet->aRow[i].lpProps));
		if (hr != hrSuccess)
			return hr;
		CopySOAPRowToMAPIRow(lpProvider, &lpsRowSetSrc->__ptr[i], lpRowSet->aRow[i].lpProps, (void **)lpRowSet->aRow[i].lpProps, ulType, &converter);
	}

	*lppRowSetDst = lpRowSet.release();
	return hrSuccess;
}

HRESULT CopySOAPRestrictionToMAPIRestriction(LPSRestriction lpDst,
    const struct restrictTable *lpSrc, void *lpBase,
    convert_context *lpConverter)
{
	HRESULT hr = hrSuccess;

	if (lpSrc == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if (lpConverter == NULL) {
		convert_context converter;
		CopySOAPRestrictionToMAPIRestriction(lpDst, lpSrc, lpBase, &converter);
		return hrSuccess;
	}

	memset(lpDst, 0, sizeof(SRestriction));
	lpDst->rt = lpSrc->ulType;

	switch(lpSrc->ulType) {
	case RES_OR:
		if (lpSrc->lpOr == NULL)
			return MAPI_E_INVALID_PARAMETER;
		lpDst->res.resOr.cRes = lpSrc->lpOr->__size;
		hr = ECAllocateMore(sizeof(SRestriction) * lpSrc->lpOr->__size, lpBase,
		     reinterpret_cast<void **>(&lpDst->res.resOr.lpRes));
		if (hr != hrSuccess)
			return hr;
		for (gsoap_size_t i = 0; i < lpSrc->lpOr->__size; ++i) {
			hr = CopySOAPRestrictionToMAPIRestriction(&lpDst->res.resOr.lpRes[i], lpSrc->lpOr->__ptr[i], lpBase, lpConverter);

			if(hr != hrSuccess)
				return hr;
		}
		break;

	case RES_AND:
		if (lpSrc->lpAnd == NULL)
			return MAPI_E_INVALID_PARAMETER;
		lpDst->res.resAnd.cRes = lpSrc->lpAnd->__size;
		hr = ECAllocateMore(sizeof(SRestriction) * lpSrc->lpAnd->__size, lpBase,
		     reinterpret_cast<void **>(&lpDst->res.resAnd.lpRes));
		if (hr != hrSuccess)
			return hr;
		for (gsoap_size_t i = 0; i < lpSrc->lpAnd->__size; ++i) {
			hr = CopySOAPRestrictionToMAPIRestriction(&lpDst->res.resAnd.lpRes[i], lpSrc->lpAnd->__ptr[i], lpBase, lpConverter);

			if(hr != hrSuccess)
				return hr;
		}
		break;

	case RES_BITMASK:
		if (lpSrc->lpBitmask == NULL)
			return MAPI_E_INVALID_PARAMETER;

		lpDst->res.resBitMask.relBMR = lpSrc->lpBitmask->ulType;
		lpDst->res.resBitMask.ulMask = lpSrc->lpBitmask->ulMask;
		lpDst->res.resBitMask.ulPropTag = lpSrc->lpBitmask->ulPropTag;
		break;

	case RES_COMMENT:
		if (lpSrc->lpComment == NULL)
			return MAPI_E_INVALID_PARAMETER;

		hr = ECAllocateMore(sizeof(SRestriction), lpBase, (void **) &lpDst->res.resComment.lpRes);
		if (hr != hrSuccess)
			return hr;
		hr = CopySOAPRestrictionToMAPIRestriction(lpDst->res.resComment.lpRes, lpSrc->lpComment->lpResTable, lpBase, lpConverter);
		if (hr != hrSuccess)
			return hr;

		lpDst->res.resComment.cValues = lpSrc->lpComment->sProps.__size;
		hr = ECAllocateMore(sizeof(SPropValue) * lpSrc->lpComment->sProps.__size, lpBase, (void **)&lpDst->res.resComment.lpProp);
		if (hr != hrSuccess)
			return hr;
		for (gsoap_size_t i = 0; i < lpSrc->lpComment->sProps.__size; ++i) {
			hr = CopySOAPPropValToMAPIPropVal(&lpDst->res.resComment.lpProp[i], &lpSrc->lpComment->sProps.__ptr[i], lpBase, lpConverter);
			if (hr != hrSuccess)
				return hr;
		}
		break;

	case RES_COMPAREPROPS:
		if (lpSrc->lpCompare == NULL)
			return MAPI_E_INVALID_PARAMETER;
		lpDst->res.resCompareProps.relop = lpSrc->lpCompare->ulType;
		lpDst->res.resCompareProps.ulPropTag1 = lpSrc->lpCompare->ulPropTag1;
		lpDst->res.resCompareProps.ulPropTag2 = lpSrc->lpCompare->ulPropTag2;
		break;

	case RES_CONTENT:
		if (lpSrc->lpContent == NULL || lpSrc->lpContent->lpProp == NULL)
			return MAPI_E_INVALID_PARAMETER;

		lpDst->res.resContent.ulFuzzyLevel = lpSrc->lpContent->ulFuzzyLevel;
		lpDst->res.resContent.ulPropTag = lpSrc->lpContent->ulPropTag;

		hr = ECAllocateMore(sizeof(SPropValue), lpBase, (void **) &lpDst->res.resContent.lpProp);
		if(hr != hrSuccess)
			return hr;

		hr = CopySOAPPropValToMAPIPropVal(lpDst->res.resContent.lpProp, lpSrc->lpContent->lpProp, lpBase, lpConverter);
		if(hr != hrSuccess)
			return hr;
		break;

	case RES_EXIST:
		if (lpSrc->lpExist == NULL)
			return MAPI_E_INVALID_PARAMETER;
		lpDst->res.resExist.ulPropTag = lpSrc->lpExist->ulPropTag;
		break;

	case RES_NOT:
		if (lpSrc->lpNot == NULL || lpSrc->lpNot->lpNot == NULL)
			return MAPI_E_INVALID_PARAMETER;
		hr = ECAllocateMore(sizeof(SRestriction), lpBase, reinterpret_cast<void **>(&lpDst->res.resNot.lpRes));
		if (hr != hrSuccess)
			return hr;
		hr = CopySOAPRestrictionToMAPIRestriction(lpDst->res.resNot.lpRes, lpSrc->lpNot->lpNot, lpBase, lpConverter);

		break;

	case RES_PROPERTY:
		if (lpSrc->lpProp == NULL || lpSrc->lpProp->lpProp == NULL)
			return MAPI_E_INVALID_PARAMETER;
		hr = ECAllocateMore(sizeof(SPropValue), lpBase, reinterpret_cast<void **>(&lpDst->res.resProperty.lpProp));
		if (hr != hrSuccess)
			return hr;
		lpDst->res.resProperty.relop = lpSrc->lpProp->ulType;
		lpDst->res.resProperty.ulPropTag = lpSrc->lpProp->ulPropTag;

		hr = CopySOAPPropValToMAPIPropVal(lpDst->res.resProperty.lpProp, lpSrc->lpProp->lpProp, lpBase, lpConverter);

		break;

	case RES_SIZE:
		if (lpSrc->lpSize == NULL)
			return MAPI_E_INVALID_PARAMETER;
		lpDst->res.resSize.cb = lpSrc->lpSize->cb;
		lpDst->res.resSize.relop = lpSrc->lpSize->ulType;
		lpDst->res.resSize.ulPropTag = lpSrc->lpSize->ulPropTag;
		break;

	case RES_SUBRESTRICTION:
		if (lpSrc->lpSub == NULL || lpSrc->lpSub->lpSubObject == NULL)
			return MAPI_E_INVALID_PARAMETER;
		lpDst->res.resSub.ulSubObject = lpSrc->lpSub->ulSubObject;
		hr = ECAllocateMore(sizeof(SRestriction), lpBase, reinterpret_cast<void **>(&lpDst->res.resSub.lpRes));
		if (hr != hrSuccess)
			return hr;
		hr = CopySOAPRestrictionToMAPIRestriction(lpDst->res.resSub.lpRes, lpSrc->lpSub->lpSubObject, lpBase, lpConverter);
		break;

	default:
		hr = MAPI_E_INVALID_PARAMETER;
		break;
	}
	return hr;
}

HRESULT CopyMAPIRestrictionToSOAPRestriction(struct restrictTable **lppDst,
    const SRestriction *lpSrc, convert_context *lpConverter)
{
	HRESULT hr = hrSuccess;
	struct restrictTable *lpDst = NULL;
	unsigned int i=0;
	auto laters = make_scope_success([&]() {
		if(hr != hrSuccess && lpDst != NULL)
			FreeRestrictTable(lpDst);
	});

	if (lpConverter == NULL) {
		convert_context converter;
		return CopyMAPIRestrictionToSOAPRestriction(lppDst, lpSrc, &converter);
	}

	lpDst = s_alloc<restrictTable>(nullptr);
	memset(lpDst, 0, sizeof(restrictTable));
	lpDst->ulType = lpSrc->rt;

	switch(lpSrc->rt) {
	case RES_OR:
		lpDst->lpOr = s_alloc<restrictOr>(nullptr);
		memset(lpDst->lpOr,0,sizeof(restrictOr));

		lpDst->lpOr->__ptr = s_alloc<restrictTable *>(nullptr, lpSrc->res.resOr.cRes);
		memset(lpDst->lpOr->__ptr, 0, sizeof(restrictTable*) * lpSrc->res.resOr.cRes);
		lpDst->lpOr->__size = lpSrc->res.resOr.cRes;

		for (i = 0; i < lpSrc->res.resOr.cRes; ++i) {
			hr = CopyMAPIRestrictionToSOAPRestriction(&(lpDst->lpOr->__ptr[i]), &lpSrc->res.resOr.lpRes[i], lpConverter);

			if(hr != hrSuccess)
				return hr;
		}
		break;

	case RES_AND:
		lpDst->lpAnd = s_alloc<restrictAnd>(nullptr);
		memset(lpDst->lpAnd,0,sizeof(restrictAnd));

		lpDst->lpAnd->__ptr = s_alloc<restrictTable *>(nullptr, lpSrc->res.resAnd.cRes);
		memset(lpDst->lpAnd->__ptr, 0, sizeof(restrictTable*) * lpSrc->res.resAnd.cRes);
		lpDst->lpAnd->__size = lpSrc->res.resAnd.cRes;

		for (i = 0; i < lpSrc->res.resAnd.cRes; ++i) {
			hr = CopyMAPIRestrictionToSOAPRestriction(&lpDst->lpAnd->__ptr[i], &lpSrc->res.resAnd.lpRes[i], lpConverter);

			if(hr != hrSuccess)
				return hr;
		}
		break;

	case RES_BITMASK:
		lpDst->lpBitmask = s_alloc<restrictBitmask>(nullptr);
		memset(lpDst->lpBitmask, 0, sizeof(restrictBitmask));

		lpDst->lpBitmask->ulMask = lpSrc->res.resBitMask.ulMask;
		lpDst->lpBitmask->ulPropTag = lpSrc->res.resBitMask.ulPropTag;
		lpDst->lpBitmask->ulType = lpSrc->res.resBitMask.relBMR;
		break;

	case RES_COMMENT:
		lpDst->lpComment = s_alloc<restrictComment>(nullptr);
		memset(lpDst->lpComment, 0, sizeof(restrictComment));

		lpDst->lpComment->sProps.__ptr = s_alloc<propVal>(nullptr, lpSrc->res.resComment.cValues);
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
		lpDst->lpCompare = s_alloc<restrictCompare>(nullptr);
		memset(lpDst->lpCompare, 0, sizeof(restrictCompare));

		lpDst->lpCompare->ulPropTag1 = lpSrc->res.resCompareProps.ulPropTag1;
		lpDst->lpCompare->ulPropTag2 = lpSrc->res.resCompareProps.ulPropTag2;
		lpDst->lpCompare->ulType = lpSrc->res.resCompareProps.relop;
		break;

	case RES_CONTENT:
		lpDst->lpContent = s_alloc<restrictContent>(nullptr);
		memset(lpDst->lpContent, 0, sizeof(restrictContent));

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
		lpDst->lpContent->lpProp = s_alloc<propVal>(nullptr);
		memset(lpDst->lpContent->lpProp, 0, sizeof(propVal));

		hr = CopyMAPIPropValToSOAPPropVal(lpDst->lpContent->lpProp, lpSrc->res.resContent.lpProp, lpConverter);
		if(hr != hrSuccess)
			return hr;
		break;

	case RES_EXIST:
		lpDst->lpExist = s_alloc<restrictExist>(nullptr);
		memset(lpDst->lpExist, 0, sizeof(restrictExist));

		lpDst->lpExist->ulPropTag = lpSrc->res.resExist.ulPropTag;
		break;

	case RES_NOT:
		lpDst->lpNot = s_alloc<restrictNot>(nullptr);
		memset(lpDst->lpNot, 0, sizeof(restrictNot));
		hr = CopyMAPIRestrictionToSOAPRestriction(&lpDst->lpNot->lpNot, lpSrc->res.resNot.lpRes, lpConverter);
		if(hr != hrSuccess)
			return hr;
		break;

	case RES_PROPERTY:
		lpDst->lpProp = s_alloc<restrictProp>(nullptr);
		memset(lpDst->lpProp, 0, sizeof(restrictProp));

		lpDst->lpProp->ulType = lpSrc->res.resProperty.relop;
		lpDst->lpProp->lpProp = s_alloc<propVal>(nullptr);
		memset(lpDst->lpProp->lpProp, 0, sizeof(propVal));
		lpDst->lpProp->ulPropTag = lpSrc->res.resProperty.ulPropTag;

		hr = CopyMAPIPropValToSOAPPropVal(lpDst->lpProp->lpProp, lpSrc->res.resProperty.lpProp, lpConverter);
		if(hr != hrSuccess)
			return hr;
		break;

	case RES_SIZE:
		lpDst->lpSize = s_alloc<restrictSize>(nullptr);
		memset(lpDst->lpSize, 0, sizeof(restrictSize));

		lpDst->lpSize->cb = lpSrc->res.resSize.cb;
		lpDst->lpSize->ulPropTag = lpSrc->res.resSize.ulPropTag;
		lpDst->lpSize->ulType = lpSrc->res.resSize.relop;
		break;

	case RES_SUBRESTRICTION:
		lpDst->lpSub = s_alloc<restrictSub>(nullptr);
		memset(lpDst->lpSub, 0, sizeof(restrictSub));

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
	HRESULT hr;
	LPSPropTagArray	lpPropTagArray = NULL;

	if (lpsPropTagArray == NULL || lppPropTagArray == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if(lpBase)
		hr = ECAllocateMore(CbNewSPropTagArray(lpsPropTagArray->__size), lpBase, (void**)&lpPropTagArray);
	else
		hr = ECAllocateBuffer(CbNewSPropTagArray(lpsPropTagArray->__size), (void**)&lpPropTagArray);
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
	HRESULT	hr;
	std::string strDest;
	size_t cbDest;

	if (lpszUtf8 == NULL || lppszTString == NULL)
		return MAPI_E_INVALID_PARAMETER;

	strDest = CONVERT_TO(lpConverter, std::string, ((ulFlags & MAPI_UNICODE) ? CHARSET_WCHAR : CHARSET_CHAR), lpszUtf8, rawsize(lpszUtf8), "UTF-8");
	cbDest = strDest.length() + ((ulFlags & MAPI_UNICODE) ? sizeof(WCHAR) : sizeof(CHAR));

	if (lpBase)
		hr = ECAllocateMore(cbDest, lpBase, (LPVOID*)lppszTString);
	else
		hr = ECAllocateBuffer(cbDest, (LPVOID*)lppszTString);

	if (hr != hrSuccess)
		return hr;

	memset(*lppszTString, 0, cbDest);
	memcpy(*lppszTString, strDest.c_str(), strDest.length());
	return hrSuccess;
}

static HRESULT TStringToUtf8(const TCHAR *lpszTstring, ULONG ulFlags,
    void *lpBase, convert_context *lpConverter, char **lppszUtf8)
{
	HRESULT	hr;
	std::string strDest;
	size_t cbDest;

	if (lpszTstring == NULL || lppszUtf8 == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if (ulFlags & MAPI_UNICODE)
		strDest = CONVERT_TO(lpConverter, std::string, "UTF-8", (wchar_t*)lpszTstring, rawsize((wchar_t*)lpszTstring), CHARSET_WCHAR);
	else
		strDest = CONVERT_TO(lpConverter, std::string, "UTF-8", (char*)lpszTstring, rawsize((char*)lpszTstring), CHARSET_CHAR);
	cbDest = strDest.length() + 1;

	if (lpBase)
		hr = ECAllocateMore(cbDest, lpBase, (LPVOID*)lppszUtf8);
	else
		hr = ECAllocateBuffer(cbDest, (LPVOID*)lppszUtf8);

	if (hr != hrSuccess)
		return hr;

	memcpy(*lppszUtf8, strDest.c_str(), cbDest);
	return hrSuccess;
}

HRESULT CopyABPropsFromSoap(const struct propmapPairArray *lpsoapPropmap,
    const struct propmapMVPairArray *lpsoapMVPropmap, SPROPMAP *lpPropmap,
    MVPROPMAP *lpMVPropmap, void *lpBase, ULONG ulFlags)
{
	HRESULT hr;
	unsigned int nLen = 0;
	convert_context converter;
	ULONG ulConvFlags;

	if (lpsoapPropmap != NULL) {
		lpPropmap->cEntries = lpsoapPropmap->__size;
		nLen = sizeof(*lpPropmap->lpEntries) * lpPropmap->cEntries;
		hr = ECAllocateMore(nLen, lpBase, (void**)&lpPropmap->lpEntries);
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
		hr = ECAllocateMore(sizeof(*lpMVPropmap->lpEntries) * lpMVPropmap->cEntries, lpBase, (void**)&lpMVPropmap->lpEntries);
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
			nLen = sizeof(*lpMVPropmap->lpEntries[i].lpszValues) * lpMVPropmap->lpEntries[i].cValues;
			hr = ECAllocateMore(nLen, lpBase, (void**)&lpMVPropmap->lpEntries[i].lpszValues);
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

HRESULT CopyABPropsToSoap(const SPROPMAP *lpPropmap,
    const MVPROPMAP *lpMVPropmap, ULONG ulFlags,
    struct propmapPairArray **lppsoapPropmap,
    struct propmapMVPairArray **lppsoapMVPropmap)
{
	HRESULT hr = hrSuccess;
	ecmem_ptr<struct propmapPairArray> soapPropmap;
	ecmem_ptr<struct propmapMVPairArray> soapMVPropmap;
	convert_context	converter;
	ULONG ulConvFlags;

	if (lpPropmap && lpPropmap->cEntries) {
		hr = ECAllocateBuffer(sizeof *soapPropmap, &~soapPropmap);
		if (hr != hrSuccess)
			return hr;
		soapPropmap->__size = lpPropmap->cEntries;
		hr = ECAllocateMore(soapPropmap->__size * sizeof *soapPropmap->__ptr, soapPropmap, (void**)&soapPropmap->__ptr);
		if (hr != hrSuccess)
			return hr;

		for (gsoap_size_t i = 0; i < soapPropmap->__size; ++i) {
			if (PROP_TYPE(lpPropmap->lpEntries[i].ulPropId) != PT_BINARY) {
				soapPropmap->__ptr[i].ulPropId = CHANGE_PROP_TYPE(lpPropmap->lpEntries[i].ulPropId, PT_STRING8);
				ulConvFlags = ulFlags;
			} else {
				soapPropmap->__ptr[i].ulPropId = lpPropmap->lpEntries[i].ulPropId;
				ulConvFlags = 0;
			}

			hr = TStringToUtf8(lpPropmap->lpEntries[i].lpszValue, ulConvFlags, soapPropmap, &converter, &soapPropmap->__ptr[i].lpszValue);
			if (hr != hrSuccess)
				return hr;
		}
	}

	if (lpMVPropmap && lpMVPropmap->cEntries) {
		hr = ECAllocateBuffer(sizeof *soapMVPropmap, &~soapMVPropmap);
		if (hr != hrSuccess)
			return hr;
		soapMVPropmap->__size = lpMVPropmap->cEntries;
		hr = ECAllocateMore(soapMVPropmap->__size * sizeof *soapMVPropmap->__ptr, soapMVPropmap, (void**)&soapMVPropmap->__ptr);
		if (hr != hrSuccess)
			return hr;

		for (gsoap_size_t i = 0; i < soapMVPropmap->__size; ++i) {
			if (PROP_TYPE(lpMVPropmap->lpEntries[i].ulPropId) != PT_MV_BINARY) {
				soapMVPropmap->__ptr[i].ulPropId = CHANGE_PROP_TYPE(lpMVPropmap->lpEntries[i].ulPropId, PT_MV_STRING8);
				ulConvFlags = ulFlags;
			} else {
				soapMVPropmap->__ptr[i].ulPropId = lpMVPropmap->lpEntries[i].ulPropId;
				ulConvFlags = 0;
			}

			soapMVPropmap->__ptr[i].sValues.__size = lpMVPropmap->lpEntries[i].cValues;
			hr = ECAllocateMore(soapMVPropmap->__ptr[i].sValues.__size * sizeof * soapMVPropmap->__ptr[i].sValues.__ptr, soapMVPropmap, (void**)&soapMVPropmap->__ptr[i].sValues.__ptr);
			if (hr != hrSuccess)
				return hr;

			for (gsoap_size_t j = 0; j < soapMVPropmap->__ptr[i].sValues.__size; ++j) {
				hr = TStringToUtf8(lpMVPropmap->lpEntries[i].lpszValues[j], ulConvFlags, soapMVPropmap, &converter, &soapMVPropmap->__ptr[i].sValues.__ptr[j]);
				if (hr != hrSuccess)
					return hr;
			}
		}
	}

	if (lppsoapPropmap != nullptr)
		*lppsoapPropmap = soapPropmap.release();
	if (lppsoapMVPropmap != nullptr)
		*lppsoapMVPropmap = soapMVPropmap.release();
	return hrSuccess;
}

HRESULT FreeABProps(struct propmapPairArray *lpsoapPropmap, struct propmapMVPairArray *lpsoapMVPropmap)
{
	if (lpsoapPropmap)
		ECFreeBuffer(lpsoapPropmap);

	if (lpsoapMVPropmap)
		ECFreeBuffer(lpsoapMVPropmap);

	return hrSuccess;
}

static HRESULT SoapUserToUser(const struct user *lpUser, ECUSER *lpsUser,
    ULONG ulFlags, void *lpBase, convert_context &converter)
{
	HRESULT hr;

	if (lpUser == NULL || lpsUser == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (lpBase == NULL)
		lpBase = lpsUser;

	memset(lpsUser, 0, sizeof(*lpsUser));

	hr = Utf8ToTString(lpUser->lpszUsername, ulFlags, lpBase, &converter, &lpsUser->lpszUsername);

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

	hr = CopySOAPEntryIdToMAPIEntryId(&lpUser->sUserId, lpUser->ulUserId, (ULONG*)&lpsUser->sUserId.cb, (LPENTRYID*)&lpsUser->sUserId.lpb, lpBase);
	if (hr != hrSuccess)
		return hr;

	lpsUser->ulIsAdmin		= lpUser->ulIsAdmin;
	lpsUser->ulIsABHidden	= lpUser->ulIsABHidden;
	lpsUser->ulCapacity		= lpUser->ulCapacity;
	lpsUser->ulObjClass = (objectclass_t)lpUser->ulObjClass;

	return hrSuccess;
}

HRESULT SoapUserArrayToUserArray(const struct userArray *lpUserArray,
    ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	ECUSER *lpECUsers = NULL;
	convert_context	converter;

	if (lpUserArray == NULL || lpcUsers == NULL || lppsUsers == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = ECAllocateBuffer(sizeof(ECUSER) * lpUserArray->__size, reinterpret_cast<void **>(&lpECUsers));
	if (hr != hrSuccess)
		return hr;
	memset(lpECUsers, 0, sizeof(ECUSER) * lpUserArray->__size);

	for (gsoap_size_t i = 0; i < lpUserArray->__size; ++i) {
		hr = SoapUserToUser(lpUserArray->__ptr + i, lpECUsers + i, ulFlags, lpECUsers, converter);
		if (hr != hrSuccess)
			return hr;
	}

	*lppsUsers = lpECUsers;
	*lpcUsers = lpUserArray->__size;
	return hrSuccess;
}

HRESULT SoapUserToUser(const struct user *lpUser, ULONG ulFlags,
    ECUSER **lppsUser)
{
	ecmem_ptr<ECUSER> lpsUser;
	convert_context	converter;

	if (lpUser == nullptr || lppsUser == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = ECAllocateBuffer(sizeof *lpsUser, &~lpsUser);
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

	if (lpBase == NULL)
		lpBase = lpsGroup;

	memset(lpsGroup, 0, sizeof(*lpsGroup));

	if (lpGroup->lpszGroupname == NULL)
		return MAPI_E_INVALID_OBJECT;

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

	hr = CopySOAPEntryIdToMAPIEntryId(&lpGroup->sGroupId, lpGroup->ulGroupId,
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
	ecmem_ptr<ECGROUP> lpECGroups;
	convert_context	converter;

	if (lpGroupArray == nullptr || lpcGroups == nullptr || lppsGroups == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = ECAllocateBuffer(sizeof(ECGROUP) * lpGroupArray->__size, &~lpECGroups);
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
	ecmem_ptr<ECGROUP> lpsGroup;
	convert_context	converter;

	if (lpGroup == nullptr || lppsGroup == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = ECAllocateBuffer(sizeof(*lpsGroup), &~lpsGroup);
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

	hr = CopySOAPEntryIdToMAPIEntryId(&lpCompany->sAdministrator, lpCompany->ulAdministrator, (ULONG*)&lpsCompany->sAdministrator.cb, (LPENTRYID*)&lpsCompany->sAdministrator.lpb, lpBase);
	if (hr != hrSuccess)
		return hr;

	hr = CopySOAPEntryIdToMAPIEntryId(&lpCompany->sCompanyId, lpCompany->ulCompanyId, (ULONG*)&lpsCompany->sCompanyId.cb, (LPENTRYID*)&lpsCompany->sCompanyId.lpb, lpBase);
	if (hr != hrSuccess)
		return hr;

	lpsCompany->ulIsABHidden	= lpCompany->ulIsABHidden;
	return hrSuccess;
}

HRESULT SoapCompanyArrayToCompanyArray(
    const struct companyArray *lpCompanyArray, ULONG ulFlags,
    ULONG *lpcCompanies, ECCOMPANY **lppsCompanies)
{
	ecmem_ptr<ECCOMPANY> lpECCompanies;
	convert_context	converter;

	if (lpCompanyArray == nullptr || lpcCompanies == nullptr ||
	    lppsCompanies == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = ECAllocateBuffer(sizeof(ECCOMPANY) * lpCompanyArray->__size, &~lpECCompanies);
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
	ecmem_ptr<ECCOMPANY> lpsCompany;
	convert_context	converter;

	if (lpCompany == nullptr || lppsCompany == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = ECAllocateBuffer(sizeof(*lpsCompany), &~lpsCompany);
	if (hr != hrSuccess)
		return hr;
	hr = SoapCompanyToCompany(lpCompany, lpsCompany, ulFlags, NULL, converter);
	if (hr != hrSuccess)
		return hr;
	*lppsCompany = lpsCompany.release();
	return hrSuccess;
}

HRESULT SvrNameListToSoapMvString8(ECSVRNAMELIST *lpSvrNameList,
    ULONG ulFlags, struct mv_string8 **lppsSvrNameList)
{
	ecmem_ptr<struct mv_string8> lpsSvrNameList;
	convert_context		converter;

	if (lpSvrNameList == nullptr || lppsSvrNameList == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = ECAllocateBuffer(sizeof(*lpsSvrNameList), &~lpsSvrNameList);
	if (hr != hrSuccess)
		return hr;
	memset(lpsSvrNameList, 0, sizeof *lpsSvrNameList);
	
	if (lpSvrNameList->cServers > 0) {
		lpsSvrNameList->__size = lpSvrNameList->cServers;
		hr = ECAllocateMore(lpSvrNameList->cServers * sizeof *lpsSvrNameList->__ptr, lpsSvrNameList, (void**)&lpsSvrNameList->__ptr);
		if (hr != hrSuccess)
			return hr;
		memset(lpsSvrNameList->__ptr, 0, lpSvrNameList->cServers * sizeof *lpsSvrNameList->__ptr);
		
		for (unsigned i = 0; i < lpSvrNameList->cServers; ++i) {
			hr = TStringToUtf8(lpSvrNameList->lpszaServer[i], ulFlags, lpSvrNameList, &converter, &lpsSvrNameList->__ptr[i]);
			if (hr != hrSuccess)
				return hr;
		}
	}
	
	*lppsSvrNameList = lpsSvrNameList.release();
	return hrSuccess;
}

HRESULT SoapServerListToServerList(const struct serverList *lpsServerList,
    ULONG ulFLags, ECSERVERLIST **lppServerList)
{
	ecmem_ptr<ECSERVERLIST> lpServerList;
	convert_context	converter;

	if (lpsServerList == nullptr || lppServerList == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = ECAllocateBuffer(sizeof(*lpServerList), &~lpServerList);
	if (hr != hrSuccess)
		return hr;
	memset(lpServerList, 0, sizeof *lpServerList);
	if (lpsServerList->__size == 0 || lpsServerList->__ptr == nullptr) {
		*lppServerList = lpServerList.release();
		return hrSuccess;
	}
	lpServerList->cServers = lpsServerList->__size;
	hr = ECAllocateMore(lpsServerList->__size * sizeof *lpServerList->lpsaServer, lpServerList, (void **)&lpServerList->lpsaServer);
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

HRESULT CreateSoapTransport(ULONG ulUIFlags, const sGlobalProfileProps
    &sProfileProps, KCmdProxy **const lppCmd)
{
	return CreateSoapTransport(ulUIFlags,
		sProfileProps.strServerPath.c_str(),
		sProfileProps.strSSLKeyFile.c_str(),
		sProfileProps.strSSLKeyPass.c_str(),
		sProfileProps.ulConnectionTimeOut,
		sProfileProps.strProxyHost.c_str(),
		sProfileProps.ulProxyPort,
		sProfileProps.strProxyUserName.c_str(),
		sProfileProps.strProxyPassword.c_str(),
		sProfileProps.ulProxyFlags,
		SOAP_IO_KEEPALIVE | SOAP_C_UTFSTRING,
		SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING,
		lppCmd);
}

// Wrap the server store entryid to client store entry. (Add a servername)
HRESULT WrapServerClientStoreEntry(const char *lpszServerName,
    const entryId *lpsStoreId, ULONG *lpcbStoreID, ENTRYID **lppStoreID)
{
	LPENTRYID	lpStoreID = NULL;
	ULONG		ulSize;

	if (lpsStoreId == NULL || lpszServerName == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (lpsStoreId->__size < 4) {
		ec_log_crit("Assertion lpsStoreId->__size >= 4 failed");
		return MAPI_E_INVALID_PARAMETER;
	}

	// The new entryid size is, current size + servername size + 1 byte term 0 - 4 bytes padding
	ulSize = lpsStoreId->__size+strlen(lpszServerName)+1-4;
	auto hr = ECAllocateBuffer(ulSize, reinterpret_cast<void **>(&lpStoreID));
	if(hr != hrSuccess)
		return hr;

	memset(lpStoreID, 0, ulSize );

	//Copy the entryid without servername
	memcpy(lpStoreID, lpsStoreId->__ptr, lpsStoreId->__size);

	// Add the server name
	strcpy((char*)lpStoreID+(lpsStoreId->__size-4), lpszServerName);

	*lpcbStoreID = ulSize;
	*lppStoreID = lpStoreID;
	return hrSuccess;
}

// Un wrap the client store entryid to server store entry. (remove a servername)
HRESULT UnWrapServerClientStoreEntry(ULONG cbWrapStoreID,
    const ENTRYID *lpWrapStoreID, ULONG *lpcbUnWrapStoreID,
    ENTRYID **lppUnWrapStoreID)
{
	LPENTRYID lpUnWrapStoreID = NULL;
	ULONG	ulSize = 0;

	if (lpWrapStoreID == NULL || lppUnWrapStoreID == NULL)
		return MAPI_E_INVALID_PARAMETER;

	auto peid = reinterpret_cast<const EID *>(lpWrapStoreID);
	if (peid->ulVersion == 0)
		ulSize = SIZEOF_EID_V0_FIXED;
	else if (peid->ulVersion == 1)
		ulSize = sizeof(EID_FIXED);
	else
		return MAPI_E_INVALID_ENTRYID;

	if (cbWrapStoreID < ulSize)
		return MAPI_E_INVALID_ENTRYID;

	auto hr = ECAllocateBuffer(ulSize, reinterpret_cast<void **>(&lpUnWrapStoreID));
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
	LPENTRYID lpUnWrapABID = NULL;
	ULONG	ulSize = 0;

	if (lpWrapABID == NULL || lppUnWrapABID == NULL)
		return MAPI_E_INVALID_PARAMETER;

	// Check minimum size of EntryID
	if (cbWrapABID < sizeof(ABEID))
		return MAPI_E_INVALID_ENTRYID;

	// FIXME: Check whether it is a Zarafa entry?
	auto pabeid = reinterpret_cast<const ABEID *>(lpWrapABID);
	if (pabeid->ulVersion == 0)
		ulSize = CbNewABEID("");
	else if (pabeid->ulVersion == 1)
		ulSize = (sizeof(ABEID) + strnlen(pabeid->szExId, cbWrapABID - sizeof(ABEID)) + 4) / 4 * 4;
	else
		return MAPI_E_INVALID_ENTRYID;

	if (cbWrapABID < ulSize)
		return MAPI_E_INVALID_ENTRYID;
	auto hr = ECAllocateBuffer(ulSize, reinterpret_cast<void **>(&lpUnWrapABID));
	if(hr != hrSuccess)
		return hr;

	memset(lpUnWrapABID, 0, ulSize);

	// Remove servername
	memcpy(lpUnWrapABID, lpWrapABID, ulSize-4);

	*lppUnWrapABID = lpUnWrapABID;
	*lpcbUnWrapABID = ulSize;
	return hrSuccess;
}

HRESULT CopySOAPNotificationToMAPINotification(void *lpProvider,
    const struct notification *lpSrc, NOTIFICATION **lppDst,
    convert_context *lpConverter)
{
	ecmem_ptr<NOTIFICATION> lpNotification;
	int nLen;

	auto hr = ECAllocateBuffer(sizeof(NOTIFICATION), &~lpNotification);
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
			nLen = strlen(lpSrc->newmail->lpszMessageClass)+1;
			hr = ECAllocateMore(nLen, lpNotification, reinterpret_cast<void **>(&dst.lpszMessageClass));
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

		if (lpSrc->tab->propIndex.Value.bin){
			auto &bin = dst.propIndex.Value.bin;
			bin.cb = lpSrc->tab->propIndex.Value.bin->__size;
			hr = ECAllocateMore(bin.cb, lpNotification,
			     reinterpret_cast<void **>(&bin.lpb));
			if (hr != hrSuccess)
				break;
			memcpy(bin.lpb, lpSrc->tab->propIndex.Value.bin->__ptr, lpSrc->tab->propIndex.Value.bin->__size);
		}

		dst.propPrior.ulPropTag = lpSrc->tab->propPrior.ulPropTag;

		if (lpSrc->tab->propPrior.Value.bin){
			auto &bin = dst.propPrior.Value.bin;
			bin.cb = lpSrc->tab->propPrior.Value.bin->__size;
			hr = ECAllocateMore(bin.cb, lpNotification,
			     reinterpret_cast<void **>(&bin.lpb));
			if (hr != hrSuccess)
				break;
			memcpy(bin.lpb, lpSrc->tab->propPrior.Value.bin->__ptr, lpSrc->tab->propPrior.Value.bin->__size);
		}

		if(lpSrc->tab->pRow)
		{
			dst.row.cValues = lpSrc->tab->pRow->__size;
			hr = ECAllocateMore(sizeof(SPropValue) * dst.row.cValues, lpNotification,
			     reinterpret_cast<void **>(&dst.row.lpProps));
			if (hr != hrSuccess)
				break;
			CopySOAPRowToMAPIRow(lpProvider, lpSrc->tab->pRow, dst.row.lpProps,
				reinterpret_cast<void **>(lpNotification.get()),
				lpSrc->tab->ulObjType, lpConverter);
		}
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
	HRESULT hr = hrSuccess;
	LPSBinary lpSBinary = NULL;

	if (lpSrc->ulEventType != fnevKopanoIcsChange)
		return MAPI_E_INVALID_PARAMETER;
	if (lpBase == NULL)
		hr = ECAllocateBuffer(sizeof(*lpSBinary), reinterpret_cast<void **>(&lpSBinary));
	else
		hr = ECAllocateMore(sizeof(*lpSBinary), lpBase, reinterpret_cast<void **>(&lpSBinary));
	if (hr != hrSuccess)
		return hr;
	memset(lpSBinary, 0, sizeof *lpSBinary);

	lpSBinary->cb = lpSrc->ics->pSyncState->__size;

	if (lpBase == NULL)
		hr = ECAllocateMore(lpSBinary->cb, lpSBinary, reinterpret_cast<void **>(&lpSBinary->lpb));
	else
		hr = ECAllocateMore(lpSBinary->cb, lpBase, reinterpret_cast<void **>(&lpSBinary->lpb));
	if (hr != hrSuccess) {
		MAPIFreeBuffer(lpSBinary);
		return hr;
	}

	memcpy(lpSBinary->lpb, lpSrc->ics->pSyncState->__ptr, lpSBinary->cb);

	*lppDst = lpSBinary;
	lpSBinary = NULL;

	MAPIFreeBuffer(lpSBinary);
	return hr;
}

static HRESULT CopyMAPISourceKeyToSoapSourceKey(const SBinary *lpsMAPISourceKey,
    struct xsd__base64Binary *lpsSoapSourceKey, void *lpBase)
{
	HRESULT hr;
	struct xsd__base64Binary sSoapSourceKey;

	if (lpsMAPISourceKey == NULL || lpsSoapSourceKey == NULL)
		return MAPI_E_INVALID_PARAMETER;

	sSoapSourceKey.__size = (int)lpsMAPISourceKey->cb;
	hr = KAllocCopy(lpsMAPISourceKey->lpb, lpsMAPISourceKey->cb, reinterpret_cast<void **>(&sSoapSourceKey.__ptr), lpBase);
	if (hr != hrSuccess)
		return hr;
	*lpsSoapSourceKey = sSoapSourceKey;
	return hrSuccess;
}

HRESULT CopyICSChangeToSOAPSourceKeys(ULONG cbChanges,
    const ICSCHANGE *lpsChanges, sourceKeyPairArray **lppsSKPA)
{
	HRESULT				hr = hrSuccess;
	memory_ptr<sourceKeyPairArray> lpsSKPA;

	if (lpsChanges == nullptr || lppsSKPA == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	hr = MAPIAllocateBuffer(sizeof *lpsSKPA, &~lpsSKPA);
	if (hr != hrSuccess)
		return hr;
	memset(lpsSKPA, 0, sizeof *lpsSKPA);

	if (cbChanges > 0) {
		lpsSKPA->__size = cbChanges;

		hr = MAPIAllocateMore(cbChanges * sizeof *lpsSKPA->__ptr, lpsSKPA, (void**)&lpsSKPA->__ptr);
		if (hr != hrSuccess)
			return hr;
		memset(lpsSKPA->__ptr, 0, cbChanges * sizeof *lpsSKPA->__ptr);

		for (unsigned i = 0; i < cbChanges; ++i) {
			hr = CopyMAPISourceKeyToSoapSourceKey(&lpsChanges[i].sSourceKey, &lpsSKPA->__ptr[i].sObjectKey, lpsSKPA);
			if (hr != hrSuccess)
				return hr;
			hr = CopyMAPISourceKeyToSoapSourceKey(&lpsChanges[i].sParentSourceKey, &lpsSKPA->__ptr[i].sParentKey, lpsSKPA);
			if (hr != hrSuccess)
				return hr;
		}
	}
	*lppsSKPA = lpsSKPA.release();
	return hrSuccess;
}

HRESULT CopyUserClientUpdateStatusFromSOAP(struct userClientUpdateStatusResponse &sUCUS,
    ULONG ulFlags, ECUSERCLIENTUPDATESTATUS **lppECUCUS)
{
	memory_ptr<ECUSERCLIENTUPDATESTATUS> lpECUCUS;
	convert_context converter;

	auto hr = MAPIAllocateBuffer(sizeof(ECUSERCLIENTUPDATESTATUS), &~lpECUCUS);
	if (hr != hrSuccess)
		return hr;

	memset(lpECUCUS, 0, sizeof(ECUSERCLIENTUPDATESTATUS));
	lpECUCUS->ulTrackId = sUCUS.ulTrackId;
	lpECUCUS->tUpdatetime = sUCUS.tUpdatetime;
	lpECUCUS->ulStatus = sUCUS.ulStatus;

	if (sUCUS.lpszCurrentversion)
		hr = Utf8ToTString(sUCUS.lpszCurrentversion, ulFlags, lpECUCUS, &converter, &lpECUCUS->lpszCurrentversion);

	if (hr == hrSuccess && sUCUS.lpszLatestversion)
		hr = Utf8ToTString(sUCUS.lpszLatestversion, ulFlags, lpECUCUS, &converter, &lpECUCUS->lpszLatestversion);

	if (hr == hrSuccess && sUCUS.lpszComputername)
		hr = Utf8ToTString(sUCUS.lpszComputername,  ulFlags, lpECUCUS, &converter, &lpECUCUS->lpszComputername);

	if (hr != hrSuccess)
		return hr;
	*lppECUCUS = lpECUCUS.release();
	return hrSuccess;
}

static HRESULT ConvertString8ToUnicode(const char *lpszA, WCHAR **lppszW,
    void *base, convert_context &converter)
{
	std::wstring wide;
	WCHAR *lpszW = NULL;

	if (lpszA == NULL || lppszW == NULL)
		return MAPI_E_INVALID_PARAMETER;

	TryConvert(lpszA, wide);
	auto hr = ECAllocateMore((wide.length() + 1) * sizeof(std::wstring::value_type),
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
	HRESULT hr;
	ULONG i;

	if (lpRestriction == NULL)
		return hrSuccess;

	switch (lpRestriction->rt) {
	case RES_OR:
		for (i = 0; i < lpRestriction->res.resOr.cRes; ++i) {
			hr = ConvertString8ToUnicode(&lpRestriction->res.resOr.lpRes[i], base, converter);
			if (hr != hrSuccess)
				return hr;
		}
		break;
	case RES_AND:
		for (i = 0; i < lpRestriction->res.resAnd.cRes; ++i) {
			hr = ConvertString8ToUnicode(&lpRestriction->res.resAnd.lpRes[i], base, converter);
			if (hr != hrSuccess)
				return hr;
		}
		break;
	case RES_NOT:
		hr = ConvertString8ToUnicode(lpRestriction->res.resNot.lpRes, base, converter);
		if (hr != hrSuccess)
			return hr;
		break;
	case RES_COMMENT:
		if (lpRestriction->res.resComment.lpRes) {
			hr = ConvertString8ToUnicode(lpRestriction->res.resComment.lpRes, base, converter);
			if (hr != hrSuccess)
				return hr;
		}
		for (i = 0; i < lpRestriction->res.resComment.cValues; ++i) {
			if (PROP_TYPE(lpRestriction->res.resComment.lpProp[i].ulPropTag) == PT_STRING8) {
				hr = ConvertString8ToUnicode(lpRestriction->res.resComment.lpProp[i].Value.lpszA, &lpRestriction->res.resComment.lpProp[i].Value.lpszW, base, converter);
				if (hr != hrSuccess)
					return hr;
				lpRestriction->res.resComment.lpProp[i].ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resComment.lpProp[i].ulPropTag, PT_UNICODE);
			}
		}
		break;
	case RES_COMPAREPROPS:
		break;
	case RES_CONTENT:
		if (PROP_TYPE(lpRestriction->res.resContent.ulPropTag) == PT_STRING8) {
			hr = ConvertString8ToUnicode(lpRestriction->res.resContent.lpProp->Value.lpszA, &lpRestriction->res.resContent.lpProp->Value.lpszW, base, converter);
			if (hr != hrSuccess)
				return hr;
			lpRestriction->res.resContent.lpProp->ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resContent.lpProp->ulPropTag, PT_UNICODE);
			lpRestriction->res.resContent.ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resContent.ulPropTag, PT_UNICODE);
		}
		break;
	case RES_PROPERTY:
		if (PROP_TYPE(lpRestriction->res.resProperty.ulPropTag) == PT_STRING8) {
			hr = ConvertString8ToUnicode(lpRestriction->res.resProperty.lpProp->Value.lpszA, &lpRestriction->res.resProperty.lpProp->Value.lpszW, base, converter);
			if (hr != hrSuccess)
				return hr;
			lpRestriction->res.resProperty.lpProp->ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resProperty.lpProp->ulPropTag, PT_UNICODE);
			lpRestriction->res.resProperty.ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resProperty.ulPropTag, PT_UNICODE);
		}
		break;
	case RES_SUBRESTRICTION:
		hr = ConvertString8ToUnicode(lpRestriction->res.resSub.lpRes, base, converter);
		if (hr != hrSuccess)
			return hr;
		break;
	};
	return hrSuccess;
}

static HRESULT ConvertString8ToUnicode(const ADRLIST *lpAdrList, void *base,
    convert_context &converter)
{
	if (lpAdrList == NULL)
		return hrSuccess;

	for (ULONG c = 0; c < lpAdrList->cEntries; ++c) {
		// treat as row
		auto hr = ConvertString8ToUnicode((LPSRow)&lpAdrList->aEntries[c], base, converter);
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
	HRESULT hr = hrSuccess;

	if (lpRow == NULL)
		return hrSuccess;

	for (ULONG c = 0; c < lpRow->cValues; ++c) {
		if (PROP_TYPE(lpRow->lpProps[c].ulPropTag) == PT_SRESTRICTION) {
			hr = ConvertString8ToUnicode((LPSRestriction)lpRow->lpProps[c].Value.lpszA, base ? base : lpRow->lpProps, converter);
		} else if (PROP_TYPE(lpRow->lpProps[c].ulPropTag) == PT_ACTIONS) {
			hr = ConvertString8ToUnicode((ACTIONS*)lpRow->lpProps[c].Value.lpszA, base ? base : lpRow->lpProps, converter);
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
