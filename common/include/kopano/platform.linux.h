/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef PLATFORM_LINUX_H
#define PLATFORM_LINUX_H

#ifndef UNICODE
#        error But KC libmapi only knows to deal with UNICODE
#endif

/*
 * Functions and (type)definitions that are needed for the Linux platform
 *
 */
#include <kopano/zcdefs.h>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <cstring>		/* memset, etc.. */
#include <cctype>		/* 64-bit int definition */
#include <dlfcn.h>
#include <cstddef>
#include <libgen.h>
#include <byteswap.h>
#include <cerrno>
#include <clocale>

#define __STDC_FORMAT_MACROS
#if defined(__cplusplus) && __cplusplus >= 200300L
#	include <cstdint>
#else
#	include <stdint.h>
#endif
#include <string>
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif

#ifndef UNUSED_VAR
#define UNUSED_VAR __attribute__((unused))
#endif

#include <cassert>
#define _vsnprintf vsnprintf
#ifdef HAVE_VSNPRINTF_L
#define _vsnprintf_l vsnprintf_l
#else
inline int _vsnprintf_l(char *str, size_t size, const char *format, locale_t locale, va_list ap) {
	locale_t prev = uselocale(locale);
	int ret = _vsnprintf(str, size, format, ap);
	uselocale(prev);
	return ret;
}
#endif

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/times.h>

/* base types */
typedef void			VOID;
typedef unsigned char	BYTE;
typedef unsigned char	UCHAR;
typedef signed char		CHAR;
typedef BYTE*			LPBYTE;
typedef unsigned short	WORD;
typedef WORD*			LPWORD;
typedef unsigned int	DWORD;
typedef DWORD*			LPDWORD;
typedef unsigned int	UINT;
typedef unsigned int	ULONG;
typedef ULONG*			LPULONG;
typedef int64_t		LONGLONG;
typedef LONGLONG*		LPLONGLONG;
typedef uint64_t	ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef int	LONG;
typedef int	BOOL;
typedef void* LPVOID;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef __int64_t __int64;

#ifndef FALSE
  #define FALSE	0
#endif
#ifndef TRUE
  #define TRUE	1
#endif

/* This is a workaround for warnings in offsetof from stddef.h */
#define offsetof_static(TYPE, MEMBER) (reinterpret_cast<uintptr_t>(&((TYPE *)1)->MEMBER)-1)

#define container_of(ptr, type, member) \
	reinterpret_cast<type *>(reinterpret_cast<char *>(ptr) - offsetof_static(type, member))

