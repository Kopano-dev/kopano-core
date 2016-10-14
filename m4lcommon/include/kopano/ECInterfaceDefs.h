/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/* C99-style: anonymous argument referenced by __VA_ARGS__, empty arg not OK */

// @todo: Document

#include <kopano/ECDebugPrint.h>
#include <string>

#define N_ARGS(...) N_ARGS_HELPER1((__VA_ARGS__, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))
#define N_ARGS_HELPER1(tuple) N_ARGS_HELPER2 tuple
#define N_ARGS_HELPER2(x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17, x18, x19, x20, n, ...) n


#define ARGLISTSET(...)					ARGLISTSET_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define ARGLISTSET_HELPER1(n, ...)		ARGLISTSET_HELPER2(n, (__VA_ARGS__))
#define ARGLISTSET_HELPER2(n, tuple)	ARGLISTSET_HELPER3(n, tuple)
#define ARGLISTSET_HELPER3(n, tuple)	ARGLISTSET_##n tuple

#define ARGLISTSET_1(v)
#define ARGLISTSET_2(t1, a1)			t1 a1
#define ARGLISTSET_4(t1, a1, t2, a2)	t1 a1, t2 a2


#define ARGLIST(...)				ARGLIST_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define ARGLIST_HELPER1(n, ...)		ARGLIST_HELPER2(n, (__VA_ARGS__))
#define ARGLIST_HELPER2(n, tuple)	ARGLIST_HELPER3(n, tuple)
#define ARGLIST_HELPER3(n, tuple)	ARGLIST_ ##n tuple

#define ARGLIST_1(s1)												ARGLISTSET s1 
#define ARGLIST_2(s1, s2)											ARGLISTSET s1, ARGLISTSET s2 
#define ARGLIST_3(s1, s2, s3)										ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3
#define ARGLIST_4(s1, s2, s3, s4)									ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4
#define ARGLIST_5(s1, s2, s3, s4, s5)								ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5
#define ARGLIST_6(s1, s2, s3, s4, s5, s6)							ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5, ARGLISTSET s6
#define ARGLIST_7(s1, s2, s3, s4, s5, s6, s7)						ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5, ARGLISTSET s6, ARGLISTSET s7
#define ARGLIST_8(s1, s2, s3, s4, s5, s6, s7, s8)					ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5, ARGLISTSET s6, ARGLISTSET s7, ARGLISTSET s8
#define ARGLIST_9(s1, s2, s3, s4, s5, s6, s7, s8, s9)				ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5, ARGLISTSET s6, ARGLISTSET s7, ARGLISTSET s8, ARGLISTSET s9
#define ARGLIST_10(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10)			ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5, ARGLISTSET s6, ARGLISTSET s7, ARGLISTSET s8, ARGLISTSET s9, ARGLISTSET s10
#define ARGLIST_11(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11)	ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5, ARGLISTSET s6, ARGLISTSET s7, ARGLISTSET s8, ARGLISTSET s9, ARGLISTSET s10, ARGLISTSET s11


#define ARGSSET(...)				ARGSSET_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define ARGSSET_HELPER1(n, ...)		ARGSSET_HELPER2(n, (__VA_ARGS__))
#define ARGSSET_HELPER2(n, tuple)	ARGSSET_HELPER3(n, tuple)
#define ARGSSET_HELPER3(n, tuple)	ARGSSET_##n tuple

#define ARGSSET_1(v)
#define ARGSSET_2(t1, a1)			a1
#define ARGSSET_4(t1, a1, t2, a2)	a1, a2


#define ARGS(...)					ARGS_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define ARGS_HELPER1(n, ...)		ARGS_HELPER2(n, (__VA_ARGS__))
#define ARGS_HELPER2(n, tuple)		ARGS_HELPER3(n, tuple)
#define ARGS_HELPER3(n, tuple)		ARGS_##n tuple

