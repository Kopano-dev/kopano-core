/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include "phpconfig.h"
#include <kopano/platform.h>
#include <kopano/MAPIErrors.h>
#include <cstdio>
#include <ctime>
#include <cmath>
#if __GNUC__ >= 5 && __GNUC__ < 6 && defined(_GLIBCXX_CMATH)
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
#include "ECImportHierarchyChangesProxy.h"
#include "typeconversion.h"
#include "main.h"

ECImportHierarchyChangesProxy::ECImportHierarchyChangesProxy(const zval *v) :
	m_cRef(1)
{
	ZVAL_OBJ(&m_lpObj, Z_OBJ_P(v));
	Z_ADDREF(m_lpObj);
}

ECImportHierarchyChangesProxy::~ECImportHierarchyChangesProxy() {
	Z_DELREF(m_lpObj);
}

ULONG 	ECImportHierarchyChangesProxy::AddRef() {
	return ++m_cRef;
}

ULONG	ECImportHierarchyChangesProxy::Release() {
	if (--m_cRef == 0) {
        delete this;
        return 0;
    }

    return m_cRef;
}

HRESULT ECImportHierarchyChangesProxy::QueryInterface(REFIID iid, void **lpvoid) {
    if(iid == IID_IExchangeImportHierarchyChanges) {
        AddRef();
        *lpvoid = this;
        return hrSuccess;
    }
    return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECImportHierarchyChangesProxy::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT ECImportHierarchyChangesProxy::Config(IStream *lpStream, unsigned int ulFlags)
{
	zvalplus pvalFuncName, pvalArgs[2];
	zval pvalReturn;

    if(lpStream) {
		ZVAL_RES(&pvalArgs[0], zend_register_resource(lpStream, le_istream));
		if (Z_RES(pvalArgs[0]))
			lpStream->AddRef();
	} else {
		ZVAL_NULL(&pvalArgs[0]);
	}

    ZVAL_LONG(&pvalArgs[1], ulFlags);

    ZVAL_STRING(&pvalFuncName, "Config");
    if (call_user_function(NULL, &m_lpObj, &pvalFuncName, &pvalReturn, 2, pvalArgs) == FAILURE) {
        php_error_docref(NULL, E_WARNING, "Config method not present on ImportHierarchyChanges object");
        return MAPI_E_CALL_FAILED;
    }
	return zval_get_long(&pvalReturn);
}

HRESULT ECImportHierarchyChangesProxy::UpdateState(IStream *lpStream)
{
	zvalplus pvalFuncName, pvalArgs;
	zval pvalReturn;

    if(lpStream) {
		ZVAL_RES(&pvalArgs, zend_register_resource(lpStream, le_istream));
		if (Z_RES(pvalArgs))
			lpStream->AddRef();
	} else {
		ZVAL_NULL(&pvalArgs);
	}

    ZVAL_STRING(&pvalFuncName, "UpdateState");
    if (call_user_function(NULL, &m_lpObj, &pvalFuncName, &pvalReturn, 1, &pvalArgs) == FAILURE) {
        php_error_docref(NULL, E_WARNING, "UpdateState method not present on ImportHierarchyChanges object");
        return MAPI_E_CALL_FAILED;
    }
	return zval_get_long(&pvalReturn);
}

HRESULT ECImportHierarchyChangesProxy::ImportFolderChange(ULONG cValues, LPSPropValue lpPropArray)  {
	zvalplus pvalFuncName, pvalArgs;
	zval pvalReturn;
	auto hr = PropValueArraytoPHPArray(cValues, lpPropArray, &pvalArgs);
    if(hr != hrSuccess) {
		php_error_docref(nullptr, E_WARNING, "Unable to convert MAPI propvalue array to PHP: %s (%x)",
			KC::GetMAPIErrorMessage(hr), hr);
		return hr;
    }

    ZVAL_STRING(&pvalFuncName, "ImportFolderChange");
    if (call_user_function(NULL, &m_lpObj, &pvalFuncName, &pvalReturn, 1, &pvalArgs) == FAILURE) {
        php_error_docref(NULL, E_WARNING, "ImportFolderChange method not present on ImportHierarchyChanges object");
        return MAPI_E_CALL_FAILED;
    }
	return zval_get_long(&pvalReturn);
}

HRESULT ECImportHierarchyChangesProxy::ImportFolderDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList) {
	zvalplus pvalFuncName, pvalArgs[2];
	zval pvalReturn;

    ZVAL_LONG(&pvalArgs[0], ulFlags);
    SBinaryArraytoPHPArray(lpSourceEntryList, &pvalArgs[1]);

    ZVAL_STRING(&pvalFuncName, "ImportFolderDeletion");
    if (call_user_function(NULL, &m_lpObj, &pvalFuncName, &pvalReturn, 2, pvalArgs) == FAILURE) {
        php_error_docref(NULL, E_WARNING, "ImportFolderDeletion method not present on ImportHierarchyChanges object");
        return MAPI_E_CALL_FAILED;
    }
	return zval_get_long(&pvalReturn);
}
