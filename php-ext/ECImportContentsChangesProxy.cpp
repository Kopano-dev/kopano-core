/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include "phpconfig.h"
#include <kopano/platform.h>
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
#include "main.h"

ECImportContentsChangesProxy::ECImportContentsChangesProxy(zval *lpObj TSRMLS_DC) :
	m_cRef(1), m_lpObj(lpObj)
{
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
    zval_ptr_dtor(&m_lpObj);
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
    
    zval *pvalFuncName;
    zval *pvalReturn;
    zval *pvalArgs[2];
    
    MAKE_STD_ZVAL(pvalFuncName);
    MAKE_STD_ZVAL(pvalReturn);
    
    MAKE_STD_ZVAL(pvalArgs[0]);
    MAKE_STD_ZVAL(pvalArgs[1]);
	if (lpStream != nullptr) {
		ZEND_REGISTER_RESOURCE(pvalArgs[0], lpStream, le_istream);
		if (Z_RESVAL_P(pvalArgs[0]))
			lpStream->AddRef();
	} else {
		ZVAL_NULL(pvalArgs[0]);
	}
    
    ZVAL_LONG(pvalArgs[1], ulFlags);
    
    ZVAL_STRING(pvalFuncName, "Config" , 1);
    
    if(call_user_function(NULL, &m_lpObj, pvalFuncName, pvalReturn, 2, pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "Config method not present on ImportContentsChanges object");
        hr = MAPI_E_CALL_FAILED;
        goto exit;
    }
    
    convert_to_long_ex(&pvalReturn);
    
    hr = pvalReturn->value.lval;

exit:
    zval_ptr_dtor(&pvalFuncName);
    zval_ptr_dtor(&pvalReturn);
    zval_ptr_dtor(&pvalArgs[0]);
    zval_ptr_dtor(&pvalArgs[1]);
    
    return hr;
}

HRESULT ECImportContentsChangesProxy::UpdateState(LPSTREAM lpStream) {
    HRESULT hr = hrSuccess;
    
    zval *pvalFuncName;
    zval *pvalReturn;
	zval *pvalArgs;
    
    MAKE_STD_ZVAL(pvalFuncName);
    MAKE_STD_ZVAL(pvalReturn);
	MAKE_STD_ZVAL(pvalArgs);
	if (lpStream != nullptr) {
		ZEND_REGISTER_RESOURCE(pvalArgs, lpStream, le_istream);
		if (Z_RESVAL_P(pvalArgs))
			lpStream->AddRef();
	} else {
		ZVAL_NULL(pvalArgs);
	}
    
    ZVAL_STRING(pvalFuncName, "UpdateState" , 1);
	if (call_user_function(nullptr, &m_lpObj, pvalFuncName, pvalReturn, 1, &pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "UpdateState method not present on ImportContentsChanges object");
        hr = MAPI_E_CALL_FAILED;
        goto exit;
    }
    
    convert_to_long_ex(&pvalReturn);
    
    hr = pvalReturn->value.lval;

exit:
    zval_ptr_dtor(&pvalFuncName);
    zval_ptr_dtor(&pvalReturn);
	zval_ptr_dtor(&pvalArgs);
    return hr;
}

HRESULT ECImportContentsChangesProxy::ImportMessageChange(ULONG cValues, LPSPropValue lpPropArray, ULONG ulFlags, LPMESSAGE * lppMessage)  {
    zval *pvalFuncName;
    zval *pvalReturn;
    zval *pvalArgs[3];
    IMessage *lpMessage = NULL;
    HRESULT hr = hrSuccess;
    
    MAKE_STD_ZVAL(pvalFuncName);
    MAKE_STD_ZVAL(pvalReturn);

    hr = PropValueArraytoPHPArray(cValues, lpPropArray, &pvalArgs[0] TSRMLS_CC);
    if(hr != hrSuccess) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to convert MAPI propvalue array to PHP");
        goto exit;
    }
    MAKE_STD_ZVAL(pvalArgs[1]);
    MAKE_STD_ZVAL(pvalArgs[2]);
        
    ZVAL_LONG(pvalArgs[1], ulFlags);
    ZVAL_NULL(pvalArgs[2]);
    
    ZVAL_STRING(pvalFuncName, "ImportMessageChange", 1);
    
    if(call_user_function(NULL, &m_lpObj, pvalFuncName, pvalReturn, 3, pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ImportMessageChange method not present on ImportContentsChanges object");
        hr = MAPI_E_CALL_FAILED;
        goto exit;
    }
        
    convert_to_long_ex(&pvalReturn);
    
    hr = pvalReturn->value.lval;
    
    if(hr != hrSuccess)
        goto exit;

    lpMessage = (IMessage *) zend_fetch_resource(&pvalReturn TSRMLS_CC, -1, name_mapi_message, NULL, 1, le_mapi_message);
        
    if(!lpMessage) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ImportMessageChange() must return a valid MAPI message resource in the last argument when returning OK (0)");
        hr = MAPI_E_CALL_FAILED;
        goto exit;
    }         
    
    if(lppMessage)
        *lppMessage = lpMessage;
           