/* find parent class */
#define METHOD_PROLOGUE_(theClass, localClass) \
	UNUSED_VAR auto pThis = container_of(this, theClass, m_x##localClass)

/* GUID defines */
struct GUID {
    DWORD	Data1;
    WORD	Data2;
    WORD	Data3;
    BYTE	Data4[8];
};
typedef GUID*  LPGUID;
typedef const GUID *LPCGUID;
typedef GUID   IID;
typedef IID*   LPIID;
typedef GUID   CLSID;
typedef CLSID* LPCLSID;
typedef GUID   FMTID;
typedef FMTID* LPFMTID;

#define REFIID   const IID &
#define REFCLSID const CLSID &

typedef GUID  UUID;		// needed? existing?

/* See initguid.h for the real defines */
#ifndef INITGUID
/* # ifdef __cplusplus */
/* #   define EXTERN_C extern "C" */
/* # else */
/* #   define EXTERN_C extern */
/* # endif */
  #define GUID_EXT extern "C"
  #define DEFINE_GUID(n,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) GUID_EXT _kc_export const GUID n
#endif

#define DEFINE_OLEGUID(n,l,w1,w2) DEFINE_GUID(n,l,w1,w2,0xC0,0,0,0,0,0,0,0x46)

//
//  Code Page Default Values.
//
#define CP_ACP                    0           // default to ANSI code page
#define CP_OEMCP                  1           // default to OEM  code page
#define CP_MACCP                  2           // default to MAC  code page
#define CP_THREAD_ACP             3           // current thread's ANSI code page
#define CP_SYMBOL                 42          // SYMBOL translations

#define CP_UTF7                   65000       // UTF-7 translation
#define CP_UTF8                   65001       // UTF-8 translation

//
//  MBCS and Unicode Translation Flags.
//
#define MB_PRECOMPOSED            0x00000001  // use precomposed chars
#define MB_COMPOSITE              0x00000002  // use composite chars
#define MB_USEGLYPHCHARS          0x00000004  // use glyph chars, not ctrl chars
#define MB_ERR_INVALID_CHARS      0x00000008  // error for invalid chars

//
// IStream flags
//
#define STGM_DIRECT             0x00000000L
#define STGM_TRANSACTED         0x00010000L
#define STGM_SIMPLE             0x08000000L
#define STGM_READ               0x00000000L
#define STGM_WRITE              0x00000001L
#define STGM_READWRITE          0x00000002L
#define STGM_SHARE_DENY_NONE    0x00000040L
#define STGM_SHARE_DENY_READ    0x00000030L
#define STGM_SHARE_DENY_WRITE   0x00000020L
#define STGM_SHARE_EXCLUSIVE    0x00000010L
#define STGM_PRIORITY           0x00040000L
#define STGM_DELETEONRELEASE    0x04000000L
#define STGM_NOSCRATCH          0x00100000L
#define STGM_CREATE             0x00001000L
#define STGM_CONVERT            0x00020000L
#define STGM_FAILIFTHERE        0x00000000L
#define STGM_NOSNAPSHOT         0x00200000L
#define STGM_DIRECT_SWMR        0x00400000L

/* correct? */
typedef unsigned int	HINSTANCE;
typedef unsigned int	HANDLE;
typedef int				HRESULT;
typedef int				SCODE;

#ifndef MAX_PATH
  #define MAX_PATH	255
#endif

#define SEVERITY_SUCCESS	0
#define SEVERITY_ERROR		1
#define IS_ERROR(scode)	((unsigned int)(scode) >> 31 == SEVERITY_ERROR)
#define FAILED(scode)	((unsigned int)(scode) >> 31)
#define SUCCEEDED(scode) (!FAILED(scode))

/* from winerror.h, same as MAKE_MAPI_SCODE */
#define MAKE_SCODE(sev,fac,code) \
    ((SCODE) (((ULONG)(sev)<<31) | ((ULONG)(fac)<<16) | ((ULONG)(code))) )

/* success codes */
#define S_OK		0
#define S_FALSE		1

/* predefined hresult errors */
#define E_UNEXPECTED ((HRESULT)0x8000FFFF)
#define E_NOTIMPL ((HRESULT)0x80004001)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define E_INVALIDARG ((HRESULT)0x80070057)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define E_POINTER ((HRESULT)0x80004003)
#define E_HANDLE ((HRESULT)0x80070006)
#define E_ABORT ((HRESULT)0x80004004)
#define E_FAIL ((HRESULT)0x80004005)
#define E_ACCESSDENIED ((HRESULT)0x80070005)

/* winerror.h definitions */
#define KC_HRESULT_TYPEDEF_(_sc) ((HRESULT)_sc)
#define STG_E_INVALIDFUNCTION KC_HRESULT_TYPEDEF_(0x80030001L)

/*
 Values are 32 bit values laid out as follows:

  3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 +---+-+-+-----------------------+-------------------------------+
 |Sev|C|R|     Facility          |               Code            |
 +---+-+-+-----------------------+-------------------------------+

 where

     Sev - is the severity code

         00 - Success
         01 - Informational
         10 - Warning
         11 - Error

     C - is the Customer code flag

     R - is a reserved bit

     Facility - is the facility code

     Code - is the facility's status code
*/
#define FACILITY_NULL		0x0000
#define FACILITY_RPC		0x0001
#define FACILITY_DISPATCH	0x0002
#define FACILITY_STORAGE	0x0003
#define FACILITY_ITF		0x0004 /* only used in mapi programs */
#define FACILITY_WIN32		0x0007
#define FACILITY_WINDOWS	0x0008
#define FACILITY_SSPI		0x0009
#define FACILITY_CONTROL	0x000A

/* mapi.h (and more) types */
typedef unsigned int	FLAGS;
typedef unsigned int	LHANDLE;
typedef unsigned int*	LPLHANDLE;

/* mapidefs.h (and more) types */
typedef wchar_t 	WCHAR;
typedef WCHAR		TCHAR;
#define KC_T(x) L##x

namespace KC {
typedef std::basic_string<TCHAR> tstring;
}
typedef WCHAR*			LPWSTR;
typedef const WCHAR*	LPCWSTR;
typedef TCHAR*			LPTSTR;
typedef const TCHAR*	LPCTSTR;
typedef WCHAR			OLECHAR;

struct FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
};
typedef struct FILETIME *LPFILETIME;
#define NANOSECS_BETWEEN_EPOCHS 116444736000000000LL /* units of 100 ns! */

