/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef CONVERSION_H
#define CONVERSION_H

#include <edkmdb.h>		// LPREADSTATE
#include <kopano/ECDefs.h>	// ECUSER
using namespace KC;
typedef ECSVRNAMELIST *LPECSVRNAMELIST;
typedef ECSERVERLIST *LPECSERVERLIST;
typedef ECQUOTASTATUS *LPECQUOTASTATUS;
typedef ECQUOTA *LPECQUOTA;
typedef ECGROUP *LPECGROUP;
typedef ECCOMPANY *LPECCOMPANY;
typedef ECUSER *LPECUSER;

#define CONV_COPY_SHALLOW	0
#define CONV_COPY_DEEP		1

typedef int(*TypeCheckFunc)(PyObject*);

FILETIME		Object_to_FILETIME(PyObject *object);
PyObject *		Object_from_FILETIME(FILETIME ft);

extern SPropValue *Object_to_p_SPropValue(PyObject *, ULONG flags = CONV_COPY_SHALLOW, void *base = nullptr);
extern SPropValue *Object_to_LPSPropValue(PyObject *, ULONG flags = CONV_COPY_SHALLOW, void *base = nullptr);
extern int Object_is_SPropValue(PyObject *);
extern PyObject *List_from_SPropValue(const SPropValue *, ULONG n);
extern PyObject *List_from_LPSPropValue(const SPropValue *, ULONG n);
extern SPropValue *List_to_p_SPropValue(PyObject *, ULONG *nvals, ULONG flags = CONV_COPY_SHALLOW, void *base = nullptr);
extern SPropValue *List_to_LPSPropValue(PyObject *, ULONG *nvals, ULONG flags = CONV_COPY_SHALLOW, void *base = nullptr);

SPropTagArray *List_to_p_SPropTagArray(PyObject *sv, ULONG ulFlags = CONV_COPY_SHALLOW);
PyObject *List_from_SPropTagArray(const SPropTagArray *lpPropTagArray);
SPropTagArray *List_to_LPSPropTagArray(PyObject *sv, ULONG ulFlags = CONV_COPY_SHALLOW);
PyObject *List_from_LPSPropTagArray(const SPropTagArray *lpPropTagArray);

extern SRestriction *Object_to_p_SRestriction(PyObject *, void *base = nullptr);
extern SRestriction *Object_to_LPSRestriction(PyObject *, void *base = nullptr);
void			Object_to_LPSRestriction(PyObject *sv, LPSRestriction lpsRestriction, void *lpBase = NULL);
extern PyObject *Object_from_SRestriction(const SRestriction *);
extern PyObject *Object_from_LPSRestriction(const SRestriction *);

PyObject *		Object_from_LPACTION(LPACTION lpAction);
PyObject *		Object_from_LPACTIONS(ACTIONS *lpsActions);
void			Object_to_LPACTION(PyObject *object, ACTION *lpAction, void *lpBase);
void			Object_to_LPACTIONS(PyObject *object, ACTIONS *lpActions, void *lpBase = NULL);

SSortOrderSet *Object_to_p_SSortOrderSet(PyObject *sv);
PyObject *Object_from_SSortOrderSet(const SSortOrderSet *lpSortOrderSet);

extern PyObject *List_from_SRowSet(const SRowSet *);
extern PyObject *List_from_LPSRowSet(const SRowSet *);
extern SRowSet *List_to_p_SRowSet(PyObject *, ULONG flags = CONV_COPY_SHALLOW, void *lpBase = nullptr);
extern SRowSet *List_to_LPSRowSet(PyObject *, ULONG flags = CONV_COPY_SHALLOW, void *lpBase = nullptr);
extern ADRLIST *List_to_p_ADRLIST(PyObject *, ULONG flags = CONV_COPY_SHALLOW, void *lpBase = nullptr);
extern ADRLIST *List_to_LPADRLIST(PyObject *, ULONG flags = CONV_COPY_SHALLOW, void *lpBase = nullptr);
extern PyObject *List_from_ADRLIST(const ADRLIST *);
extern PyObject *List_from_LPADRLIST(const ADRLIST *);