#define ARGS_1(s1)												ARGSSET s1 
#define ARGS_2(s1, s2)											ARGSSET s1, ARGSSET s2 
#define ARGS_3(s1, s2, s3)										ARGSSET s1, ARGSSET s2, ARGSSET s3
#define ARGS_4(s1, s2, s3, s4)									ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4
#define ARGS_5(s1, s2, s3, s4, s5)								ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5
#define ARGS_6(s1, s2, s3, s4, s5, s6)							ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5, ARGSSET s6
#define ARGS_7(s1, s2, s3, s4, s5, s6, s7)						ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5, ARGSSET s6, ARGSSET s7
#define ARGS_8(s1, s2, s3, s4, s5, s6, s7, s8)					ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5, ARGSSET s6, ARGSSET s7, ARGSSET s8
#define ARGS_9(s1, s2, s3, s4, s5, s6, s7, s8, s9)				ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5, ARGSSET s6, ARGSSET s7, ARGSSET s8, ARGSSET s9
#define ARGS_10(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10)		ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5, ARGSSET s6, ARGSSET s7, ARGSSET s8, ARGSSET s9, ARGSSET s10
#define ARGS_11(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11)	ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5, ARGSSET s6, ARGSSET s7, ARGSSET s8, ARGSSET s9, ARGSSET s10, ARGSSET s11



#define FORMAT_ARGSSET(...)					FORMAT_ARGSSET_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define FORMAT_ARGSSET_HELPER1(n, ...)		FORMAT_ARGSSET_HELPER2(n, (__VA_ARGS__))
#define FORMAT_ARGSSET_HELPER2(n, tuple)	FORMAT_ARGSSET_HELPER3(n, tuple)
#define FORMAT_ARGSSET_HELPER3(n, tuple)	FORMAT_ARGSSET_##n tuple

#define FORMAT_ARGSSET_1(v)					""
#define FORMAT_ARGSSET_2(t1, a1)			#a1 "=%s"
#define FORMAT_ARGSSET_4(t1, a1, t2, a2)	"("#a1","#a2")=%s"


#define FORMAT_ARGS(...)				FORMAT_ARGS_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define FORMAT_ARGS_HELPER1(n, ...)		FORMAT_ARGS_HELPER2(n, (__VA_ARGS__))
#define FORMAT_ARGS_HELPER2(n, tuple)	FORMAT_ARGS_HELPER3(n, tuple)
#define FORMAT_ARGS_HELPER3(n, tuple)	FORMAT_ARGS_##n tuple

#define FORMAT_ARGS_1(s1)												FORMAT_ARGSSET s1 
#define FORMAT_ARGS_2(s1, s2)											FORMAT_ARGSSET s1 ", " FORMAT_ARGSSET s2 
#define FORMAT_ARGS_3(s1, s2, s3)										FORMAT_ARGSSET s1 ", " FORMAT_ARGSSET s2 ", " FORMAT_ARGSSET s3
#define FORMAT_ARGS_4(s1, s2, s3, s4)									FORMAT_ARGSSET s1 ", " FORMAT_ARGSSET s2 ", " FORMAT_ARGSSET s3 ", " FORMAT_ARGSSET s4
#define FORMAT_ARGS_5(s1, s2, s3, s4, s5)								FORMAT_ARGSSET s1 ", " FORMAT_ARGSSET s2 ", " FORMAT_ARGSSET s3 ", " FORMAT_ARGSSET s4 ", " FORMAT_ARGSSET s5
#define FORMAT_ARGS_6(s1, s2, s3, s4, s5, s6)							FORMAT_ARGSSET s1 ", " FORMAT_ARGSSET s2 ", " FORMAT_ARGSSET s3 ", " FORMAT_ARGSSET s4 ", " FORMAT_ARGSSET s5 ", " FORMAT_ARGSSET s6
#define FORMAT_ARGS_7(s1, s2, s3, s4, s5, s6, s7)						FORMAT_ARGSSET s1 ", " FORMAT_ARGSSET s2 ", " FORMAT_ARGSSET s3 ", " FORMAT_ARGSSET s4 ", " FORMAT_ARGSSET s5 ", " FORMAT_ARGSSET s6 ", " FORMAT_ARGSSET s7
#define FORMAT_ARGS_8(s1, s2, s3, s4, s5, s6, s7, s8)					FORMAT_ARGSSET s1 ", " FORMAT_ARGSSET s2 ", " FORMAT_ARGSSET s3 ", " FORMAT_ARGSSET s4 ", " FORMAT_ARGSSET s5 ", " FORMAT_ARGSSET s6 ", " FORMAT_ARGSSET s7 ", " FORMAT_ARGSSET s8
#define FORMAT_ARGS_9(s1, s2, s3, s4, s5, s6, s7, s8, s9)				FORMAT_ARGSSET s1 ", " FORMAT_ARGSSET s2 ", " FORMAT_ARGSSET s3 ", " FORMAT_ARGSSET s4 ", " FORMAT_ARGSSET s5 ", " FORMAT_ARGSSET s6 ", " FORMAT_ARGSSET s7 ", " FORMAT_ARGSSET s8 ", " FORMAT_ARGSSET s9
#define FORMAT_ARGS_10(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10)			FORMAT_ARGSSET s1 ", " FORMAT_ARGSSET s2 ", " FORMAT_ARGSSET s3 ", " FORMAT_ARGSSET s4 ", " FORMAT_ARGSSET s5 ", " FORMAT_ARGSSET s6 ", " FORMAT_ARGSSET s7 ", " FORMAT_ARGSSET s8 ", " FORMAT_ARGSSET s9 ", " FORMAT_ARGSSET s10
#define FORMAT_ARGS_11(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11)	FORMAT_ARGSSET s1 ", " FORMAT_ARGSSET s2 ", " FORMAT_ARGSSET s3 ", " FORMAT_ARGSSET s4 ", " FORMAT_ARGSSET s5 ", " FORMAT_ARGSSET s6 ", " FORMAT_ARGSSET s7 ", " FORMAT_ARGSSET s8 ", " FORMAT_ARGSSET s9 ", " FORMAT_ARGSSET s10 ", " FORMAT_ARGSSET s11



