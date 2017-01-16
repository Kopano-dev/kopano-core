%module icalmapi

%{
#include "ICalToMAPI.h"
#include "MAPIToICal.h"
#include "VCFToMAPI.h"
#include "MAPIToVCF.h"
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
%apply ICALMAPICLASS **{ KC::VCFToMapi **, KC::MapiToVCF **};

/* GetItemInfo output parameters */
%typemap(in,numinputs=0) (eIcalType *) (eIcalType temp) {
	$1 = &temp;
}
%typemap(argout) eIcalType* value {
	%append_output(SWIG_From_long(*$1));
}

%typemap(in,numinputs=0) (time_t *) (time_t temp) {
	$1 = &temp;
}
%typemap(argout) time_t* value {
	%append_output(SWIG_From_long(*$1));
}

%typemap(in,numinputs=0) (SBinary *) (SBinary temp) {
	$1 = &temp;
}
%typemap(argout,fragment="SWIG_FromCharPtrAndSize") (SBinary* ) {
	%append_output(SWIG_FromCharPtrAndSize((const char *)$1->lpb, $1->cb));
}

/* Finalize output parameters */
%typemap(in,numinputs=0) (std::string *) (std::string temp) {
	$1 = &temp;
}
%typemap(argout) (std::string *) {
	/* @todo fix this not to go through a cstring */
	%append_output(SWIG_FromCharPtrAndSize($1->c_str(), $1->length()));
}

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
%include "VCFToMAPI.h"
%include "MAPIToVCF.h"
