%module icalmapi

%{
#include "ICalToMAPI.h"
#include "MAPIToICal.h"
#include <mapitovcf.hpp>
#include <vcftomapi.hpp>
%}

%include "std_string.i"
%include "cstring.i"
%include <kopano/typemap.i>

// Create class output parameters (both conversions)
%typemap(in,numinputs=0) (ICALMAPICLASS **) ($basetype *temp) {
	temp = NULL;
	$1 = &temp;
}
%typemap(argout) (ICALMAPICLASS **) {
  %append_output(SWIG_NewPointerObj((void*)*($1), $*1_descriptor, SWIG_SHADOW | SWIG_OWNER));
}
%apply ICALMAPICLASS **{ KC::ICalToMapi **, KC::MapiToICal **};
%apply ICALMAPICLASS **{ KC::vcftomapi **, KC::mapitovcf **};

/* GetItemInfo output parameters */
%typemap(in,numinputs=0) (eIcalType *) (eIcalType temp) {
	$1 = &temp;
}
%typemap(argout) eIcalType* value {
	%append_output(PyInt_FromLong(*$1));
}

%typemap(in,numinputs=0) (time_t *) (time_t temp) {
	$1 = &temp;
}
%typemap(argout) time_t* value {
	%append_output(PyInt_FromLong(*$1));
}

%typemap(in,numinputs=0) (SBinary *) (SBinary temp) {
	$1 = &temp;
}
%typemap(argout,fragment="SWIG_FromCharPtrAndSize") (SBinary* ) {
	%append_output(PyBytes_FromStringAndSize((const char *)$1->lpb, $1->cb));
}

/* Finalize output parameters */
%typemap(in,numinputs=0) (std::string *) (std::string temp) {
	$1 = &temp;
}
%typemap(argout) (std::string *) {
	%append_output(PyBytes_FromStringAndSize($1->c_str(), $1->length()));
}

%typemap(in) (const std::string &strIcal) (std::string temp, char *buf=NULL, Py_ssize_t size)
{
    if(PyBytes_AsStringAndSize($input, &buf, &size) == -1)
        %argument_fail(SWIG_ERROR,"$type",$symname, $argnum);

    temp = std::string(buf, size);
    $1 = &temp;
}

%typemap(freearg) (const std::string &strIcal) {
}

%apply const std::string &strIcal {const std::string &ical};

/* defines for the eIcalType */
#define VEVENT 0
#define VTODO 1
#define VJOURNAL 2

/* let swig know this is the same, so it cat get the conversion stuff from typemap.i */
typedef IAddrBook* LPADRBOOK;
typedef IMessage* LPMESSAGE;

%include <kopano/zcdefs.h>
%include "ICalToMAPI.h"
%include "MAPIToICal.h"
%include <mapitovcf.hpp>
%include <vcftomapi.hpp>
