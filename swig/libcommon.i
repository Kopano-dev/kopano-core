%module libcommon

%{
    #include <kopano/platform.h>
    #include <kopano/memory.hpp>
    #include <mapi.h>
    #include <mapidefs.h>
    #include <mapicode.h>
    #include <mapiutil.h>

    #include "HtmlToTextParser.h"
    #include "rtfutil.h"
    #include "favoritesutil.h"
    #include <kopano/Util.h>
    #include <kopano/memory.hpp>
	#include <kopano/ECLogger.h>
    #include "fileutil.h"
	#include "IStreamAdapter.h"
    // FIXME: why cant we get this from typemap_python
    #if PY_MAJOR_VERSION >= 3
        #define PyString_AsStringAndSize(value, input, size) \
                PyBytes_AsStringAndSize(value, input, size)
    #endif
%}

%include "wchar.i"
%include "cstring.i"
%include "cwstring.i"
%include "std_string.i"
%include <kopano/typemap.i>

class CHtmlToTextParser {
    public:
        bool Parse(wchar_t *in);
        std::wstring& GetText();
        
        %extend {
            wchar_t *__str__() {
                return (wchar_t*)$self->GetText().c_str();
            }

            wchar_t *GetData() {
                return (wchar_t*)$self->GetText().c_str();
            }
        }

};

// std::string&

%typemap(in) (std::string) (char *buf=NULL, Py_ssize_t size)
{
    if(PyBytes_AsStringAndSize($input, &buf, &size) == -1)
        %argument_fail(SWIG_ERROR,"$type",$symname, $argnum);

    $1 = std::string(buf, size);
}

%typemap(in,numinputs=0) std::string &OUTPUT	(std::string s)
{
	$1 = &s;
}
%typemap(argout,fragment="SWIG_FromCharPtr") (std::string &OUTPUT)
{
	%append_output(PyBytes_FromString($1->c_str()));
	if(PyErr_Occurred())
		goto fail;
}

%typemap(in,numinputs=0) std::wstring &OUTPUT	(std::wstring s)
{
	$1 = &s;
}
%typemap(argout,fragment="SWIG_FromWCharPtr") (std::wstring &OUTPUT)
{
	%append_output(SWIG_FromWCharPtr($1->c_str()));
	if(PyErr_Occurred())
		goto fail;
}

// some common/rtfutil.h functions
HRESULT HrExtractHTMLFromRTF(std::string lpStrRTFIn, std::string &OUTPUT, ULONG ulCodepage);
HRESULT HrExtractHTMLFromTextRTF(std::string lpStrRTFIn, std::string &OUTPUT, ULONG ulCodepage);
HRESULT HrExtractHTMLFromRealRTF(std::string lpStrRTFIn, std::string &OUTPUT, ULONG ulCodepage);
HRESULT HrExtractBODYFromTextRTF(std::string lpStrRTFIn, std::wstring &OUTPUT);

// functions from favoritesutil.h
HRESULT GetShortcutFolder(IMAPISession *lpSession, LPTSTR lpszFolderName, LPTSTR lpszFolderComment, ULONG ulFlags, IMAPIFolder **lppShortcutFolder);

HRESULT DelFavoriteFolder(IMAPIFolder *lpShortcutFolder, LPSPropValue lpPropSourceKey);
HRESULT AddFavoriteFolder(IMAPIFolder *lpShortcutFolder, IMAPIFolder *lpFolder, LPTSTR lpszAliasName, ULONG ulFlags);

HRESULT GetConfigMessage(IMsgStore *lpStore, char *szMessageName, IMessage **OUTPUT);

// functions from common/Util.h
class Util {
public:
    static ULONG GetBestBody(IMAPIProp *lpPropObj, ULONG ulFlags);
       %extend {
               /* static ULONG GetBestBody(LPSPropValue lpProps, ULONG cValues, ULONG ulFlags); */
               /* swapped because typemap.i only implements (cValues, lpProps) */
               static ULONG GetBestBody(ULONG cValues, LPSPropValue lpProps, ULONG ulFlags) { return Util::GetBestBody(lpProps, cValues, ulFlags); }
       }
};

%feature("notabstract") IStreamAdapter;

// this is a way to make std::string objects that you can pass to the IStreamAdapter
%{
std::string *new_StdString(char *szData) { return new std::string(szData); }
void delete_StdString(std::string *string) { delete string; }
typedef std::string std_string;
%}

class std_string {
public:
	%extend {
		std_string(char *szData) { return new_StdString(szData); }
		~std_string() { delete self; };
	}
};

%cstring_input_binary(const char *pv, ULONG cb);

class IStreamAdapter {
public:
	// Hard to typemap so using other method below
    // virtual HRESULT Read(void *OUTPUT, ULONG cb, ULONG *OUTPUT) = 0;
    virtual HRESULT Write(const char *pv, ULONG cb, ULONG *OUTPUT) = 0;
	%extend {
		HRESULT Read(ULONG cb, char **lpOutput, ULONG *ulRead) {
			char *buffer;
			HRESULT hr = MAPIAllocateBuffer(cb, (void **)&buffer);

			if(hr != hrSuccess)
				return hr;

			self->Read(buffer, cb, ulRead);

			*lpOutput = buffer;
			return hrSuccess;
		}
    }
    virtual HRESULT Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) = 0;
    virtual HRESULT SetSize(ULARGE_INTEGER libNewSize) = 0;
    virtual HRESULT CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) = 0;
    virtual HRESULT Commit(DWORD grfCommitFlags) = 0;
    virtual HRESULT Revert() = 0;
    virtual HRESULT LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) = 0;
    virtual HRESULT UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) = 0;
    virtual HRESULT Stat(STATSTG *pstatstg, DWORD grfStatFlag) = 0;
    virtual HRESULT Clone(IStream **ppstm) = 0;

	%extend {
		IStreamAdapter(std_string *strData) {
			IStreamAdapter *lpStream = new IStreamAdapter(*(std::string *)strData);
			return lpStream;
		}
		~IStreamAdapter() {
			self->Release();
		}
	}
};

