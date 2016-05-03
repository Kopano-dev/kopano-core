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

#include <kopano/platform.h>
#include "SOAPDebug.h"
#include <kopano/kcodes.h>
#include <kopano/ECDebug.h>

#include <edkmdb.h>
#include <mapidefs.h>
#include <kopano/stringutil.h>

using namespace std;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

std::string RestrictionToString(const restrictTable *lpRestriction,
    unsigned int indent)
{
	std::string strResult;
	unsigned int i = 0;
	unsigned int j = 0;

	if(lpRestriction == NULL)
		return "NULL";

	for (j = 0; j < indent; ++j)
		strResult += "  ";

	switch(lpRestriction->ulType)
	{
		case RES_OR:
			strResult = "RES_OR:\n";
			for (i = 0; i < lpRestriction->lpOr->__size; ++i) {
				for (j = 0; j < indent + 1; ++j)
					strResult += "  ";
				strResult += "Restriction: "+ RestrictionToString(lpRestriction->lpOr->__ptr[i], indent+1)+"\n";
			}
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "---or---\n";
			break;
		case RES_AND:
			strResult = "RES_AND:\n";
			for (i = 0; i < lpRestriction->lpAnd->__size; ++i) {
				for (j = 0; j < indent + 1; ++j)
					strResult += "  ";
				strResult += "Restriction: " + RestrictionToString(lpRestriction->lpAnd->__ptr[i], indent+1);
			}
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "---and---\n";
			break;

		case RES_BITMASK:
			strResult = "RES_BITMASK:\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			switch(lpRestriction->lpBitmask->ulType){
				case BMR_EQZ:
					strResult+= "BMR: R_EQZ\n";
					break;
				case BMR_NEZ:
					strResult+= "BMR: R_NEZ\n";
					break;
				default:
					strResult+= "BMR: Not specified("+stringify(lpRestriction->lpBitmask->ulType)+")\n";
					break;
			}
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "proptag: "+PropNameFromPropTag(lpRestriction->lpBitmask->ulPropTag)+"\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "mask: "+stringify(lpRestriction->lpBitmask->ulMask)+"\n";
			break;
		case RES_COMMENT:
			strResult = "RES_COMMENT:\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "props: " + PropNameFromPropArray(lpRestriction->lpComment->sProps.__size, lpRestriction->lpComment->sProps.__ptr)+"\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "restriction: "+ RestrictionToString(lpRestriction->lpComment->lpResTable, indent+1)+"\n";
			break;
		case RES_COMPAREPROPS:
			strResult = "RES_COMPAREPROPS:\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "relop: ";
			strResult += RelationalOperatorToString(lpRestriction->lpCompare->ulType);
			strResult += "\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "proptag1: "+PropNameFromPropTag(lpRestriction->lpCompare->ulPropTag1)+"\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "proptag2: "+PropNameFromPropTag(lpRestriction->lpCompare->ulPropTag2)+"\n";
			break;
		case RES_CONTENT:
			strResult = "RES_CONTENT:\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "FuzzyLevel: "+FuzzyLevelToString(lpRestriction->lpContent->ulFuzzyLevel)+"\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "proptag: "+PropNameFromPropTag(lpRestriction->lpContent->ulPropTag)+"\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "props: " + PropNameFromPropArray(1, lpRestriction->lpContent->lpProp)+"\n";
			break;
		case RES_EXIST:
			strResult = "RES_EXIST:\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "proptag: "+PropNameFromPropTag(lpRestriction->lpExist->ulPropTag)+"\n";
			break;
		case RES_NOT:
			strResult = "RES_NOT:\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "restriction: "+ RestrictionToString(lpRestriction->lpNot->lpNot, indent+1)+"\n";
			break;
		case RES_PROPERTY:
			strResult = "RES_PROPERTY:\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "relop: ";
			strResult += RelationalOperatorToString(lpRestriction->lpProp->ulType);
			strResult += "\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "proptag: "+PropNameFromPropTag(lpRestriction->lpProp->ulPropTag)+((lpRestriction->lpProp->ulPropTag&MV_FLAG)?" (MV_PROP)":"")+"\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "props: " + PropNameFromPropArray(1, lpRestriction->lpProp->lpProp)+((lpRestriction->lpProp->lpProp->ulPropTag&MV_FLAG)?" (MV_PROP)":"")+"\n";
			break;
		case RES_SIZE:
			strResult = "RES_SIZE:\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "relop: ";
			strResult += RelationalOperatorToString(lpRestriction->lpSize->ulType);
			strResult += "\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "proptag: "+PropNameFromPropTag(lpRestriction->lpSize->ulPropTag)+"\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "sizeofprop: "+ stringify(lpRestriction->lpSize->cb) + "\n";
			break;
		case RES_SUBRESTRICTION:
			strResult = "RES_SUBRESTRICTION:\n";
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			switch(lpRestriction->lpSub->ulSubObject) {
				case PR_MESSAGE_RECIPIENTS:
					strResult+= "subobject: PR_MESSAGE_RECIPIENTS\n";
					break;
				case PR_MESSAGE_ATTACHMENTS:
					strResult+= "subobject: PR_MESSAGE_ATTACHMENTS\n";
					break;
				default:
					strResult += "subobject: Not specified("+stringify(lpRestriction->lpSub->ulSubObject)+")\n";
					break;
			}
			for (j = 0; j < indent; ++j)
				strResult += "  ";
			strResult += "Restriction: "+ RestrictionToString(lpRestriction->lpSub->lpSubObject, indent+1)+"\n";
			break;
		default:
			strResult = "UNKNOWN TYPE:\n";
			break;
	}

	return strResult;
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
			strResult = "PT_I2: "+stringify(lpPropValue->Value.i);
			break;
		case PT_LONG:
			strResult = "PT_LONG: "+stringify(lpPropValue->Value.ul);
			break;
		case PT_BOOLEAN:
			strResult = "PT_BOOLEAN: "+stringify(lpPropValue->Value.b);
			break;
		case PT_R4:
			strResult = "PT_R4: "+stringify_float(lpPropValue->Value.flt);
			break;
		case PT_DOUBLE:
			strResult = "PT_DOUBLE: "+stringify_double(lpPropValue->Value.dbl);
			break;
		case PT_APPTIME:
			strResult = "PT_APPTIME: "+stringify_double(lpPropValue->Value.dbl);
			break;
		case PT_CURRENCY:
			strResult = "PT_CURRENCY: lo="+stringify(lpPropValue->Value.hilo->lo)+" hi="+stringify(lpPropValue->Value.hilo->hi);
			break;
		case PT_SYSTIME:
			//strResult = "PT_SYSTIME: fth="+stringify(lpPropValue->Value.hilo->hi)+" ftl="+stringify(lpPropValue->Value.hilo->lo);
			{
				time_t t = FileTimeToUnixTime(lpPropValue->Value.hilo->hi, lpPropValue->Value.hilo->lo);
				strResult = (string)"PT_SYSTIME: " + ctime(&t);
			}
			break;
		case PT_I8:
			strResult = "PT_I8: " + stringify_int64(lpPropValue->Value.li);
			break;
		case PT_UNICODE:
			strResult = "PT_UNICODE: " + ((lpPropValue->Value.lpszA)?(std::string)lpPropValue->Value.lpszA:std::string("NULL"));
			break;
		case PT_STRING8:
			strResult = "PT_STRING8: " + ((lpPropValue->Value.lpszA)?(std::string)lpPropValue->Value.lpszA:std::string("NULL"));
			break;
		case PT_BINARY:
			strResult = "PT_BINARY: cb="+stringify(lpPropValue->Value.bin->__size);
			strResult+= " Data="+((lpPropValue->Value.bin->__ptr)?bin2hex(lpPropValue->Value.bin->__size, lpPropValue->Value.bin->__ptr) : std::string("NULL"));
			break;
		case PT_CLSID:
			strResult = "PT_CLSID: (Skip)";
			break;
		case PT_NULL:
			strResult = "PT_NULL: ";
			break;
		case PT_UNSPECIFIED:
			strResult = "PT_UNSPECIFIED: ";
			break;
		case PT_ERROR:
			strResult = "PT_ERROR: " + stringify(lpPropValue->Value.ul, true);
			break;
		case PT_SRESTRICTION:
			strResult = "PT_SRESTRICTION: structure...";
			break;
		case PT_ACTIONS:
			strResult = "PT_ACTIONS: structure...";
			break;
		case PT_OBJECT:
			strResult = "<OBJECT>";
			break;
		case PT_MV_I2:
			strResult = "PT_MV_I2[" + stringify(lpPropValue->Value.mvi.__size) + "]";
			break;
		case PT_MV_LONG:
			strResult = "PT_MV_LONG[" + stringify(lpPropValue->Value.mvi.__size) + "]";
			break;
		case PT_MV_R4:
			strResult = "PT_MV_R4[" + stringify(lpPropValue->Value.mvi.__size) + "]";
			break;
		case PT_MV_DOUBLE:
			strResult = "PT_MV_DOUBLE[" + stringify(lpPropValue->Value.mvi.__size) + "]";
			break;
		case PT_MV_APPTIME:
			strResult = "PT_MV_APPTIME[" + stringify(lpPropValue->Value.mvi.__size) + "]";
			break;
		case PT_MV_CURRENCY:
			strResult = "PT_MV_CURRENCY[" + stringify(lpPropValue->Value.mvi.__size) + "]";
			break;
		case PT_MV_SYSTIME:
			strResult = "PT_MV_SYSTIME[" + stringify(lpPropValue->Value.mvi.__size) + "]";
			break;
		case PT_MV_I8:
			strResult = "PT_MV_I8[" + stringify(lpPropValue->Value.mvi.__size) + "]";
			break;
		case PT_MV_UNICODE:
			strResult = "PT_MV_UNICODE[" + stringify(lpPropValue->Value.mvi.__size) + "]" + "\n";
			for (int i = 0; i < lpPropValue->Value.mvi.__size; ++i)
				strResult += std::string("\t") + lpPropValue->Value.mvszA.__ptr[i] + "\n";
			break;
		case PT_MV_STRING8:
			strResult = "PT_MV_STRING8[" + stringify(lpPropValue->Value.mvi.__size) + "]" + "\n";
			for (int i = 0; i < lpPropValue->Value.mvi.__size; ++i)
				strResult += std::string("\t") + lpPropValue->Value.mvszA.__ptr[i] + "\n";
			break;
		case PT_MV_BINARY:
			strResult = "PT_MV_BINARY[" + stringify(lpPropValue->Value.mvi.__size) + "]";
			break;
		case PT_MV_CLSID:
			strResult = "PT_MV_CLSID[" + stringify(lpPropValue->Value.mvi.__size) + "]";
			break;
		default:
			strResult = "<UNKNOWN>";
			break;
	}

	return strResult;
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