PyObject *		List_from_LPSPropProblemArray(LPSPropProblemArray lpProblemArray);
LPSPropProblemArray List_to_LPSPropProblemArray(PyObject *, ULONG ulFlags = CONV_COPY_SHALLOW);

PyObject *		Object_from_LPMAPINAMEID(LPMAPINAMEID lpMAPINameId);
PyObject *		List_from_LPMAPINAMEID(LPMAPINAMEID *lppMAPINameId, ULONG cNames);
LPMAPINAMEID *	List_to_p_LPMAPINAMEID(PyObject *, ULONG *lpcNames, ULONG ulFlags = CONV_COPY_SHALLOW);

extern ENTRYLIST *List_to_p_ENTRYLIST(PyObject *);
extern ENTRYLIST *List_to_LPENTRYLIST(PyObject *);
PyObject *		List_from_LPENTRYLIST(LPENTRYLIST lpEntryList);

LPNOTIFICATION	List_to_LPNOTIFICATION(PyObject *, ULONG *lpcNames);
PyObject *		List_from_LPNOTIFICATION(LPNOTIFICATION lpNotif, ULONG cNotifs);
PyObject *		Object_from_LPNOTIFICATION(NOTIFICATION *lpNotif);
NOTIFICATION *	Object_to_LPNOTIFICATION(PyObject *);

LPFlagList		List_to_LPFlagList(PyObject *);
PyObject *		List_from_LPFlagList(LPFlagList lpFlags);

LPMAPIERROR		Object_to_LPMAPIERROR(PyObject *);
PyObject *		Object_from_LPMAPIERROR(LPMAPIERROR lpMAPIError);

LPREADSTATE		List_to_LPREADSTATE(PyObject *, ULONG *lpcElements);
PyObject *		List_from_LPREADSTATE(LPREADSTATE lpReadState, ULONG cElements);

LPCIID 			List_to_LPCIID(PyObject *, ULONG *);
PyObject *		List_from_LPCIID(LPCIID iids, ULONG cElements);

ECUSER *Object_to_LPECUSER(PyObject *, ULONG ulFlags);
PyObject *Object_from_LPECUSER(ECUSER *lpUser, ULONG ulFlags);
PyObject *List_from_LPECUSER(ECUSER *lpUser, ULONG cElements, ULONG ulFlags);

ECGROUP *Object_to_LPECGROUP(PyObject *, ULONG ulFlags);
PyObject *Object_from_LPECGROUP(ECGROUP *lpGroup, ULONG ulFlags);
PyObject *List_from_LPECGROUP(ECGROUP *lpGroup, ULONG cElements, ULONG ulFlags);

ECCOMPANY *Object_to_LPECCOMPANY(PyObject *, ULONG ulFlags);
PyObject *Object_from_LPECCOMPANY(ECCOMPANY *lpCompany, ULONG ulFlags);
PyObject *List_from_LPECCOMPANY(ECCOMPANY *lpCompany, ULONG cElements, ULONG ulFlags);

ECQUOTA *Object_to_LPECQUOTA(PyObject *);
PyObject *Object_from_LPECQUOTA(ECQUOTA *lpQuota);

PyObject *Object_from_LPECQUOTASTATUS(ECQUOTASTATUS *lpQuotaStatus);
LPROWLIST		List_to_LPROWLIST(PyObject *, ULONG ulFlags = CONV_COPY_SHALLOW);

ECSVRNAMELIST *List_to_LPECSVRNAMELIST(PyObject *object);

PyObject *Object_from_LPECSERVER(ECSERVER *lpServer);
PyObject *List_from_LPECSERVERLIST(ECSERVERLIST *lpServerList);

void			Init();

void			DoException(HRESULT hr);
int				GetExceptionError(PyObject *, HRESULT *);

void			Object_to_STATSTG(PyObject *, STATSTG *);
PyObject *		Object_from_STATSTG(STATSTG *);

#endif // ndef CONVERSION_H
