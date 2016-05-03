%module(directors="1") kopanosync

%{
#include <mapix.h>
#include <mapidefs.h>
#include "ECSync.h"
#include "ECSyncSettings.h"
#include "ECSyncLog.h"
%}

%include "std_string.i"
%include "std_wstring.i"
%include "cstring.i"
%include <kopano/typemap.i>

#ifdef HAVE_OFFLINE_SUPPORT

%typemap(in,numinputs=0) 	IECSync** ($basetype *temp)
	"temp = NULL; $1 = &temp;";
%typemap(argout)	IECSync**
{
  %append_output(SWIG_NewPointerObj((void*)*($1), $*1_descriptor, SWIG_SHADOW | SWIG_OWNER));
}

%typemap(in,numinputs=0)		ULONG *lpulStatus ($basetype temp)
	"temp = 0; $1 = &temp;";
%typemap(argout)			ULONG *lpulStatus
{
	%append_output(SWIG_From_long(*$1));
}

%{
static HRESULT PythonSyncProgressCallBack(void* lpObject, ULONG ulStep, ULONG ulSteps, double dTotalProgress, LPCWSTR szFolderName)
{
   PyObject *func, *arglist;
   PyObject *result;
   double    dres = 0;
   
   SWIG_PYTHON_THREAD_BEGIN_BLOCK;
   
   func = (PyObject *)lpObject;				     // Get Python function
   arglist = Py_BuildValue("(iidu)",ulStep,ulSteps,dTotalProgress,szFolderName);             // Build argument list
   result = PyEval_CallObject(func,arglist);     // Call Python
   Py_DECREF(arglist);                           // Trash arglist
   Py_XDECREF(result);

   SWIG_PYTHON_THREAD_END_BLOCK;
   return hrSuccess;
}

static HRESULT PythonSyncStatusCallBack(void* lpObject, ULONG ulStatus)
{
   PyObject *func, *arglist;
   PyObject *result;
   double    dres = 0;
   
   SWIG_PYTHON_THREAD_BEGIN_BLOCK;
   
   func = (PyObject *)lpObject;				     // Get Python function
   arglist = Py_BuildValue("(i)", ulStatus);     // Build argument list
   result = PyEval_CallObject(func,arglist);     // Call Python
   Py_DECREF(arglist);                           // Trash arglist
   Py_XDECREF(result);
   
   SWIG_PYTHON_THREAD_END_BLOCK;
   return hrSuccess;
}
%}

class IECSync
{
public:
	//static HRESULT Create(IMAPISession* lpSession, IECSync **lppECSync);
	
	HRESULT StartSync();
	HRESULT StopSync();
	HRESULT GetSyncStatus(ULONG * lpulStatus);
	
	%extend {
	    IECSync(IMAPISession* lpSession) { 
			IECSync *lpSync = NULL;
			CreateECSync(lpSession, &lpSync); 
			return lpSync;
	    }
	
		~IECSync() { self->Release(); }
		
		void SetSyncStatusCallBack(PyObject *pyfunc) {
			self->SetSyncStatusCallBack((void*)pyfunc, PythonSyncStatusCallBack);
			Py_INCREF(pyfunc);
		}
		
		void SetSyncProgressCallBack(PyObject *pyfunc) {
			self->SetSyncProgressCallBack((void*)pyfunc, PythonSyncProgressCallBack);
			Py_INCREF(pyfunc);
		}
	}

private:
	ECSync();
};

%include "windows.i"

#endif // HAVE_OFFLINE_SUPPORT

#if SWIGPYTHON
%include "ECLogger.i"
#endif

%include "ECLibSync.h"
%include "ECSyncSettings.h"
%include "ECSyncLog.h"
