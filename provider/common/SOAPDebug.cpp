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
#include "SOAPDebug.h"
#include <kopano/kcodes.h>
#include <kopano/ECDebug.h>

#include <edkmdb.h>
#include <mapidefs.h>
#include <kopano/stringutil.h>

using namespace KC::string_literals;

namespace KC {

std::string RestrictionToString(const restrictTable *lpRestriction,
    unsigned int indent)
{
	std::string strResult;
	unsigned int j = 0;

	if(lpRestriction == NULL)
		return "NULL";

	for (j = 0; j < indent; ++j)
		strResult += "  ";

	switch(lpRestriction->ulType)
	{
	case RES_OR:
		strResult = "RES_OR:\n";
		for (gsoap_size_t i = 0; i < lpRestriction->lpOr->__size; ++i) {
			for (j = 0; j < indent + 1; ++j)
				strResult += "  ";
			strResult += "Restriction: " + RestrictionToString(lpRestriction->lpOr->__ptr[i], indent + 1) + "\n";
		}
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		return strResult += "---or---\n";
	case RES_AND:
		strResult = "RES_AND:\n";
		for (gsoap_size_t i = 0; i < lpRestriction->lpAnd->__size; ++i) {
			for (j = 0; j < indent + 1; ++j)
				strResult += "  ";
			strResult += "Restriction: " + RestrictionToString(lpRestriction->lpAnd->__ptr[i], indent + 1);
		}
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		return strResult += "---and---\n";
	case RES_BITMASK:
		strResult = "RES_BITMASK:\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		switch (lpRestriction->lpBitmask->ulType) {
		case BMR_EQZ:
			strResult += "BMR: R_EQZ\n";
			break;
		case BMR_NEZ:
			strResult += "BMR: R_NEZ\n";
			break;
		default:
			strResult += "BMR: Not specified(" + stringify(lpRestriction->lpBitmask->ulType) + ")\n";
			break;
		}
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		strResult += "proptag: " + PropNameFromPropTag(lpRestriction->lpBitmask->ulPropTag) + "\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		return strResult += "mask: " + stringify(lpRestriction->lpBitmask->ulMask) + "\n";
	case RES_COMMENT:
		strResult = "RES_COMMENT:\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		strResult += "props: " + PropNameFromPropArray(lpRestriction->lpComment->sProps.__size, lpRestriction->lpComment->sProps.__ptr) + "\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		return strResult += "restriction: " + RestrictionToString(lpRestriction->lpComment->lpResTable, indent + 1) + "\n";
	case RES_COMPAREPROPS:
		strResult = "RES_COMPAREPROPS:\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		strResult += "relop: ";
		strResult += RelationalOperatorToString(lpRestriction->lpCompare->ulType);
		strResult += "\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		strResult += "proptag1: " + PropNameFromPropTag(lpRestriction->lpCompare->ulPropTag1) + "\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		return strResult += "proptag2: " + PropNameFromPropTag(lpRestriction->lpCompare->ulPropTag2) + "\n";
	case RES_CONTENT:
		strResult = "RES_CONTENT:\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		strResult += "FuzzyLevel: " + FuzzyLevelToString(lpRestriction->lpContent->ulFuzzyLevel) + "\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		strResult += "proptag: " + PropNameFromPropTag(lpRestriction->lpContent->ulPropTag) + "\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		return strResult += "props: " + PropNameFromPropArray(1, lpRestriction->lpContent->lpProp) + "\n";
	case RES_EXIST:
		strResult = "RES_EXIST:\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		return strResult += "proptag: " + PropNameFromPropTag(lpRestriction->lpExist->ulPropTag) + "\n";
	case RES_NOT:
		strResult = "RES_NOT:\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		return strResult += "restriction: " + RestrictionToString(lpRestriction->lpNot->lpNot, indent + 1) + "\n";
	case RES_PROPERTY:
		strResult = "RES_PROPERTY:\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		strResult += "relop: ";
		strResult += RelationalOperatorToString(lpRestriction->lpProp->ulType);
		strResult += "\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		strResult += "proptag: " + PropNameFromPropTag(lpRestriction->lpProp->ulPropTag) + ((lpRestriction->lpProp->ulPropTag & MV_FLAG) ? " (MV_PROP)" : "") + "\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		return strResult += "props: " + PropNameFromPropArray(1, lpRestriction->lpProp->lpProp) + ((lpRestriction->lpProp->lpProp->ulPropTag & MV_FLAG) ? " (MV_PROP)" : "") + "\n";
	case RES_SIZE:
		strResult = "RES_SIZE:\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		strResult += "relop: ";
		strResult += RelationalOperatorToString(lpRestriction->lpSize->ulType);
		strResult += "\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		strResult += "proptag: " + PropNameFromPropTag(lpRestriction->lpSize->ulPropTag) + "\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		return strResult += "sizeofprop: " + stringify(lpRestriction->lpSize->cb) + "\n";
	case RES_SUBRESTRICTION:
		strResult = "RES_SUBRESTRICTION:\n";
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		switch (lpRestriction->lpSub->ulSubObject) {
		case PR_MESSAGE_RECIPIENTS:
			strResult += "subobject: PR_MESSAGE_RECIPIENTS\n";
			break;
		case PR_MESSAGE_ATTACHMENTS:
			strResult += "subobject: PR_MESSAGE_ATTACHMENTS\n";
			break;
		default:
			strResult += "subobject: Not specified(" + stringify(lpRestriction->lpSub->ulSubObject) + ")\n";
			break;
		}
		for (j = 0; j < indent; ++j)
			strResult += "  ";
		return strResult += "Restriction: " + RestrictionToString(lpRestriction->lpSub->lpSubObject, indent + 1) + "\n";
	default:
		return "UNKNOWN TYPE:\n";
	}
}