#define PRINT_ARGSSET_IN(...)				PRINT_ARGSSET_IN_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define PRINT_ARGSSET_IN_HELPER1(n, ...)	PRINT_ARGSSET_IN_HELPER2(n, (__VA_ARGS__))
#define PRINT_ARGSSET_IN_HELPER2(n, tuple)	PRINT_ARGSSET_IN_HELPER3(n, tuple)
#define PRINT_ARGSSET_IN_HELPER3(n, tuple)	PRINT_ARGSSET_IN_##n tuple

#define PRINT_ARGSSET_IN_1(v)					NULL
#define PRINT_ARGSSET_IN_2(t1, a1)				ECDebugPrint<std::string, ECDebugPrintBase::NoDeref>::toString(a1).c_str()
#define PRINT_ARGSSET_IN_4(t1, a1, t2, a2)		ECDebugPrint<std::string, ECDebugPrintBase::NoDeref>::toString(a1, a2).c_str()



#define PRINT_ARGSSET_OUT(...)				PRINT_ARGSSET_OUT_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define PRINT_ARGSSET_OUT_HELPER1(n, ...)	PRINT_ARGSSET_OUT_HELPER2(n, (__VA_ARGS__))
#define PRINT_ARGSSET_OUT_HELPER2(n, tuple)	PRINT_ARGSSET_OUT_HELPER3(n, tuple)
#define PRINT_ARGSSET_OUT_HELPER3(n, tuple)	PRINT_ARGSSET_OUT_##n tuple

#define PRINT_ARGSSET_OUT_1(v)					NULL
#define PRINT_ARGSSET_OUT_2(t1, a1)				ECDebugPrint<std::string, ECDebugPrintBase::Deref>::toString(a1).c_str()
#define PRINT_ARGSSET_OUT_4(t1, a1, t2, a2)		ECDebugPrint<std::string, ECDebugPrintBase::Deref>::toString(a1, a2).c_str()



#define PRINT_ARGS_IN(...)				PRINT_ARGS_IN_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define PRINT_ARGS_IN_HELPER1(n, ...)	PRINT_ARGS_IN_HELPER2(n, (__VA_ARGS__))
#define PRINT_ARGS_IN_HELPER2(n, tuple)	PRINT_ARGS_IN_HELPER3(n, tuple)
#define PRINT_ARGS_IN_HELPER3(n, tuple)	PRINT_ARGS_IN_##n tuple 

