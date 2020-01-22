/* SPDX-License-Identifier: AGPL-3.0-only */
// Python
%include <python/std_basic_string.i>
%include <python/cwstring.i>

%{
#include <kopano/conversion.h>
#include <kopano/director_util.h>

#define SWIG_SHADOW 0
#define SWIG_OWNER SWIG_POINTER_OWN

#define DIRECTORARGOUT(_arg) (__tupleIndex == -1 ? (PyObject*)(_arg) : PyTuple_GetItem((_arg), __tupleIndex++))

#if PY_MAJOR_VERSION >= 3
        #define PyString_AsStringAndSize(value, input, size) \
                PyBytes_AsStringAndSize(value, input, size)
#endif
%}

#if SWIG_VERSION > 0x020004
	/* also in this version, the _name $1_name isn't required anymore, but it still works, so I'll leave those */
	#define DAO_PARAMETER $1
	#define DAO_RESULT $result
#else
	/* old swig has weird names in directorargout */
	#define DAO_PARAMETER $result
	#define DAO_RESULT $input
#endif

%init %{
	Init();
%}

%fragment("SWIG_FromBytePtrAndSize","header",fragment="SWIG_FromCharPtrAndSize") {
SWIGINTERNINLINE PyObject *
SWIG_FromBytePtrAndSize(const unsigned char* carray, size_t size)
{
	return SWIG_FromCharPtrAndSize(reinterpret_cast<const char *>(carray), size);
}
}

// Exceptions
%include "exception.i"
%typemap(out) HRESULT
{
  $result = Py_None;
  Py_INCREF(Py_None);
  if(FAILED($1)) {
	DoException($1);
	SWIG_fail;
  }
}

// Input
%typemap(in) (ULONG, MAPIARRAY) (KC::memory_ptr<std::remove_const<std::remove_pointer<$2_type>::type>::type> tmp)
{
	ULONG len;
	tmp.reset(const_cast<std::add_pointer< std::remove_const< std::remove_pointer< $2_type >::type >::type >::type>(List_to$2_mangle($input, &len)));
	$1 = len;
	if(PyErr_Occurred()) goto fail;
        $2 = tmp;
}

%typemap(in) 			(MAPIARRAY, ULONG)
{
	ULONG len;
	$1 = List_to$1_mangle($input, &len);
	$2 = len;
	if(PyErr_Occurred()) goto fail;
}

%typemap(in) MAPILIST *INPUT (KC::memory_ptr<std::remove_pointer<$*type>::type> tmp)
{
	tmp.reset(List_to$*mangle($input));
	if(PyErr_Occurred()) goto fail;
	$1 = &+tmp;
}

%typemap(in) MAPILIST (KC::memory_ptr<std::remove_const<std::remove_pointer<$1_type>::type>::type> tmp)
{
	tmp.reset(List_to$mangle($input));
	if(PyErr_Occurred()) goto fail;
        $1 = tmp;
}

// use adrlist_ptr for adrlists

%typemap(in) ADRLIST *INPUT (KC::adrlist_ptr tmp), LPADRLIST INOUT (KC::adrlist_ptr tmp)
{
        tmp.reset(List_to$mangle($input));
        if(PyErr_Occurred()) goto fail;
        $1 = tmp;
}

%typemap(in) LPROWLIST (KC::rowlist_ptr tmp)
{
	tmp.reset(List_to_LPROWLIST($input));
	if(PyErr_Occurred()) goto fail;
	$1 = tmp;
}

%typemap(in) MAPISTRUCT (KC::memory_ptr<std::remove_const<std::remove_pointer<$type>::type>::type> tmp)
{
        tmp.reset(Object_to$mangle($input));
	if(PyErr_Occurred()) goto fail;
        $1 = tmp;
}

%typemap(in)				SYSTEMTIME
{
	$1 = Object_to$mangle($input);
	if(PyErr_Occurred()) goto fail;
}

// we cannot use ulFlags during conversion, as it may not have been converted yet (use arginit for ulFlags?)

%typemap(in)				MAPISTRUCT_W_FLAGS (PyObject *tmpobj)
{
       tmpobj = $input;
}

%typemap(check) MAPISTRUCT_W_FLAGS (KC::memory_ptr<std::remove_const<std::remove_pointer<$type>::type>::type> tmp)
{
       tmp.reset(Object_to$mangle(tmpobj$argnum, ulFlags));
       if(PyErr_Occurred()) {
               %argument_fail(SWIG_ERROR,"$type",$symname, $argnum);
       }
       $1 = tmp;
}