exit:
    zval_ptr_dtor(&pvalFuncName);
    zval_ptr_dtor(&pvalReturn);
    zval_ptr_dtor(&pvalArgs[0]);
    zval_ptr_dtor(&pvalArgs[1]);
    zval_ptr_dtor(&pvalArgs[2]);
  
    return hr;
}

HRESULT ECImportContentsChangesProxy::ImportMessageDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList) {
    HRESULT hr = hrSuccess;
    
    zval *pvalFuncName;
    zval *pvalReturn;
    zval *pvalArgs[2];
    
    MAKE_STD_ZVAL(pvalFuncName);
    MAKE_STD_ZVAL(pvalReturn);
    
    MAKE_STD_ZVAL(pvalArgs[0]);

    ZVAL_LONG(pvalArgs[0], ulFlags);
    SBinaryArraytoPHPArray(lpSourceEntryList, &pvalArgs[1] TSRMLS_CC);

    ZVAL_STRING(pvalFuncName, "ImportMessageDeletion" , 1);
    
    if(call_user_function(NULL, &m_lpObj, pvalFuncName, pvalReturn, 2, pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ImportMessageDeletion method not present on ImportContentsChanges object");
        hr = MAPI_E_CALL_FAILED;
        goto exit;
    }
    
    convert_to_long_ex(&pvalReturn);
    
    hr = pvalReturn->value.lval;

exit:
    zval_ptr_dtor(&pvalFuncName);
    zval_ptr_dtor(&pvalReturn);
    zval_ptr_dtor(&pvalArgs[0]);
    zval_ptr_dtor(&pvalArgs[1]);
    
    return hr;
}

HRESULT ECImportContentsChangesProxy::ImportPerUserReadStateChange(ULONG cElements, LPREADSTATE lpReadState) {
    HRESULT hr = hrSuccess;
    
    zval *pvalFuncName;
    zval *pvalReturn;
	zval *pvalArgs;
    
    MAKE_STD_ZVAL(pvalFuncName);
    MAKE_STD_ZVAL(pvalReturn);
	ReadStateArraytoPHPArray(cElements, lpReadState, &pvalArgs TSRMLS_CC);
    ZVAL_STRING(pvalFuncName, "ImportPerUserReadStateChange" , 1);
	if (call_user_function(nullptr, &m_lpObj, pvalFuncName, pvalReturn, 1, &pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ImportPerUserReadStateChange method not present on ImportContentsChanges object");
        hr = MAPI_E_CALL_FAILED;
        goto exit;
    }
    
    convert_to_long_ex(&pvalReturn);
    
    hr = pvalReturn->value.lval;

exit:
    zval_ptr_dtor(&pvalFuncName);
    zval_ptr_dtor(&pvalReturn);
	zval_ptr_dtor(&pvalArgs);
    return hr;
}

HRESULT ECImportContentsChangesProxy::ImportMessageMove(ULONG cbSourceKeySrcFolder, BYTE *pbSourceKeySrcFolder, ULONG cbSourceKeySrcMessage, BYTE *pbSourceKeySrcMessage, ULONG cbPCLMessage, BYTE *pbPCLMessage, ULONG cbSourceKeyDestMessage, BYTE *pbSourceKeyDestMessage, ULONG cbChangeNumDestMessage, BYTE *pbChangeNumDestMessage) {
    return MAPI_E_NO_SUPPORT;
}