#define PRINT_ARGS_IN_1(s1)													PRINT_ARGSSET_IN s1 
#define PRINT_ARGS_IN_2(s1, s2)												PRINT_ARGSSET_IN s1, PRINT_ARGSSET_IN s2 
#define PRINT_ARGS_IN_3(s1, s2, s3)											PRINT_ARGSSET_IN s1, PRINT_ARGSSET_IN s2, PRINT_ARGSSET_IN s3
#define PRINT_ARGS_IN_4(s1, s2, s3, s4)										PRINT_ARGSSET_IN s1, PRINT_ARGSSET_IN s2, PRINT_ARGSSET_IN s3, PRINT_ARGSSET_IN s4
#define PRINT_ARGS_IN_5(s1, s2, s3, s4, s5)									PRINT_ARGSSET_IN s1, PRINT_ARGSSET_IN s2, PRINT_ARGSSET_IN s3, PRINT_ARGSSET_IN s4, PRINT_ARGSSET_IN s5
#define PRINT_ARGS_IN_6(s1, s2, s3, s4, s5, s6)								PRINT_ARGSSET_IN s1, PRINT_ARGSSET_IN s2, PRINT_ARGSSET_IN s3, PRINT_ARGSSET_IN s4, PRINT_ARGSSET_IN s5, PRINT_ARGSSET_IN s6
#define PRINT_ARGS_IN_7(s1, s2, s3, s4, s5, s6, s7)							PRINT_ARGSSET_IN s1, PRINT_ARGSSET_IN s2, PRINT_ARGSSET_IN s3, PRINT_ARGSSET_IN s4, PRINT_ARGSSET_IN s5, PRINT_ARGSSET_IN s6, PRINT_ARGSSET_IN s7
#define PRINT_ARGS_IN_8(s1, s2, s3, s4, s5, s6, s7, s8)						PRINT_ARGSSET_IN s1, PRINT_ARGSSET_IN s2, PRINT_ARGSSET_IN s3, PRINT_ARGSSET_IN s4, PRINT_ARGSSET_IN s5, PRINT_ARGSSET_IN s6, PRINT_ARGSSET_IN s7, PRINT_ARGSSET_IN s8
#define PRINT_ARGS_IN_9(s1, s2, s3, s4, s5, s6, s7, s8, s9)					PRINT_ARGSSET_IN s1, PRINT_ARGSSET_IN s2, PRINT_ARGSSET_IN s3, PRINT_ARGSSET_IN s4, PRINT_ARGSSET_IN s5, PRINT_ARGSSET_IN s6, PRINT_ARGSSET_IN s7, PRINT_ARGSSET_IN s8, PRINT_ARGSSET_IN s9
#define PRINT_ARGS_IN_10(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10)			PRINT_ARGSSET_IN s1, PRINT_ARGSSET_IN s2, PRINT_ARGSSET_IN s3, PRINT_ARGSSET_IN s4, PRINT_ARGSSET_IN s5, PRINT_ARGSSET_IN s6, PRINT_ARGSSET_IN s7, PRINT_ARGSSET_IN s8, PRINT_ARGSSET_IN s9, PRINT_ARGSSET_IN s10
#define PRINT_ARGS_IN_11(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11)		PRINT_ARGSSET_IN s1, PRINT_ARGSSET_IN s2, PRINT_ARGSSET_IN s3, PRINT_ARGSSET_IN s4, PRINT_ARGSSET_IN s5, PRINT_ARGSSET_IN s6, PRINT_ARGSSET_IN s7, PRINT_ARGSSET_IN s8, PRINT_ARGSSET_IN s9, PRINT_ARGSSET_IN s10, PRINT_ARGSSET_IN s11



#define PRINT_ARGS_OUT(...)					PRINT_ARGS_OUT_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define PRINT_ARGS_OUT_HELPER1(n, ...)		PRINT_ARGS_OUT_HELPER2(n, (__VA_ARGS__))
#define PRINT_ARGS_OUT_HELPER2(n, tuple)	PRINT_ARGS_OUT_HELPER3(n, tuple)
#define PRINT_ARGS_OUT_HELPER3(n, tuple)	PRINT_ARGS_OUT_##n tuple 

