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

#include "phpconfig.h"

#include <kopano/platform.h>
#include <kopano/ecversion.h>
#include <cstdio>
#include <ctime>
#include <cmath>

extern "C" {
	// Remove these defines to remove warnings
	#undef PACKAGE_VERSION
	#undef PACKAGE_TARNAME
	#undef PACKAGE_NAME
	#undef PACKAGE_STRING
	#undef PACKAGE_BUGREPORT
	
	#include "php.h"
   	#include "php_globals.h"
	#include "ext/standard/info.h"
	#include "ext/standard/php_string.h"
}



// A very, very nice PHP #define that causes link errors in MAPI when you have multiple
// files referencing MAPI....
#undef inline

/***************************************************************
* MAPI Includes
***************************************************************/

#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <mapispi.h>
#include <mapitags.h>
#include <mapicode.h>

#define USES_IID_IMAPIProp
#define USES_IID_IMAPIContainer
#define USES_IID_IMsgStore
#define USES_IID_IMessage
#define USES_IID_IExchangeManageStore

#include <edkguid.h>
#include <edkmdb.h>
#include "ECImportContentsChangesProxy.h"
#include "typeconversion.h"

static char *name_mapi_message;
static int le_mapi_message;

ECImportContentsChangesProxy::ECImportContentsChangesProxy(zval *lpObj TSRMLS_DC) {
    this->m_cRef = 1; // Object is in use when created!
    this->m_lpObj = lpObj;
#if ZEND_MODULE_API_NO >= 20071006
    Z_ADDREF_P(m_lpObj);
#else
    ZVAL_ADDREF(m_lpObj);
#endif
#ifdef ZTS
	this->TSRMLS_C = TSRMLS_C;
#endif
}

ECImportContentsChangesProxy::~ECImportContentsChangesProxy() {
    zval_ptr_dtor(m_lpObj);
}

ULONG 	ECImportContentsChangesProxy::AddRef() {
	return ++m_cRef;
}

ULONG	ECImportContentsChangesProxy::Release() {
	if (--m_cRef == 0) {
        delete this;
        return 0;
    }
        
    return m_cRef;
}

HRESULT ECImportContentsChangesProxy::QueryInterface(REFIID iid, void **lpvoid) {
    if(iid == IID_IExchangeImportContentsChanges) {
        AddRef();
        *lpvoid = this;
        return hrSuccess;
    }
    return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECImportContentsChangesProxy::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT ECImportContentsChangesProxy::Config(LPSTREAM lpStream, ULONG ulFlags) {
    HRESULT hr = hrSuccess;
    
    zval pvalFuncName;
    zval pvalReturn;
    zval pvalArgs[2];
    
    if(lpStream) {
	Z_LVAL_P(&pvalArgs[0]) = (long)lpStream;
	Z_TYPE_INFO_P(&pvalArgs[0]) = IS_RESOURCE;
    } else {
        ZVAL_NULL(&pvalArgs[0]);
    }
    
    ZVAL_LONG(&pvalArgs[1], ulFlags);
    
    ZVAL_STRING(&pvalFuncName, "Config");
    
    if(call_user_function(NULL, m_lpObj, &pvalFuncName, &pvalReturn, 2, pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "Config method not present on ImportContentsChanges object");
        hr = MAPI_E_CALL_FAILED;
        goto exit;
    }
    
    convert_to_long_ex(&pvalReturn);
    
    hr = pvalReturn.value.lval;

exit:
    return hr;
}

HRESULT ECImportContentsChangesProxy::UpdateState(LPSTREAM lpStream) {
    HRESULT hr = hrSuccess;
    
    zval pvalFuncName;
    zval pvalReturn;
    zval pvalArgs[1];
    
    if(lpStream) {
	Z_LVAL_P(&pvalArgs[0]) = (long)lpStream;
	Z_TYPE_INFO_P(&pvalArgs[0]) = IS_RESOURCE;
    } else {
        ZVAL_NULL(&pvalArgs[0]);
    }
    
    ZVAL_STRING(&pvalFuncName, "UpdateState");
    
    if(call_user_function(NULL, m_lpObj, &pvalFuncName, &pvalReturn, 1, pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "UpdateState method not present on ImportContentsChanges object");
        hr = MAPI_E_CALL_FAILED;
        goto exit;
    }
    
    convert_to_long_ex(&pvalReturn);
    
    hr = pvalReturn.value.lval;

exit:
    return hr;
}

HRESULT ECImportContentsChangesProxy::ImportMessageChange(ULONG cValues, LPSPropValue lpPropArray, ULONG ulFlags, LPMESSAGE * lppMessage)  {
    zval pvalFuncName;
    zval pvalReturn;
    zval pvalArgs[3];

    IMessage *lpMessage = NULL;
    HRESULT hr = hrSuccess;

    hr = PropValueArraytoPHPArray(cValues, lpPropArray, &pvalArgs[0] TSRMLS_CC);
    if(hr != hrSuccess) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to convert MAPI propvalue array to PHP");
        goto exit;
    }
        
    ZVAL_LONG(&pvalArgs[1], ulFlags);
    ZVAL_NULL(&pvalArgs[2]);
    
    ZVAL_STRING(&pvalFuncName, "ImportMessageChange");
    
    if(call_user_function(NULL, m_lpObj, &pvalFuncName, &pvalReturn, 3, pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ImportMessageChange method not present on ImportContentsChanges object");
        hr = MAPI_E_CALL_FAILED;
        goto exit;
    }
        
    convert_to_long_ex(&pvalReturn);
    
    hr = pvalReturn.value.lval;
    
    if(hr != hrSuccess)
        goto exit;

    lpMessage = (IMessage *) zend_fetch_resource(Z_RES_P(&pvalReturn) TSRMLS_CC, name_mapi_message, le_mapi_message);
        
    if(!lpMessage) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ImportMessageChange() must return a valid MAPI message resource in the last argument when returning OK (0)");
        hr = MAPI_E_CALL_FAILED;
        goto exit;
    }         
    
    if(lppMessage)
        *lppMessage = lpMessage;
           
exit:
    return hr;
}