std::string PropNameFromPropArray(unsigned int cValues,
    const propVal *lpPropArray)
{
	std::string data;
	
	if(lpPropArray == NULL)
		return "NULL";
	else if(cValues == 0)
		return "EMPTY";

	for (unsigned int i = 0; i < cValues; ++i) {
		if(i>0)
			data+=", ";

		data += PropNameFromPropTag(lpPropArray[i].ulPropTag);
		data += ": ";
		data += PropValueToString(&lpPropArray[i]);
		data += "\n";
		
	}

	return data;
}

std::string PropValueToString(const propVal *lpPropValue)
{
	std::string strResult;

	if(lpPropValue == NULL)
		return "NULL";

	switch(PROP_TYPE(lpPropValue->ulPropTag)) {	
	case PT_I2:
		return "PT_I2: " + stringify(lpPropValue->Value.i);
	case PT_LONG:
		return "PT_LONG: " + stringify(lpPropValue->Value.ul);
	case PT_BOOLEAN:
		return "PT_BOOLEAN: " + stringify(lpPropValue->Value.b);
	case PT_R4:
		return "PT_R4: " + stringify_float(lpPropValue->Value.flt);
	case PT_DOUBLE:
		return "PT_DOUBLE: " + stringify_double(lpPropValue->Value.dbl);
	case PT_APPTIME:
		return "PT_APPTIME: " + stringify_double(lpPropValue->Value.dbl);
	case PT_CURRENCY:
		return "PT_CURRENCY: lo=" + stringify(lpPropValue->Value.hilo->lo) + " hi=" + stringify(lpPropValue->Value.hilo->hi);
	case PT_SYSTIME:
		//strResult = "PT_SYSTIME: fth="+stringify(lpPropValue->Value.hilo->hi)+" ftl="+stringify(lpPropValue->Value.hilo->lo);
		{
			auto t = FileTimeToUnixTime({lpPropValue->Value.hilo->lo, static_cast<DWORD>(lpPropValue->Value.hilo->hi)});
			return "PT_SYSTIME: "s + ctime(&t);
		}
		break;
	case PT_I8:
		return "PT_I8: " + stringify_int64(lpPropValue->Value.li);
	case PT_UNICODE:
		return "PT_UNICODE: " + (lpPropValue->Value.lpszA ? (std::string)lpPropValue->Value.lpszA : std::string("NULL"));
	case PT_STRING8:
		return "PT_STRING8: " + (lpPropValue->Value.lpszA ? (std::string)lpPropValue->Value.lpszA : std::string("NULL"));
	case PT_BINARY:
		strResult = "PT_BINARY: cb=" + stringify(lpPropValue->Value.bin->__size);
		return strResult += " Data=" + (lpPropValue->Value.bin->__ptr ? bin2hex(lpPropValue->Value.bin->__size, lpPropValue->Value.bin->__ptr) : std::string("NULL"));
	case PT_CLSID:
		return "PT_CLSID: (Skip)";
	case PT_NULL:
		return "PT_NULL: ";
	case PT_UNSPECIFIED:
		return "PT_UNSPECIFIED: ";
	case PT_ERROR:
		return "PT_ERROR: " + stringify(lpPropValue->Value.ul, true);
	case PT_SRESTRICTION:
		return "PT_SRESTRICTION: structure...";
	case PT_ACTIONS:
		return "PT_ACTIONS: structure...";
	case PT_OBJECT:
		return "<OBJECT>";
	case PT_MV_I2:
		return "PT_MV_I2[" + stringify(lpPropValue->Value.mvi.__size) + "]";
	case PT_MV_LONG:
		return "PT_MV_LONG[" + stringify(lpPropValue->Value.mvl.__size) + "]";
	case PT_MV_R4:
		return "PT_MV_R4[" + stringify(lpPropValue->Value.mvflt.__size) + "]";
	case PT_MV_DOUBLE:
		return "PT_MV_DOUBLE[" + stringify(lpPropValue->Value.mvdbl.__size) + "]";
	case PT_MV_APPTIME:
		return "PT_MV_APPTIME[" + stringify(lpPropValue->Value.mvbin.__size) + "]";
	case PT_MV_CURRENCY:
		return "PT_MV_CURRENCY[" + stringify(lpPropValue->Value.mvbin.__size) + "]";
	case PT_MV_SYSTIME:
		return "PT_MV_SYSTIME[" + stringify(lpPropValue->Value.mvbin.__size) + "]";
	case PT_MV_I8:
		return "PT_MV_I8[" + stringify(lpPropValue->Value.mvli.__size) + "]";
	case PT_MV_UNICODE:
		strResult = "PT_MV_UNICODE[" + stringify(lpPropValue->Value.mvszA.__size) + "]" + "\n";
		for (gsoap_size_t i = 0; i < lpPropValue->Value.mvszA.__size; ++i)
			strResult += std::string("\t") + lpPropValue->Value.mvszA.__ptr[i] + "\n";
		return strResult;
	case PT_MV_STRING8:
		strResult = "PT_MV_STRING8[" + stringify(lpPropValue->Value.mvszA.__size) + "]" + "\n";
		for (gsoap_size_t i = 0; i < lpPropValue->Value.mvszA.__size; ++i)
			strResult += std::string("\t") + lpPropValue->Value.mvszA.__ptr[i] + "\n";
		return strResult;
	case PT_MV_BINARY:
		return "PT_MV_BINARY[" + stringify(lpPropValue->Value.mvbin.__size) + "]";
	case PT_MV_CLSID:
		return "PT_MV_CLSID[" + stringify(lpPropValue->Value.mvbin.__size) + "]";
	default:
		return "<UNKNOWN>";
	}
}

const char* RightsToString(unsigned int ulecRights)
{
	switch (ulecRights) {
	case(ecSecurityRead):
		return "read";
	case(ecSecurityCreate):
		return "create";
	case(ecSecurityEdit):
		return "edit";
	case(ecSecurityDelete):
		return "delete";
	case(ecSecurityCreateFolder):
		return "change hierarchy";
	case(ecSecurityFolderVisible):
		return "view";
	case(ecSecurityFolderAccess):
		return "folder permissions";
	case(ecSecurityOwner):
		return "owner";
	case(ecSecurityAdmin):
		return "admin";
	default:
		return "none";
	};
}

} /* namespace */