#define PRINT_ARGS_OUT_1(s1)												PRINT_ARGSSET_OUT s1 
#define PRINT_ARGS_OUT_2(s1, s2)											PRINT_ARGSSET_OUT s1, PRINT_ARGSSET_OUT s2 
#define PRINT_ARGS_OUT_3(s1, s2, s3)										PRINT_ARGSSET_OUT s1, PRINT_ARGSSET_OUT s2, PRINT_ARGSSET_OUT s3
#define PRINT_ARGS_OUT_4(s1, s2, s3, s4)									PRINT_ARGSSET_OUT s1, PRINT_ARGSSET_OUT s2, PRINT_ARGSSET_OUT s3, PRINT_ARGSSET_OUT s4
#define PRINT_ARGS_OUT_5(s1, s2, s3, s4, s5)								PRINT_ARGSSET_OUT s1, PRINT_ARGSSET_OUT s2, PRINT_ARGSSET_OUT s3, PRINT_ARGSSET_OUT s4, PRINT_ARGSSET_OUT s5
#define PRINT_ARGS_OUT_6(s1, s2, s3, s4, s5, s6)							PRINT_ARGSSET_OUT s1, PRINT_ARGSSET_OUT s2, PRINT_ARGSSET_OUT s3, PRINT_ARGSSET_OUT s4, PRINT_ARGSSET_OUT s5, PRINT_ARGSSET_OUT s6
#define PRINT_ARGS_OUT_7(s1, s2, s3, s4, s5, s6, s7)						PRINT_ARGSSET_OUT s1, PRINT_ARGSSET_OUT s2, PRINT_ARGSSET_OUT s3, PRINT_ARGSSET_OUT s4, PRINT_ARGSSET_OUT s5, PRINT_ARGSSET_OUT s6, PRINT_ARGSSET_OUT s7
#define PRINT_ARGS_OUT_8(s1, s2, s3, s4, s5, s6, s7, s8)					PRINT_ARGSSET_OUT s1, PRINT_ARGSSET_OUT s2, PRINT_ARGSSET_OUT s3, PRINT_ARGSSET_OUT s4, PRINT_ARGSSET_OUT s5, PRINT_ARGSSET_OUT s6, PRINT_ARGSSET_OUT s7, PRINT_ARGSSET_OUT s8
#define PRINT_ARGS_OUT_9(s1, s2, s3, s4, s5, s6, s7, s8, s9)				PRINT_ARGSSET_OUT s1, PRINT_ARGSSET_OUT s2, PRINT_ARGSSET_OUT s3, PRINT_ARGSSET_OUT s4, PRINT_ARGSSET_OUT s5, PRINT_ARGSSET_OUT s6, PRINT_ARGSSET_OUT s7, PRINT_ARGSSET_OUT s8, PRINT_ARGSSET_OUT s9
#define PRINT_ARGS_OUT_10(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10)			PRINT_ARGSSET_OUT s1, PRINT_ARGSSET_OUT s2, PRINT_ARGSSET_OUT s3, PRINT_ARGSSET_OUT s4, PRINT_ARGSSET_OUT s5, PRINT_ARGSSET_OUT s6, PRINT_ARGSSET_OUT s7, PRINT_ARGSSET_OUT s8, PRINT_ARGSSET_OUT s9, PRINT_ARGSSET_OUT s10
#define PRINT_ARGS_OUT_11(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11)		PRINT_ARGSSET_OUT s1, PRINT_ARGSSET_OUT s2, PRINT_ARGSSET_OUT s3, PRINT_ARGSSET_OUT s4, PRINT_ARGSSET_OUT s5, PRINT_ARGSSET_OUT s6, PRINT_ARGSSET_OUT s7, PRINT_ARGSSET_OUT s8, PRINT_ARGSSET_OUT s9, PRINT_ARGSSET_OUT s10, PRINT_ARGSSET_OUT s11



#define XCLASS(_iface)	x ##_iface
#define ICLASS(_iface)	I ##_iface
#define CLASSMETHOD(_class, _method)	_class::_method
#define METHODSTR(_iface, _method)	METHODSTR_HELPER1(CLASSMETHOD(ICLASS(_iface), _method))
#define METHODSTR_HELPER1(_method)	METHODSTR_HELPER2(_method)
#define METHODSTR_HELPER2(_method)	#_method

