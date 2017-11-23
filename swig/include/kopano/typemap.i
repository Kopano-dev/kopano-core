// Generic typemaps
%apply unsigned int {ULONG};
%apply unsigned long {ULONG_PTR};
%apply int {LONG};
%apply unsigned int {DWORD, HRESULT};
%apply unsigned int *OUTPUT {ULONG *, ULONG *lpulSteps, ULONG *lpulProgress};
%apply unsigned int *INOUT {ULONG *INOUT};
%apply unsigned int *OUTPUT {LONG *};
%apply bool {BOOL};
#ifdef UNICODE
%apply wchar_t * {LPTSTR};
#else
%apply char * {LPTSTR};
#endif

%include "cstring.i"
%include "cwstring.i"
%cstring_output_allocate_size(char **lpOutput, ULONG *ulRead, MAPIFreeBuffer(*$1));

%typemap(in) (const void *pv, ULONG cb) (int res, char *buf = NULL, Py_ssize_t size, int alloc = 0)
{
  if(PyBytes_AsStringAndSize($input, &buf, &size) == -1)
    %argument_fail(SWIG_ERROR,"$type",$symname, $argnum);

  $1 = %reinterpret_cast(buf, $1_ltype);
  $2 = %numeric_cast(size, $2_ltype);
}

// HRESULT
%include "exception.i"
%typemap(out) HRESULT (char ex[64])
{
  if(FAILED($1)) {
    snprintf(ex,sizeof(ex),"failed with HRESULT 0x%08X", $1);
	SWIG_exception(SWIG_RuntimeError, ex);
  }
}

// LARGE_INTEGER / ULARGE_INTEGER

%typemap(in,fragment=SWIG_AsVal_frag(unsigned long long)) ULARGE_INTEGER
{
  unsigned long long l = 0;
  SWIG_AsVal(unsigned long long)($input, &l);  

  $1.QuadPart = l;
}

%typemap(in,numinputs=0) ULARGE_INTEGER * (ULARGE_INTEGER u)
{
  $1 = &u;
}

%typemap(argout,fragment=SWIG_From_frag(unsigned long long)) ULARGE_INTEGER *
{
  %append_output(SWIG_From(unsigned long long)($1->QuadPart));
}

%typemap(in,fragment=SWIG_AsVal_frag(long long)) LARGE_INTEGER
{
  long long l = 0;
  SWIG_AsVal(long long)($input, &l);  

  $1.QuadPart = l;
}

%typemap(in,numinputs=0) LARGE_INTEGER * (LARGE_INTEGER u)
{
  $1 = &u;
}

%typemap(argout,fragment=SWIG_From_frag(unsigned long long)) ULARGE_INTEGER *
{
 %append_output(SWIG_From(unsigned long long)($1->QuadPart));
}

// FILETIME

%typemap(in,fragment=SWIG_AsVal_frag(unsigned long long)) FILETIME
{
  $1 = Object_to_FILETIME($input);
}

%typemap(in,numinputs=0) FILETIME * (FILETIME ft)
{
  $1 = &ft;
}

%typemap(argout,fragment=SWIG_From_frag(unsigned long long)) FILETIME *
{
  %append_output(Object_from_FILETIME(*$1));
}

// ULONG+LP

// Input
%typemap(in, fragment="SWIG_AsCharPtrAndSize")				(ULONG cbEntryID, LPENTRYID lpEntryID) (int res, char *buf = NULL, size_t size, int alloc = 0)
{
  if($input == Py_None) {
    $1 = 0;
    $2 = NULL;
  } else {
    if(PyBytes_AsStringAndSize($input, &buf, (Py_ssize_t *)&size) == -1) {
      %argument_fail(SWIG_ERROR,"$type",$symname, $argnum);
    }
    $1 = %numeric_cast(size, $1_ltype);
    $2 = %reinterpret_cast(buf, $2_ltype);
  }
}
%typemap(freearg) (ULONG cbEntryID, LPENTRYID lpEntryID) {
  if (alloc$argnum == SWIG_NEWOBJ) %delete_array(buf$argnum);
}
%apply (ULONG cbEntryID, LPENTRYID lpEntryID) {(ULONG cFolderKeySize, BYTE *lpFolderSourceKey), (ULONG cMessageKeySize, BYTE *lpMessageSourceKey), (ULONG cbInstanceKey, BYTE *pbInstanceKey), (ULONG cbCollapseState, BYTE *pbCollapseState)};
%apply (ULONG cbEntryID, LPENTRYID lpEntryID) {(ULONG cbEntryID1, ENTRYID *lpEntryID1), (ULONG cbEntryID2, ENTRYID *lpEntryID2),
(ULONG cbEIDContainer, LPENTRYID lpEIDContainer), (ULONG cbEIDNewEntryTpl, LPENTRYID lpEIDNewEntryTpl), (ULONG cbUserEntryID, LPENTRYID lpUserEntryID) };

