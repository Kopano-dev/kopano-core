/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include "phpconfig.h"
#include <kopano/platform.h>
#include <kopano/ecversion.h>
#include <cstdio>
#include <ctime>
#include <cmath>
#if __GNUC_PREREQ(5, 0) && !__GNUC_PREREQ(6, 0)
using std::isfinite;
using std::isnan;
#endif

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

static char *name_mapi_message;
static int le_mapi_message;

ECImportContentsChangesProxy::ECImportContentsChangesProxy(const zval *v TSRMLS_DC) :
	m_cRef(1)
{
	ZVAL_OBJ(&m_lpObj, Z_OBJ_P(v));
	Z_ADDREF(m_lpObj);
}

ECImportContentsChangesProxy::~ECImportContentsChangesProxy() {
	Z_DELREF(m_lpObj);
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
	ZVAL_NULL(&pvalReturn);
    if (call_user_function(NULL, &m_lpObj, &pvalFuncName, &pvalReturn, 2, pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "Config method not present on ImportContentsChanges object");
        return MAPI_E_CALL_FAILED;
    }
    
    convert_to_long_ex(&pvalReturn);
    return pvalReturn.value.lval;
}

HRESULT ECImportContentsChangesProxy::UpdateState(LPSTREAM lpStream) {
    zval pvalFuncName;
    zval pvalReturn;
	zval pvalArgs;
    
    if(lpStream) {
		Z_LVAL_P(&pvalArgs) = reinterpret_cast<uintptr_t>(lpStream);
		Z_TYPE_INFO_P(&pvalArgs) = IS_RESOURCE;
    } else {
		ZVAL_NULL(&pvalArgs);
    }
    
    ZVAL_STRING(&pvalFuncName, "UpdateState");
	ZVAL_NULL(&pvalReturn);
	if (call_user_function(nullptr, &m_lpObj, &pvalFuncName, &pvalReturn, 1, &pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "UpdateState method not present on ImportContentsChanges object");
        return MAPI_E_CALL_FAILED;
    }
    
    convert_to_long_ex(&pvalReturn);
    return pvalReturn.value.lval;
}

HRESULT ECImportContentsChangesProxy::ImportMessageChange(ULONG cValues, LPSPropValue lpPropArray, ULONG ulFlags, LPMESSAGE * lppMessage)  {
    zval pvalFuncName;
    zval pvalReturn;
    zval pvalArgs[3];

    IMessage *lpMessage = NULL;
    HRESULT hr = PropValueArraytoPHPArray(cValues, lpPropArray, &pvalArgs[0] TSRMLS_CC);
    if(hr != hrSuccess) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to convert MAPI propvalue array to PHP");
        return hr;
    }
        
    ZVAL_LONG(&pvalArgs[1], ulFlags);
    ZVAL_NULL(&pvalArgs[2]);
    
    ZVAL_STRING(&pvalFuncName, "ImportMessageChange");
	ZVAL_NULL(&pvalReturn);
    if (call_user_function(NULL, &m_lpObj, &pvalFuncName, &pvalReturn, 3, pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ImportMessageChange method not present on ImportContentsChanges object");
        return MAPI_E_CALL_FAILED;
    }
        
    convert_to_long_ex(&pvalReturn);
    
    hr = pvalReturn.value.lval;
    
    if(hr != hrSuccess)
        return hr;
    lpMessage = (IMessage *) zend_fetch_resource(Z_RES_P(&pvalReturn) TSRMLS_CC, name_mapi_message, le_mapi_message);
        
    if(!lpMessage) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ImportMessageChange() must return a valid MAPI message resource in the last argument when returning OK (0)");
        return MAPI_E_CALL_FAILED;
    }         
    
    if(lppMessage)
        *lppMessage = lpMessage;
    return hrSuccess;
}

HRESULT ECImportContentsChangesProxy::ImportMessageDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList) {
    zval pvalFuncName;
    zval pvalReturn;
    zval pvalArgs[2];
    
    ZVAL_LONG(&pvalArgs[0], ulFlags);
    SBinaryArraytoPHPArray(lpSourceEntryList, &pvalArgs[1] TSRMLS_CC);

    ZVAL_STRING(&pvalFuncName, "ImportMessageDeletion");
	ZVAL_NULL(&pvalReturn);
    if (call_user_function(NULL, &m_lpObj, &pvalFuncName, &pvalReturn, 2, pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ImportMessageDeletion method not present on ImportContentsChanges object");
        return MAPI_E_CALL_FAILED;
    }
    
    convert_to_long_ex(&pvalReturn);
    return pvalReturn.value.lval;
}

HRESULT ECImportContentsChangesProxy::ImportPerUserReadStateChange(ULONG cElements, LPREADSTATE lpReadState) {
    zval pvalFuncName;
    zval pvalReturn;
	zval pvalArgs;
    
	ReadStateArraytoPHPArray(cElements, lpReadState, &pvalArgs TSRMLS_CC);
    ZVAL_STRING(&pvalFuncName, "ImportPerUserReadStateChange");
	ZVAL_NULL(&pvalReturn);
	if (call_user_function(nullptr, &m_lpObj, &pvalFuncName, &pvalReturn, 1, &pvalArgs TSRMLS_CC) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ImportPerUserReadStateChange method not present on ImportContentsChanges object");
        return MAPI_E_CALL_FAILED;
    }
    
    convert_to_long_ex(&pvalReturn);
    return pvalReturn.value.lval;
}

HRESULT ECImportContentsChangesProxy::ImportMessageMove(ULONG cbSourceKeySrcFolder, BYTE *pbSourceKeySrcFolder, ULONG cbSourceKeySrcMessage, BYTE *pbSourceKeySrcMessage, ULONG cbPCLMessage, BYTE *pbPCLMessage, ULONG cbSourceKeyDestMessage, BYTE *pbSourceKeyDestMessage, ULONG cbChangeNumDestMessage, BYTE *pbChangeNumDestMessage) {
    return MAPI_E_NO_SUPPORT;
}