// Output
%typemap(argout)	MAPILIST *
{
	%append_output3(List_from_$basetype(*($1)));
	if(PyErr_Occurred()) goto fail;
}

%typemap(argout)	MAPILIST INOUT
{
	%append_output3(List_from_$basetype($1));
	if(PyErr_Occurred()) goto fail;
}

%typemap(argout)	MAPISTRUCT *
{
	%append_output3(Object_from_$basetype(*($1)));
	if(PyErr_Occurred()) goto fail;
}

%typemap(argout)	SYSTEMTIME *
{
	%append_output3(Object_from_$basetype(*($1)));
	if(PyErr_Occurred()) goto fail;
}

%typemap(argout)	(ULONG *, MAPIARRAY *)
{
	%append_output3(List_from_$2_basetype(*($2),*($1)));
	if(PyErr_Occurred()) goto fail;
}

%typemap(argout)	(MAPIARRAY, LONG)
{
	%append_output3(List_from_$1_basetype($1,$2));
	if(PyErr_Occurred()) goto fail;
}

// Unicode

// LPTSTR

// Input

// Defer to 'CHECK' stage
%typemap(in) LPTSTR
{
  $1 = (LPTSTR)$input;
}

%typemap(check,fragment="SWIG_AsWCharPtrAndSize") LPTSTR (std::string strInput, wchar_t *buf, int alloc = 0)
{
  PyObject *o = (PyObject *)$1;
  if(o == Py_None)
    $1 = NULL;
  else {
    if(ulFlags & MAPI_UNICODE) {
      if(PyUnicode_Check(o)) {
		size_t size = 0;
		SWIG_AsWCharPtrAndSize(o, &buf, &size, &alloc);
		$1 = buf;
      } else {
        PyErr_SetString(PyExc_RuntimeError, "MAPI_UNICODE flag passed but passed parameter is not a unicode string");
      }
    } else {
      if(PyUnicode_Check(o)) {
        PyErr_SetString(PyExc_RuntimeError, "MAPI_UNICODE flag not passed but passed parameter is a unicode string");
      }

      char *input;
      Py_ssize_t size;

      if(PyString_AsStringAndSize(o, &input, &size) != -1) {
        strInput.assign(input, size);
        $1 = (LPTSTR)strInput.c_str();
      }
      else
        %argument_fail(SWIG_ERROR,"$type",$symname, $argnum);
    }
  }

  if(PyErr_Occurred()) {
    %argument_fail(SWIG_ERROR,"$type",$symname, $argnum);
  }

}

%typemap(freearg) LPTSTR
{
  if (alloc$argnum == SWIG_NEWOBJ) %delete_array(buf$argnum);
}


// Director stuff
%feature("director:except") {
  if ($error != NULL) {
    HRESULT hr;
    
    if (GetExceptionError($error, &hr) == 1) {
        PyErr_Clear();
        return hr;	// Early return
    } else {
		if (check_call_from_python()) {
			throw Swig::DirectorMethodException();	// Let the calling python interpreter handle the exception
		} else {
			PyErr_Print();
			PyErr_Clear();
			return MAPI_E_CALL_FAILED;
		}
    }
  }
}

%typemap(directorin)	(ULONG, MAPIARRAY)
{
  $input = List_from_$2_basetype($2_name, $1_name);
  if(PyErr_Occurred())
    %dirout_fail(0, "$type");
}

%typemap(directorin)	MAPILIST
{
  $input = List_from_$1_basetype($1_name);
  if(PyErr_Occurred())
    %dirout_fail(0, "$type");
}

%typemap(directorin)	MAPISTRUCT
{
  $input = Object_from_$1_basetype($1_name);
  if(PyErr_Occurred())
    %dirout_fail(0, "$type");  
}

// void *    Used in CopyTo/CopyProps
%typemap(directorin) void *lpDestObj
{
	$input = SWIG_NewPointerObj($1_name, TypeFromIID(*__iid), 0 | 0);
}

%typemap(directorin,noblock=1) const IID& USE_IID_FOR_OUTPUT
{
  const IID* __iid = &$1_name;
  $input = SWIG_FromCharPtrAndSize((const char *)__iid, sizeof(IID));
}

