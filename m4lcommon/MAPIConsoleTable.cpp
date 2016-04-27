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

#include "MAPIConsoleTable.h"
#include "ConsoleTable.h"
#include <kopano/mapi_ptr.h>
#include <kopano/mapi_ptr/mapi_rowset_ptr.h>
#include <kopano/stringutil.h>

static std::string ToString(const SPropValue *lpProp)
{
    switch(PROP_TYPE(lpProp->ulPropTag)) {
        case PT_STRING8:
            return std::string(lpProp->Value.lpszA);
        case PT_LONG:
            return stringify(lpProp->Value.ul);
        case PT_DOUBLE:
            return stringify(lpProp->Value.dbl);
        case PT_FLOAT:
            return stringify(lpProp->Value.flt);
        case PT_I8:
            return stringify_int64(lpProp->Value.li.QuadPart);
        case PT_SYSTIME:
        {
            time_t t;
            char buf[32]; // must be at least 26 bytes
            FileTimeToUnixTime(lpProp->Value.ft, &t);
            ctime_r(&t, buf);
            return trim(buf, " \t\n\r\v\f");
        }
        case PT_MV_STRING8:
        {
            std::string s;
            for (unsigned int i = 0; i < lpProp->Value.MVszA.cValues; ++i) {
                if(!s.empty())
                    s += ",";
                s += lpProp->Value.MVszA.lppszA[i];
            }
            
            return s;
        }
            
    }
    
    return std::string();
}

HRESULT MAPITablePrint(IMAPITable *lpTable, bool humanreadable /* = true */)
{
    HRESULT hr = hrSuccess;
    SPropTagArrayPtr ptrColumns;
    SRowSetPtr ptrRows;
    ConsoleTable ct(0, 0);
    unsigned int i = 0, j = 0;
    
    hr = lpTable->QueryColumns(0, &ptrColumns);
    if(hr != hrSuccess)
        goto exit;
        
    hr = lpTable->QueryRows(-1, 0, &ptrRows);
    if(hr != hrSuccess)
        goto exit;
        
    ct.Resize(ptrRows.size(), ptrColumns->cValues);
    
	for (i = 0; i < ptrColumns->cValues; ++i)
		ct.SetHeader(i, stringify(ptrColumns->aulPropTag[i], true));
    
	for (i = 0; i < ptrRows.size(); ++i)
		for (j = 0; j < ptrRows[i].cValues; ++j)
			ct.SetColumn(i, j, ToString(&ptrRows[i].lpProps[j]));
    
	humanreadable ? ct.PrintTable() : ct.DumpTable();
        
exit:
    return hr;
}