// Output
%typemap(in,numinputs=0) (ULONG *OUTPUT, LPENTRYID *OUTPUT) (ULONG cbEntryID = 0, KCHL::memory_ptr< std::remove_pointer<$*2_type>::type > lpEntryID) {
  $1 = &cbEntryID; $2 = &~lpEntryID;
}
%typemap(argout,fragment="SWIG_FromCharPtrAndSize") (ULONG *OUTPUT, LPENTRYID *OUTPUT)
{
  if (*$2) {
    %append_output(PyBytes_FromStringAndSize((const char *)*$2,*$1));
  }
}
%apply (ULONG *OUTPUT, LPENTRYID *OUTPUT) {(ULONG* lpcbStoreId, LPENTRYID* lppStoreId), (ULONG* lpcbRootId, LPENTRYID *lppRootId), (ULONG *lpulOutput, LPBYTE *lpOutput)};

// Optional In & Output
%typemap(in) (ULONG *OPTINOUT, LPENTRYID *OPTINOUT) (int res, char *buf = NULL, size_t size, int alloc = 0, ULONG cbEntryID = 0, KCHL::memory_ptr<ENTRYID> tmp) {
  $1 = &cbEntryID;

  res = SWIG_AsCharPtrAndSize($input, &buf, &size, &alloc);
  if (!SWIG_IsOK(res)) {
    %argument_fail(res,"$type",$symname, $argnum);
  }
  if(buf == NULL) {
    *$1 = 0;
    $2 = &~tmp;
  } else {
    *$1 = %numeric_cast(size - 1, $*1_ltype);
    tmp.reset(%reinterpret_cast(buf, $*2_ltype));
    $2 = &+tmp;
  }
}
%typemap(argout,fragment="SWIG_FromCharPtrAndSize") (ULONG *OPTINOUT, LPENTRYID *OPTINOUT)
{
  if (*$2) {
    %append_output(PyBytes_FromStringAndSize((const char *)*$2,*$1));
  }
}
%apply (ULONG *OPTINOUT, LPENTRYID *OPTINOUT) {(ULONG* lpcbStoreId_oio, LPENTRYID* lppStoreId_oio), (ULONG* lpcbRootId_oio, LPENTRYID *lppRootId_oio)};

// IID + LPUNKOWN

// Output
%typemap(in,numinputs=0) LPUNKNOWN *OUTPUT_USE_IID (LPUNKNOWN temp) {
  $1 = ($1_type)&temp;
}
%typemap(argout) LPUNKNOWN *OUTPUT_USE_IID
{
 %append_output(SWIG_NewPointerObj((void*)*($1), TypeFromIID(*__lpiid), SWIG_SHADOW | SWIG_OWNER));
}
// Also apply to void ** in QueryInterface()
%apply LPUNKNOWN *OUTPUT_USE_IID {void **OUTPUT_USE_IID};

// LPMAPIUID/LPCIID/LPGUID

// Input
%typemap(in,fragment="SWIG_AsCharPtrAndSize")	LPMAPIUID (int res, char *buf = NULL, size_t size, int alloc = 0),
				LPCIID (int res, char *buf = NULL, size_t size, int alloc = 0),
				LPGUID (int res, char *buf = NULL, size_t size, int alloc = 0)
{
  alloc = SWIG_OLDOBJ;
  if($input == Py_None)
    $1 = 0;
  else {
    if(PyBytes_AsStringAndSize($input, &buf, (Py_ssize_t *)&size) == -1 || size != sizeof(MAPIUID)) {
      %argument_fail(SWIG_ERROR,"$type",$symname, $argnum);
    }
    $1 = %reinterpret_cast(buf, $1_ltype);
  }
}
%typemap(in,fragment="SWIG_AsCharPtrAndSize")	const IID& (int res = 0, char *buf = NULL, size_t size = 0, int alloc = 0)
{
  alloc = SWIG_OLDOBJ;
  if($input == Py_None)
      $1 = 0;
  else {
      if(!(buf = PyBytes_AsString($input))) {
        %argument_fail(SWIG_ERROR, "$type", $symname, $argnum);
      }
      $1 = %reinterpret_cast(buf, $1_ltype);
  }
}
%typemap(freearg,noblock=1,match="in") LPMAPIUID, LPCIID, const IID& {
  if (alloc$argnum == SWIG_NEWOBJ) %delete_array(buf$argnum);
}
// Used for LPUNKNOWN *
%typemap(arginit,noblock=1,fragment="SWIG_AsCharPtrAndSize") LPCIID USE_IID_FOR_OUTPUT
{
  LPCIID &__lpiid = $1;
}
%typemap(arginit,noblock=1,fragment="SWIG_AsCharPtrAndSize") const IID& USE_IID_FOR_OUTPUT
{
  LPIID &__lpiid = $1;
}

