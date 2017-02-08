%module(directors="1") kopanosync

%{
#include <mapix.h>
#include <mapidefs.h>
#include "ECSyncSettings.h"
#include "ECSyncLog.h"
%}

%include <kopano/typemap.i>

#if SWIGPYTHON
%include "ECLogger.i"
#endif

%include <kopano/zcdefs.h>
%include "ECSyncSettings.h"
%include "ECSyncLog.h"