%typemap(directorin) (const void *pv, ULONG cb)
{
  $input = SWIG_FromCharPtrAndSize((const char *)$1_name, $2_name);
}

%typemap(directorin) MAPILIST *INPUT
{
	$input = List_from$*mangle(*$1_name);
}

%typemap(directorin) ULARGE_INTEGER
{
	$input = SWIG_From(unsigned long long)($1_name.QuadPart);
}

%typemap(directorargout) ULARGE_INTEGER *
{
  DAO_PARAMETER->QuadPart = PyLong_AsLongLong(DAO_RESULT);
}

%typemap(directorin) LARGE_INTEGER
{
	$input = SWIG_From(long long)($1_name.QuadPart);
}

%typemap(directorargout) LARGE_INTEGER *
{
  DAO_PARAMETER->QuadPart = PyLong_AsLongLong(DAO_RESULT);
}

%typemap(directorargout) (ULONG *OUTPUT)
{
  *DAO_PARAMETER = PyInt_AsLong(DAO_RESULT);
}

%typemap(directorargout) STATSTG *OUTPUT
{
  Object_to_STATSTG(DAO_RESULT, DAO_PARAMETER);
  if(PyErr_Occurred()) {
    %dirout_fail(0, "$type");
  }
}

%typemap(directorargout) void **OUTPUT_USE_IID
{
  int swig_res = SWIG_ConvertPtr(DIRECTORARGOUT(DAO_RESULT), DAO_PARAMETER, TypeFromIID(*__iid), 0);
  if (!SWIG_IsOK(swig_res)) {
    %dirout_fail(swig_res, "$type");
  }
}

%typemap(directorargout)	MAPICLASS *
{
  int swig_res = SWIG_ConvertPtr(DIRECTORARGOUT(DAO_RESULT), (void**)DAO_PARAMETER, $*1_descriptor, 0);
  if (!SWIG_IsOK(swig_res)) {
    %dirout_fail(swig_res, "$type");
  }

  /**
   * We use a referencing system in which the director object (the c++ interface) forwards
   * all addref() and release() calls to the python object, and the python object remains
   * the owner of the c++ director object; this means that when the python object gets to a refcount
   * of 0, it will clean up the c++ director object just like any other SWIGged object. However,
   * when this bit of code is invoked, it means that we are outputting an object from a director method
   * back to c++. When the object we are returning is a director object itself, this means that there are
   * now TWO references to the object: from python AND from C++ (since you should always return refcount 1
   * objects to c++). Since normally the object created will have a refcount of 1 (from python) we have
   * to increment the refcount when it is a director object.
   *
   * To put it a different way, although the object always had a c++ part, it was not referenced from c++
   * until we returned it to the realm of c++ from here, so we have to addref it now.
   */

	(*DAO_PARAMETER)->AddRef();
}

%typemap(directorin)	MAPICLASS
{
  $input = SWIG_NewPointerObj($1_name, $1_descriptor, SWIG_SHADOW | SWIG_OWNER);
}

%typemap(directorin)	(ULONG, BYTE*)
{
  if ($1_name > 0 && $2_name != NULL)
    $input = SWIG_FromCharPtrAndSize((char*)$2_name, $1_name);
}

%typemap(directorin,noblock=1)	LPCIID, LPGUID, GUID *
{
	LPCIID __iid = $1_name;
	$input = SWIG_FromCharPtrAndSize((char*)$1_name, sizeof(GUID));
}

%apply (ULONG, BYTE*) {(ULONG cbSourceKeySrcFolder, BYTE *pbSourceKeySrcFolder)}
%apply (ULONG, BYTE*) {(ULONG cbSourceKeySrcMessage, BYTE *pbSourceKeySrcMessage)}
%apply (ULONG, BYTE*) {(ULONG cbPCLMessage, BYTE *pbPCLMessage)}
%apply (ULONG, BYTE*) {(ULONG cbSourceKeyDestMessage, BYTE *pbSourceKeyDestMessage)}
%apply (ULONG, BYTE*) {(ULONG cbChangeNumDestMessage, BYTE * pbChangeNumDestMessage)}

%typemap(directorargout)	MAPISTRUCT *
{
  *DAO_PARAMETER = Object_to_$basetype(DIRECTORARGOUT(DAO_RESULT));
  if(PyErr_Occurred()) {
    %dirout_fail(0, "$type");
  }
}