%typemap(in, numinputs=0) LPMAPIUID OUTPUT (MAPIUID tmpUid)
{
	$1 = ($1_type)&tmpUid;
}

%typemap(argout) LPMAPIUID OUTPUT
{
	%append_output(PyBytes_FromStringAndSize((const char *)$1,sizeof(MAPIUID)));
}

// ULONG ulFlags
%typemap(arginit,noblock=1) ULONG ulFlags
{
  ULONG ulFlags = 0;
}

%typemap(in) ULONG ulFlags (unsigned int fl, int ecode)
{
	ecode = SWIG_AsVal(unsigned int)($input, &fl);
	if (!SWIG_IsOK(ecode)) {
		%argument_fail(ecode,"$type",$symname, $argnum);
	} 
	$1 = fl;
	ulFlags = fl;
}

// LPTSTR

// Output
%typemap(in,numinputs=0) (LPTSTR *OUTPUT) (LPTSTR lpStr = NULL) {
  $1 = &lpStr;
}
%typemap(argout,fragment="SWIG_FromCharPtr,SWIG_FromWCharPtr") LPTSTR *OUTPUT {
	if (ulFlags & MAPI_UNICODE) {
		%append_output(SWIG_FromWCharPtr(*$1));
	} else {
		%append_output(SWIG_FromCharPtr((char*)*$1));
	}
}
%typemap(freearg) LPTSTR *OUTPUT {
	MAPIFreeBuffer(*$1);
}

// char** allocated with mapi
%typemap(in,numinputs=0) (char** OUTMAPICHAR) (char* lpStr = NULL) {
  $1 = &lpStr;
}
%typemap(argout,fragment="SWIG_FromCharPtr") char** OUTMAPICHAR {
    %append_output(SWIG_FromCharPtr((char*)*$1));
}
%typemap(freearg) char** OUTMAPICHAR {
    MAPIFreeBuffer(*$1);
}

// ULONG+IUnknown **

// Output
%typemap(in,numinputs=0) (ULONG *OUTPUT, IUnknown **OUTPUT) (ULONG ulType, IUnknown *lpUnk)
{
	ulType = 0;
	lpUnk = NULL;

	$1 = &ulType;
	$2 = &lpUnk;
}

%typemap(argout) (ULONG *OUTPUT, IUnknown **OUTPUT)
{
  switch(*($1)) {
    case MAPI_FOLDER:
		%append_output(SWIG_NewPointerObj((void*)*($2), SWIGTYPE_p_IMAPIFolder, SWIG_SHADOW | SWIG_OWNER)); break;
	case MAPI_MESSAGE:
		%append_output(SWIG_NewPointerObj((void*)*($2), SWIGTYPE_p_IMessage, SWIG_SHADOW | SWIG_OWNER)); break;
	case MAPI_MAILUSER:
		%append_output(SWIG_NewPointerObj((void*)*($2), SWIGTYPE_p_IMailUser, SWIG_SHADOW | SWIG_OWNER)); break;
	case MAPI_DISTLIST:
		%append_output(SWIG_NewPointerObj((void*)*($2), SWIGTYPE_p_IDistList, SWIG_SHADOW | SWIG_OWNER)); break;
	case MAPI_ABCONT:
		%append_output(SWIG_NewPointerObj((void*)*($2), SWIGTYPE_p_IABContainer, SWIG_SHADOW | SWIG_OWNER)); break;
    default:
		break;
  }
}

// unsigned char *, unsigned int
%typemap(in,numinputs=1) (unsigned char *, unsigned int) (int res, char *buf = NULL, size_t size, int alloc = 0)
{
	res = SWIG_AsCharPtrAndSize($input, &buf, &size, &alloc);
	if (!SWIG_IsOK(res)) {
		%argument_fail(res,"$type",$symname, $argnum);
	}
	if(buf == NULL) {
		$1 = NULL;
		$2 = 0;
	} else {
		$1 = %reinterpret_cast(buf, $1_ltype);
		$2 = %numeric_cast(size - 1, $2_ltype);
	}
}

// STATSTG

%typemap(in,numinputs=0) STATSTG * (STATSTG stat)
{
	$1 = &stat;
	memset(&stat, 0, sizeof(stat));
}

%typemap(argout) STATSTG *
{
	%append_output(Object_from_STATSTG($1));
}

// MAPISTRUCT (MAPI data struct)

// Input
%typemap(arginit) MAPISTRUCT
	"$1 = NULL;"

// MAPISTRUCT_W_FLAGS (MAPI data struct with LPTSTR members)

// Input
%typemap(arginit) MAPISTRUCT_W_FLAGS
	"$1 = NULL;"

// MAPILIST (MAPI list of data structs)

