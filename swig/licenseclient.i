%module licenseclient

%{
	#include <mapidefs.h>			// HRESULT
	#include "KCmd.nsmap" /* get the namespace symbol */
	#include "ECLicenseClient.h"
	#include "licenseclient_conv.h"

	ECRESULT ServiceTypeStringToServiceType(const char *lpszServiceType, int &serviceType) {
		ECRESULT er = erSuccess;
		if (lpszServiceType == NULL) {
			er = KCERR_INVALID_TYPE;
			goto exit;
		}
		if (strcmp(lpszServiceType, "ZCP") == 0)
			serviceType = 0;	/*SERVICE_TYPE_ZCP*/
		else if (strcmp(lpszServiceType, "ARCHIVER") == 0)
			serviceType = 1;	/*SERVICE_TYPE_ARCHIVE*/
		else
			er = KCERR_INVALID_TYPE;
	exit:
		return er;
	}
%}


// ECRESULT
%include "exception.i"
%typemap(out) ECRESULT (char ex[64])
{
  if(FAILED($1)) {
	snprintf(ex,sizeof(ex),"failed with ECRESULT 0x%08X", $1);
	SWIG_exception(SWIG_RuntimeError, ex);
  }
}


// std::vector<std::string>&
%typemap(in,numinputs=0) std::vector<std::string> &OUTPUT	(std::vector<std::string> sv)
{
	$1 = &sv;
}
%typemap(argout) (std::vector<std::string> &OUTPUT)
{
	%append_output(List_from_StringVector(*($1)));
	if(PyErr_Occurred())
		goto fail;
}


// const std::vector<std::string>&
%typemap(in) const std::vector<std::string> & (int res, std::vector<std::string> v)
{
	res = List_to_StringVector($input, v);
	if (res != 0) {
		goto fail;
	}
	$1 = &v;
}



// std::string&
%typemap(in,numinputs=0) std::string &OUTPUT	(std::string s)
{
	$1 = &s;
}
%typemap(argout,fragment="SWIG_FromCharPtr") (std::string &OUTPUT)
{
	%append_output(SWIG_FromCharPtr($1->c_str()));
	if(PyErr_Occurred())
		goto fail;
}


// unsigned char *, unsigned int
%typemap(in,numinputs=1) (unsigned char *, unsigned int) (int res, char *buf = 0, size_t size, int alloc = 0)
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



// unsigned int
%typemap(in,numinputs=0) unsigned int *OUTPUT (unsigned int u)
{
  $1 = &u;
}

%typemap(argout,fragment=SWIG_From_frag(unsigned int)) unsigned int *OUTPUT
{
  %append_output(SWIG_From(unsigned int)(*$1));
}



%apply std::vector<std::string> &OUTPUT						{std::vector<std::string > &lstCapabilities}
%apply std::string &OUTPUT									{std::string &lpstrSerial}
%apply (unsigned char *, unsigned int)						{(unsigned char *lpData, unsigned int ulSize)}
%apply (unsigned int *OUTPUT)								{unsigned int *lpulUserCount}


%fragment("SWIG_FromBytePtrAndSize","header",fragment="SWIG_FromCharPtrAndSize") {
SWIGINTERNINLINE PyObject *
SWIG_FromBytePtrAndSize(const unsigned char* carray, size_t size)
{
	return SWIG_FromCharPtrAndSize(reinterpret_cast<const char *>(carray), size);
}
}


%include <typemaps/cstrings.swg>
%typemaps_cstring(%bstring,
		 unsigned char,
		 SWIG_AsCharPtr,
		 SWIG_AsCharPtrAndSize,
		 SWIG_FromCharPtr,
		 SWIG_FromBytePtrAndSize);
%bstring_output_allocate_size(unsigned char **lpResponse, unsigned int *lpulResponseSize, delete[] *$1)

%include "std_string.i"

class ECLicenseClient {
public:
    ECLicenseClient(char *szLicensePath, unsigned int ulTimeOut);

	%extend {
		ECRESULT GetCapabilities(const char *lpszServiceType, std::vector<std::string > &lstCapabilities) {
			ECRESULT er;
			int serviceType;

			if (lpszServiceType == NULL)
				return KCERR_INVALID_PARAMETER;
			
			er = ServiceTypeStringToServiceType(lpszServiceType, serviceType);
			if (er == erSuccess)
				er = self->GetCapabilities(serviceType, lstCapabilities);

			return er;
		}
		
		ECRESULT GetSerial(const char *lpszServiceType, std::string &lpstrSerial, std::vector<std::string> &OUTPUT /*lstCALs*/) {
			ECRESULT er;
			int serviceType;

			if (lpszServiceType == NULL)
				return KCERR_INVALID_PARAMETER;
			
			er = ServiceTypeStringToServiceType(lpszServiceType, serviceType);
			if (er == erSuccess)
				er = self->GetSerial(serviceType, lpstrSerial, OUTPUT /*lstCALs*/);

			return er;
		}
		
		ECRESULT GetInfo(const char *lpszServiceType, unsigned int *lpulUserCount) {
			ECRESULT er;
			int serviceType;

			if (lpszServiceType == NULL)
				return KCERR_INVALID_PARAMETER;
			
			er = ServiceTypeStringToServiceType(lpszServiceType, serviceType);
			if (er == erSuccess)
				er = self->GetInfo(serviceType, lpulUserCount);

			return er;
		}

		ECRESULT SetSerial(const char *lpszServiceType, const std::string &strSerial, const std::vector<std::string> &lstCALs, unsigned int *OUTPUT) {
			ECRESULT er;
			int serviceType;

			if (lpszServiceType == NULL)
				return KCERR_INVALID_PARAMETER;
			
			er = ServiceTypeStringToServiceType(lpszServiceType, serviceType);
			if (er == erSuccess)
				er = self->SetSerial(serviceType, strSerial, lstCALs);

			if (OUTPUT)
				*OUTPUT = 0;	// SWIG thinks it's a failure if you output nothing.

			return er;
		}
	}
	
	ECRESULT Auth(unsigned char *lpData, unsigned int ulSize, void **, unsigned int *);
};