%typemap(directorout,fragment=SWIG_AsVal_frag(long))	HRESULT (Py_ssize_t __tupleIndex)
{
  __tupleIndex = (PyTuple_Check($input) == 0 ? -1 : 0);
  $result = hrSuccess;
}

%typemap(directorargout) (ULONG *OUTPUTC, MAPIARRAY *OUTPUTP) (ULONG len)
{
	*$2_name = List_to$*2_mangle(DIRECTORARGOUT(DAO_RESULT), &len, CONV_COPY_DEEP);
	*$1_name = len;
}

%typemap(directorargout) MAPILIST *OUTPUT
{
	if(DAO_PARAMETER)
		*DAO_PARAMETER = List_to$*1_mangle(DIRECTORARGOUT(DAO_RESULT), CONV_COPY_DEEP);
}

// See MAPICLASS * for comments. Note that we ignore the IID. We could also still QueryInterface on $result.
%typemap(directorargout)	LPUNKNOWN *OUTPUT_USE_IID (int swig_res)
{
  swig_res = SWIG_ConvertPtr(DIRECTORARGOUT(DAO_RESULT), (void**)DAO_PARAMETER, SWIGTYPE_p_IUnknown, 0);
  if (!SWIG_IsOK(swig_res)) {
    %dirout_fail(swig_res, "$type");
  }
	(*DAO_PARAMETER)->AddRef();
}

%typemap(directorargout) ULONG *
{
	*DAO_PARAMETER = (ULONG)PyLong_AsUnsignedLong(DIRECTORARGOUT(DAO_RESULT));
}

%typemap(directorargout) LONG *
{
	*DAO_PARAMETER = (LONG)PyLong_AsLong(DIRECTORARGOUT(DAO_RESULT));
}

%typemap(in,numinputs=0) void *OUTPUT
{
}

%typemap(directorargout) (void *OUTPUT, ULONG cb, ULONG *cbOUTPUT)
{
    Py_ssize_t size = 0;
    char *s = NULL;
    if(PyString_AsStringAndSize(DAO_RESULT, &s, &size) == -1)
        %dirout_fail(0, "$type");

	memcpy($1_name, s, size);

	*$3_name = size;
}


%typemap(directorin) (ULONG cbEntryID, LPENTRYID lpEntryID)
{
  if ($1_name > 0 && $2_name != NULL)
    $input = PyBytes_FromStringAndSize((char*)$2_name, $1_name);
}

%apply (ULONG, MAPIARRAY) {(ULONG cElements, LPREADSTATE lpReadState), (ULONG cNotif, LPNOTIFICATION lpNotifications)};
%apply (ULONG *OUTPUTC, MAPIARRAY *OUTPUTP) {(ULONG *OUTPUTC, LPSPropValue *OUTPUTP), (ULONG *OUTPUTC, LPMAPINAMEID **OUTPUTP)};
%apply (MAPILIST *OUTPUT) {SPropTagArray **OUTPUT, LPSPropTagArray *OUTPUT, LPSPropProblemArray *OUTPUT, LPSRowSet *OUTPUT};
%apply MAPICLASS {IMAPISession *, IProfAdmin *, IMsgServiceAdmin *, IMAPITable *, IMsgStore *, IMAPIFolder *, IMAPITable *, IStream *, IMessage *, IAttach *, IAddrBook *}
%apply (ULONG cbEntryID, LPENTRYID lpEntryID) {(ULONG cFolderKeySize, BYTE *lpFolderSourceKey), (ULONG cMessageKeySize, BYTE *lpMessageSourceKey), (ULONG cbInstanceKey, BYTE *pbInstanceKey), (ULONG cbCollapseState, BYTE *pbCollapseState)};

%typemap(in) (unsigned int, char **) {
  /* Check if is a list */
  if (PyList_Check($input)) {
    int i;
    $1 = PyList_Size($input);
    $2 = (char **) malloc(($1+1)*sizeof(char *));
    for (i = 0; i < $1; ++i) {
      PyObject *o = PyList_GetItem($input,i);
      if (PyString_Check(o))
	$2[i] = PyString_AsString(PyList_GetItem($input,i));
      else {
	PyErr_SetString(PyExc_TypeError,"list must contain strings");
	free($2);
	return NULL;
      }
    }
    $2[i] = 0;
  } else {
    PyErr_SetString(PyExc_TypeError,"not a list");
    return NULL;
  }
}

%typemap(freearg) (unsigned int, char **) {
	free((void *)$2);
}

