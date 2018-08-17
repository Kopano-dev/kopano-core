/* SPDX-License-Identifier: AGPL-3.0-only */
%{
#include <kopano/ECTags.h>
%}

// Pull in the language-specific helpers
#if SWIGPYTHON
%include "python/helpers_python.i"
#endif

%typemap(in) (IMAPIProp *lpWrapped, LPCIID USE_IID_FOR_OUTPUT) (int res)
{
	res = SWIG_ConvertPtr($input, (void**)&$1, $1_descriptor, 0 |  0 );
    if (!SWIG_IsOK(res)) {
      SWIG_exception_fail(SWIG_ArgError(res1), "BUG"); 
    }

	$2 = IIDFromType(TypeFromObject($input));
}

%typemap(in,numinputs=0) LPUNKNOWN *OUTPUT_USE_IID (LPUNKNOWN temp) {
  $1 = ($1_type)&temp;
}


/* Unwrap a MAPI object */
%inline %{
HRESULT UnwrapObject(IMAPIProp *lpWrapped, LPCIID USE_IID_FOR_OUTPUT, LPUNKNOWN* OUTPUT_USE_IID) {
	HRESULT hr = hrSuccess;
	IUnknown *lpUnwrapped = NULL;
	LPSPropValue lpPropValue = NULL;

	if (lpWrapped == NULL || OUTPUT_USE_IID == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if (HrGetOneProp(lpWrapped, PR_EC_OBJECT, &lpPropValue) == hrSuccess) {
		lpUnwrapped = reinterpret_cast<IUnknown *>(lpPropValue->Value.lpszA);
		if (lpUnwrapped == NULL) {
			hr = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}

		hr = lpUnwrapped->QueryInterface(*USE_IID_FOR_OUTPUT, (void**)OUTPUT_USE_IID);
	} else {
		// Possible object already wrapped, gives the original object back
		hr = lpWrapped->QueryInterface(*USE_IID_FOR_OUTPUT, (void**)OUTPUT_USE_IID);
	}

exit:
	MAPIFreeBuffer(lpPropValue);
	return hr;
}
%}