// Input
%typemap(arginit) MAPILIST
	"$1 = NULL;"

// Output
%typemap(in,numinputs=0)	MAPILIST * (KCHL::memory_ptr< std::remove_pointer<$basetype>::type > temp), MAPISTRUCT * (KCHL::memory_ptr< std::remove_pointer<$basetype>::type > temp)
{
        $1 = &~temp;
}

// MAPICLASS (Class instances of MAPI objects)

// Output
%typemap(in,numinputs=0) 	MAPICLASS *($basetype *temp)
	"temp = NULL; $1 = &temp;";
%typemap(argout)	MAPICLASS *
{
  %append_output(SWIG_NewPointerObj((void*)*($1), $*1_descriptor, SWIG_SHADOW | SWIG_OWNER));
}

// MAPIARRAY (List of objects)

// Output
%typemap(in,numinputs=0)	(ULONG *,MAPIARRAY *) (ULONG c, KCHL::memory_ptr< std::remove_pointer< std::remove_pointer< $2_type >::type >::type > tmp)
{
        $2 = &~tmp;
        c = 0; $1 = &c;
}

%typemap(arginit) (ULONG, MAPIARRAY)
{
	$1 = 0;
	$2 = NULL;
}

// SYSTEMTIME
// Output (specifics are in typemap_python.i)
%typemap(in, numinputs=0)	(SYSTEMTIME *)(SYSTEMTIME st)
	"$1 = &st;";


// ECLogger director
#if SWIGPYTHON

%typemap(in) KC::ECLogger *annoyingswig (int res, ECSimpleLogger *sl, ECLoggerProxy *proxy)
{
	res = SWIG_ConvertPtr($input, (void **)&sl, SWIGTYPE_p_ECSimpleLogger, 0 | 0);
	if(!SWIG_IsOK(res))
		%argument_fail(res,"ECSimpleLogger",$symname, $argnum);

	ECLoggerProxy::Create(EC_LOGLEVEL_DEBUG, sl, &proxy);
	$1 = proxy;
}

%typemap(freearg) KC::ECLogger *annoyingswig
{
	if ($1 != nullptr)
		$1->Release();
}

#endif

// Pull in the language-specific typemap
#if SWIGPYTHON
%include <kopano/typemap_python.i>
#endif

// Mapping of types to correct MAPI* handler type

// Input
%apply (ULONG, MAPIARRAY) {(ULONG cValues, SPropValue *lpProps), (ULONG cValues, LPSPropValue lpProps), (ULONG cPropNames, LPMAPINAMEID* lppPropNames), (ULONG cInterfaces, LPCIID lpInterfaces), (ULONG cValuesConversion, SPropValue *lpPropArrayConversion), (ULONG cValuesConversion, LPSPropValue lpPropArrayConversion)};
%apply MAPILIST {SPropTagArray *, LPSPropTagArray, LPENTRYLIST, ADRLIST *, LPADRLIST, LPFlagList};
%apply MAPILIST *INPUT {SPropTagArray **, LPSPropTagArray *};
%apply MAPISTRUCT {LPSRestriction, SSortOrderSet *, SPropValue *, LPSPropValue, LPNOTIFICATION};

// Output
%apply (ULONG *, MAPIARRAY *) {(ULONG *OUTPUTC, SPropValue **OUTPUTP), (ULONG *OUTPUTC, LPSPropValue *OUTPUTP), (ULONG *OUTPUTC, LPNOTIFICATION *OUTPUTP), (ULONG *OUTPUTC, LPMAPINAMEID **OUTPUTP)};
%apply MAPILIST * {ADRLIST **OUTPUT, LPADRLIST *OUTPUT, LPSRowSet *OUTPUT, LPSPropProblemArray *OUTPUT, SPropTagArray **OUTPUT, LPSPropTagArray *OUTPUT, LPENTRYLIST *OUTPUT};
%apply MAPISTRUCT * {LPMAPIERROR *OUTPUT, SSortOrderSet **OUTPUT, LPSRestriction *OUTPUT};

// Input/Output
%apply MAPILIST INOUT {ADRLIST *INOUT, LPADRLIST INOUT, LPFlagList INOUT };

// Classes
%apply MAPICLASS *{IMAPISession **, IProfAdmin **, IMsgServiceAdmin **, IMAPITable **, IMsgStore **, IMAPIFolder **, IMAPITable **, IStream **, IMessage **, IAttach **, IAddrBook **, IProviderAdmin **, IProfSect **, IUnknown **}

// Specialization of MAPILIST * for rowset and adrlist types

%typemap(in, numinputs=0) LPADRLIST *OUTPUT (KCHL::adrlist_ptr temp)
{
       $1 = &~temp;
}

%typemap(in, numinputs=0) LPSRowSet *OUTPUT (KCHL::rowset_ptr temp)
{
        $1 = &~temp;
}