#define DEF_ULONGMETHOD(_trace, _class, _iface, _method, ...)														\
ULONG __stdcall CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__))	{			\
	_trace(TRACE_ENTRY, METHODSTR(_iface, _method), FORMAT_ARGS(__VA_ARGS__), PRINT_ARGS_IN( __VA_ARGS__));			\
	ULONG ul = 0;																						\
	try {																										\
		METHOD_PROLOGUE_(_class, _iface);																		\
		ul = pThis->_method(ARGS(__VA_ARGS__));																	\
	} catch (const std::bad_alloc &) {																			\
		ul = -1;																			\
	}																											\
	_trace(TRACE_RETURN, METHODSTR(_iface, _method), "SUCCESS: " FORMAT_ARGS(__VA_ARGS__), PRINT_ARGS_OUT(__VA_ARGS__));	\
	return ul;																									\
}

#define DEF_ULONGMETHOD1(_trace, _class, _iface, _method, ...) \
ULONG __stdcall CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__))	\
{ \
	_trace(TRACE_ENTRY, METHODSTR(_iface, _method), FORMAT_ARGS(__VA_ARGS__), PRINT_ARGS_IN( __VA_ARGS__)); \
	ULONG ul = 0; \
	METHOD_PROLOGUE_(_class, _iface); \
	ul = pThis->_method(ARGS(__VA_ARGS__)); \
	_trace(TRACE_RETURN, METHODSTR(_iface, _method), "SUCCESS: " FORMAT_ARGS(__VA_ARGS__), PRINT_ARGS_OUT(__VA_ARGS__)); \
	return ul; \
}

#define DEF_ULONGMETHOD0(_class, _iface, _method, ...) \
ULONG __stdcall CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__))	\
{ \
	METHOD_PROLOGUE_(_class, _iface); \
	return pThis->_method(ARGS(__VA_ARGS__)); \
}

#define DEF_HRMETHOD(_trace, _class, _iface, _method, ...)														\
HRESULT __stdcall CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__))	{			\
	_trace(TRACE_ENTRY, METHODSTR(_iface, _method), FORMAT_ARGS(__VA_ARGS__), PRINT_ARGS_IN( __VA_ARGS__));			\
	HRESULT	hr = hrSuccess;																						\
	try {																										\
		METHOD_PROLOGUE_(_class, _iface);																		\
		hr = pThis->_method(ARGS(__VA_ARGS__));																	\
	} catch (const std::bad_alloc &) {																			\
		hr = MAPI_E_NOT_ENOUGH_MEMORY;																			\
	}																											\
	if (FAILED(hr))																								\
		_trace(TRACE_RETURN, METHODSTR(_iface, _method), "FAILED: %s", GetMAPIErrorDescription(hr).c_str());	\
	else																										\
		_trace(TRACE_RETURN, METHODSTR(_iface, _method), "SUCCESS: " FORMAT_ARGS(__VA_ARGS__), PRINT_ARGS_OUT(__VA_ARGS__));	\
	return hr;																									\
}

/* without exception passthrough */
#define DEF_HRMETHOD1(_trace, _class, _iface, _method, ...)														\
HRESULT __stdcall CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__)) \
{ \
	_trace(TRACE_ENTRY, METHODSTR(_iface, _method), FORMAT_ARGS(__VA_ARGS__), PRINT_ARGS_IN( __VA_ARGS__)); \
	METHOD_PROLOGUE_(_class, _iface); \
	HRESULT hr = pThis->_method(ARGS(__VA_ARGS__)); \
	if (FAILED(hr)) \
		_trace(TRACE_RETURN, METHODSTR(_iface, _method), "FAILED: %s", GetMAPIErrorDescription(hr).c_str()); \
	else \
		_trace(TRACE_RETURN, METHODSTR(_iface, _method), "SUCCESS: " FORMAT_ARGS(__VA_ARGS__), PRINT_ARGS_OUT(__VA_ARGS__)); \
	return hr; \
}

/* and without tracing */
#define DEF_HRMETHOD0(_class, _iface, _method, ...) \
HRESULT __stdcall CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__)) \
{ \
	METHOD_PROLOGUE_(_class, _iface); \
	return pThis->_method(ARGS(__VA_ARGS__)); \
}

