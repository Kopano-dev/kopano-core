/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
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

#define XCLASS(_iface)	x ##_iface
#define CLASSMETHOD(_class, _method)	_class::_method
#define METHODSTR_HELPER1(_method)	METHODSTR_HELPER2(_method)
#define METHODSTR_HELPER2(_method)	#_method

#define DEF_ULONGMETHOD1(_trace, _class, _iface, _method, ...) \
ULONG CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__))	\
{ \
	METHOD_PROLOGUE_(_class, _iface); \
	return pThis->_method(ARGS(__VA_ARGS__)); \
}

/* without exception passthrough */
#define DEF_HRMETHOD1(_trace, _class, _iface, _method, ...)														\
HRESULT CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__)) \
{ \
	METHOD_PROLOGUE_(_class, _iface); \
	return pThis->_method(ARGS(__VA_ARGS__)); \
}