HRESULT ECImportContentsChangesProxy::ImportMessageDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList) {
    HRESULT hr = hrSuccess;
    
    zval pvalFuncName;
    zval pvalReturn;
    zval pvalArgs[2];
    
    ZVAL_LONG(&pvalArgs[0], ulFlags);
    SBinaryArraytoPHPArray(lpSourceEntryList, &pvalArgs[1] TSRMLS_CC);

    ZVAL_STRING(&pvalFuncName, "ImportMessageDeletion");
    
    if(call_user_function(NULL, m_lpObj, &pvalFuncName, &pvalReturn, 2, pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ImportMessageDeletion method not present on ImportContentsChanges object");
        hr = MAPI_E_CALL_FAILED;
        goto exit;
    }
    
    convert_to_long_ex(&pvalReturn);
    
    hr = pvalReturn.value.lval;

exit:
    return hr;
}

HRESULT ECImportContentsChangesProxy::ImportPerUserReadStateChange(ULONG cElements, LPREADSTATE lpReadState) {
    HRESULT hr = hrSuccess;
    
    zval pvalFuncName;
    zval pvalReturn;
    zval pvalArgs[1];
    
    ReadStateArraytoPHPArray(cElements, lpReadState, &pvalArgs[0] TSRMLS_CC);

    ZVAL_STRING(&pvalFuncName, "ImportPerUserReadStateChange");
    
    if(call_user_function(NULL, m_lpObj, &pvalFuncName, &pvalReturn, 1, pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ImportPerUserReadStateChange method not present on ImportContentsChanges object");
        hr = MAPI_E_CALL_FAILED;
        goto exit;
    }
    
    convert_to_long_ex(&pvalReturn);
    
    hr = pvalReturn.value.lval;

exit:
    return hr;
}

HRESULT ECImportContentsChangesProxy::ImportMessageMove(ULONG cbSourceKeySrcFolder, BYTE FAR * pbSourceKeySrcFolder, ULONG cbSourceKeySrcMessage, BYTE FAR * pbSourceKeySrcMessage, ULONG cbPCLMessage, BYTE FAR * pbPCLMessage, ULONG cbSourceKeyDestMessage, BYTE FAR * pbSourceKeyDestMessage, ULONG cbChangeNumDestMessage, BYTE FAR * pbChangeNumDestMessage) {
    return MAPI_E_NO_SUPPORT;
}