/* made up .. seems correct */
union LARGE_INTEGER {
  __extension__ struct {
    DWORD LowPart;
    LONG HighPart;
  };
  struct {
    DWORD LowPart;
    LONG HighPart;
  } u;
  LONGLONG QuadPart;
};
typedef union LARGE_INTEGER *PLARGE_INTEGER;

union ULARGE_INTEGER {
  __extension__ struct {
    DWORD LowPart;
    DWORD HighPart;
  };
  struct {
    DWORD LowPart;
    DWORD HighPart;
  } u;
  ULONGLONG QuadPart;
};
typedef union ULARGE_INTEGER *PULARGE_INTEGER;

/* #define lpszA	char* */
/* #define lppszA	char** */
/* #define MVszA	char** */

/* #define lpszW	WCHAR* */
/* #define lppszW	WCHAR** */
/* #define MVszW	WCHAR** */

// [MS-DTYP].pdf
struct SYSTEMTIME {
	WORD wYear;
	WORD wMonth;
	WORD wDayOfWeek;
	WORD wDay;
	WORD wHour;
	WORD wMinute;
	WORD wSecond;
	WORD wMilliseconds;
};
typedef struct SYSTEMTIME *PSYSTEMTIME;

struct STATSTG {
    LPSTR pwcsName;		// was LPOLESTR .. wtf is that?
    DWORD type;
    ULARGE_INTEGER cbSize;
    FILETIME mtime;
    FILETIME ctime;
    FILETIME atime;
    DWORD grfMode;
    DWORD grfLocksSupported;
    CLSID clsid;
    DWORD grfStateBits;
    DWORD reserved;
};

enum STGTY {
    STGTY_STORAGE	= 1,
    STGTY_STREAM	= 2,
    STGTY_LOCKBYTES	= 3,
    STGTY_PROPERTY	= 4
};

enum STREAM_SEEK {
    STREAM_SEEK_SET	= 0,
    STREAM_SEEK_CUR	= 1,
    STREAM_SEEK_END	= 2
};

enum STATFLAG {
    STATFLAG_DEFAULT	= 0,
    STATFLAG_NONAME	= 1,
    STATFLAG_NOOPEN	= 2
};

// struct RemSNB {
// unsigned long ulCntStr;
//     unsigned long ulCntChar;
//     //OLECHAR rgString[ 1 ];
//     } RemSNB;
// typedef RemSNB* wireSNB;
typedef OLECHAR** SNB;

/*
  extra typedefs used in following interfaces
  since these are not used, we can define them as void, instead of the large struct it is in WIN32
*/
typedef DWORD LCID;

/* functions */
extern _kc_export bool operator!=(const GUID &, const GUID &) noexcept;
extern _kc_export bool operator==(REFIID, const GUID &) noexcept;

extern "C" {

extern _kc_export HRESULT CoCreateGuid(LPGUID);
extern _kc_export void GetSystemTimeAsFileTime(FILETIME *);

#define _tcslen	wcslen
#define _tcscpy wcscpy
#define _tcscmp wcscmp
#define _tcsicmp wcscasecmp

extern _kc_export void Sleep(unsigned int usec);

/* because the flags are not used linux, they do not match the windows flags! */
#define GPTR 0
extern _kc_export void * GlobalAlloc(UINT flags, ULONG size);

} /* extern "C" */

typedef void * DLIB;
#ifdef __APPLE__
  #define UNUSEDXX_REAL_DLL_NAME		"libkcclient.dylib"
  #define SHARED_OBJECT_EXTENSION	"so"
#else
  #define UNUSEDXX_REAL_DLL_NAME		"libkcclient.so"
  #define SHARED_OBJECT_EXTENSION	"so"
#endif
#define PATH_SEPARATOR				'/'

namespace KC {

extern _kc_export void le_to_cpu(SYSTEMTIME &);
extern _kc_export time_t GetProcessTime(void);

#define GetTickCount() 0L

#define TICKS_PER_SEC (sysconf(_SC_CLK_TCK))

/**
 * Tell the compiler that a function/method behaves like printf.
 * _fmt is the 1-indexed index to the format argument
 * _va is the 1-indexed index to the first va argument.
 * NOTE: For non-static methods the this pointer has index 1.
 * There is no wprintf attribute :-(
 */
#define KC_LIKE_PRINTF(fmt, va) __attribute__((format(printf, fmt, va)))

std::vector<std::string> get_backtrace();

} /* namespace */

#endif // PLATFORM_LINUX_H
