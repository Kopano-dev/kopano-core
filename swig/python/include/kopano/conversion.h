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

#ifndef CONVERSION_H
#define CONVERSION_H

#include <edkmdb.h>		// LPREADSTATE
#include <kopano/ECDefs.h>	// ECUSER
typedef ECUSERCLIENTUPDATESTATUS *LPECUSERCLIENTUPDATESTATUS;
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
extern int Object_is_LPSPropValue(PyObject *);
extern PyObject *List_from_SPropValue(const SPropValue *, ULONG n);
extern PyObject *List_from_LPSPropValue(const SPropValue *, ULONG n);
extern SPropValue *List_to_p_SPropValue(PyObject *, ULONG *nvals, ULONG flags = CONV_COPY_SHALLOW, void *base = nullptr);
extern SPropValue *List_to_LPSPropValue(PyObject *, ULONG *nvals, ULONG flags = CONV_COPY_SHALLOW, void *base = nullptr);

SPropTagArray *List_to_p_SPropTagArray(PyObject *sv, ULONG ulFlags = CONV_COPY_SHALLOW);
PyObject *List_from_SPropTagArray(const SPropTagArray *lpPropTagArray);
SPropTagArray *List_to_LPSPropTagArray(PyObject *sv, ULONG ulFlags = CONV_COPY_SHALLOW);
PyObject *List_from_LPSPropTagArray(const SPropTagArray *lpPropTagArray);

LPSRestriction	Object_to_LPSRestriction(PyObject *sv, void *lpBase = NULL);
void			Object_to_LPSRestriction(PyObject *sv, LPSRestriction lpsRestriction, void *lpBase = NULL);
PyObject *		Object_from_LPSRestriction(LPSRestriction lpRestriction);

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

LPADRPARM		Object_to_LPADRPARM(PyObject *av);

LPADRENTRY		Object_to_LPADRENTRY(PyObject *av);

PyObject *		List_from_LPSPropProblemArray(LPSPropProblemArray lpProblemArray);
LPSPropProblemArray List_to_LPSPropProblemArray(PyObject *, ULONG ulFlags = CONV_COPY_SHALLOW);

PyObject *		Object_from_LPMAPINAMEID(LPMAPINAMEID lpMAPINameId);
PyObject *		List_from_LPMAPINAMEID(LPMAPINAMEID *lppMAPINameId, ULONG cNames);
LPMAPINAMEID *	List_to_p_LPMAPINAMEID(PyObject *, ULONG *lpcNames, ULONG ulFlags = CONV_COPY_SHALLOW);

LPENTRYLIST		List_to_LPENTRYLIST(PyObject *);
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

PyObject *Object_from_LPECUSERCLIENTUPDATESTATUS(ECUSERCLIENTUPDATESTATUS *lpECUCUS);

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