#define DEF_HRMETHOD_EX(_trace, _class, _iface, _extra_fmt, _extra_arg, _method, ...)														\
HRESULT __stdcall CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__))	{			\
	METHOD_PROLOGUE_(_class, _iface);																		\
	_trace(TRACE_ENTRY, METHODSTR(_iface, _method), _extra_fmt ", " FORMAT_ARGS(__VA_ARGS__), _extra_arg, PRINT_ARGS_IN( __VA_ARGS__));			\
	HRESULT	hr = hrSuccess;																						\
	try {																										\
		hr = pThis->_method(ARGS(__VA_ARGS__));																	\
	} catch (const std::bad_alloc &) {																			\
		hr = MAPI_E_NOT_ENOUGH_MEMORY;																			\
	}																											\
	if (FAILED(hr))																								\
		_trace(TRACE_RETURN, METHODSTR(_iface, _method), "FAILED: %s " _extra_fmt, GetMAPIErrorDescription(hr).c_str(), _extra_arg);	\
	else																										\
		_trace(TRACE_RETURN, METHODSTR(_iface, _method), "SUCCESS: " _extra_fmt ", " FORMAT_ARGS(__VA_ARGS__), _extra_arg, PRINT_ARGS_OUT(__VA_ARGS__));	\
	return hr;																									\
}

#define DEF_HRMETHOD_EX2(_trace, _class, _iface, _extra_fmt, _extra_arg1, _extra_arg2, _method, ...)														\
HRESULT __stdcall CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__))	{			\
	METHOD_PROLOGUE_(_class, _iface);																		\
	_trace(TRACE_ENTRY, METHODSTR(_iface, _method), _extra_fmt ", " FORMAT_ARGS(__VA_ARGS__), _extra_arg1, _extra_arg2, PRINT_ARGS_IN( __VA_ARGS__));			\
	HRESULT	hr = hrSuccess;																						\
	try {																										\
		hr = pThis->_method(ARGS(__VA_ARGS__));																	\
	} catch (const std::bad_alloc &) {																			\
		hr = MAPI_E_NOT_ENOUGH_MEMORY;																			\
	}																											\
	if (FAILED(hr))																								\
		_trace(TRACE_RETURN, METHODSTR(_iface, _method), "FAILED: %s " _extra_fmt, GetMAPIErrorDescription(hr).c_str(), _extra_arg1, _extra_arg2);	\
	else																										\
		_trace(TRACE_RETURN, METHODSTR(_iface, _method), "SUCCESS: " _extra_fmt ", " FORMAT_ARGS(__VA_ARGS__), _extra_arg1, _extra_arg2, PRINT_ARGS_OUT(__VA_ARGS__));	\
	return hr;																									\
}

#define DEF_HRMETHOD_FORWARD(_trace, _class, _iface, _method, _member, ...)														\
HRESULT __stdcall CLASSMETHOD(_class, _method)(ARGLIST(__VA_ARGS__))	{			\
	_trace(TRACE_ENTRY, METHODSTR(_iface, _method), FORMAT_ARGS(__VA_ARGS__), PRINT_ARGS_IN( __VA_ARGS__));			\
	HRESULT	hr = hrSuccess;																						\
	try {																										\
		hr = _member->_method(ARGS(__VA_ARGS__));																	\
	} catch (const std::bad_alloc &) {																			\
		hr = MAPI_E_NOT_ENOUGH_MEMORY;																			\
	}																											\
	if (FAILED(hr))																								\
		_trace(TRACE_RETURN, METHODSTR(_iface, _method), "FAILED: %s", GetMAPIErrorDescription(hr).c_str());	\
	else																										\
		_trace(TRACE_RETURN, METHODSTR(_iface, _method), "SUCCESS: " FORMAT_ARGS(__VA_ARGS__), PRINT_ARGS_OUT(__VA_ARGS__));	\
	return hr;																									\
}

#define DEF_HRMETHOD_NOSUPPORT(_trace, _class, _iface, _method, ...)														\
HRESULT __stdcall CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__))	{			\
	_trace(TRACE_ENTRY, METHODSTR(_iface, _method), FORMAT_ARGS(__VA_ARGS__), PRINT_ARGS_IN( __VA_ARGS__));			\
	HRESULT	hr = MAPI_E_NO_SUPPORT;																						\
	_trace(TRACE_RETURN, METHODSTR(_iface, _method), "FAILED: %s", GetMAPIErrorDescription(hr).c_str());	\
	return hr;																									\
}

#define DEF_HRMETHOD0_NOSUPPORT(_class, _iface, _method, ...) \
HRESULT __stdcall CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__)) \
{ \
	return MAPI_E_NO_SUPPORT; \
}
