/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECWSUTIL_H
#define ECWSUTIL_H

#include <mapidefs.h>
#include <mapicode.h>
#include "kcore.hpp"
#include <kopano/kcodes.h>
#include "ECMsgStore.h"

namespace KC {
class convert_context;
}

class KCmdProxy;

extern HRESULT CopyMAPIPropValToSOAPPropVal(propVal *dst, const SPropValue *src, KC::convert_context * = nullptr);
extern HRESULT CopySOAPPropValToMAPIPropVal(SPropValue *dst, const struct propVal *src, void *base, KC::convert_context * = nullptr);
extern HRESULT CopySOAPRowToMAPIRow(void *prov, const struct propValArray *src, SPropValue *dst, void **base, ULONG type, KC::convert_context * = nullptr);
HRESULT CopySOAPRowSetToMAPIRowSet(void *lpProvider, const struct rowSet *lpsRowSetSrc, LPSRowSet *lppRowSetDst, ULONG ulType);
extern HRESULT CopySOAPRestrictionToMAPIRestriction(SRestriction *dst, const struct restrictTable *src, void *base, KC::convert_context * = nullptr);
extern HRESULT CopyMAPIRestrictionToSOAPRestriction(struct restrictTable **dst, const SRestriction *src, KC::convert_context * = nullptr);
extern HRESULT CopyMAPIRowSetToSOAPRowSet(const SRowSet *src, struct rowSet **dst, KC::convert_context * = nullptr);
extern HRESULT CopyMAPIRowToSOAPRow(const SRow *src, struct propValArray *dst, KC::convert_context * = nullptr);
extern HRESULT CopySOAPRowToMAPIRow(const struct propValArray *src, SPropValue *dst, void *base, KC::convert_context * = nullptr);
HRESULT CopyMAPIEntryIdToSOAPEntryId(ULONG cbEntryIdSrc, const ENTRYID *lpEntryIdSrc, entryId **lppDest);
HRESULT CopyMAPIEntryIdToSOAPEntryId(ULONG cbEntryIdSrc, const ENTRYID *lpEntryIdSrc, entryId *lpDest, bool bCheapCopy = false);
HRESULT CopySOAPEntryIdToMAPIEntryId(const entryId *lpSrc, ULONG *lpcbDest, LPENTRYID *lppEntryIdDest, void *lpBase = NULL);
HRESULT CopySOAPEntryIdToMAPIEntryId(const entryId *lpSrc, ULONG ulObjId, ULONG *lpcbDest, LPENTRYID *lppEntryIdDest, void *lpBase = NULL);
HRESULT CopySOAPEntryIdToMAPIEntryId(const entryId *lpSrc, ULONG ulObjId, ULONG ulType, ULONG *lpcbDest, LPENTRYID *lppEntryIdDest, void *lpBase = NULL);
HRESULT CopyMAPIEntryListToSOAPEntryList(const ENTRYLIST *lpMsgList, struct entryList *lpsEntryList);
HRESULT CopySOAPEntryListToMAPIEntryList(const struct entryList *lpsEntryList, LPENTRYLIST *lppMsgList);
HRESULT FreeABProps(struct propmapPairArray *lpsoapPropmap, struct propmapMVPairArray *lpsoapMVPropmap);
extern HRESULT CopyABPropsToSoap(const KC::SPROPMAP *, const KC::MVPROPMAP *, ULONG flags, struct propmapPairArray **, struct propmapMVPairArray **);
extern HRESULT CopyABPropsFromSoap(const struct propmapPairArray *, const struct propmapMVPairArray *, KC::SPROPMAP *, KC::MVPROPMAP *, void *base, ULONG flags);
extern HRESULT SoapUserArrayToUserArray(const struct userArray *, ULONG flags, ULONG *nusers, KC::ECUSER **);
extern HRESULT SoapUserToUser(const struct user *, ULONG flags, KC::ECUSER **);
extern HRESULT SoapGroupArrayToGroupArray(const struct groupArray *, ULONG flags, ULONG *ngrp, KC::ECGROUP **);
extern HRESULT SoapGroupToGroup(const struct group *, ULONG flags, KC::ECGROUP **);
extern HRESULT SoapCompanyArrayToCompanyArray(const struct companyArray *, ULONG flags, ULONG *ncomp, KC::ECCOMPANY **);
extern HRESULT SoapCompanyToCompany(const struct company *, ULONG flags, KC::ECCOMPANY **);
extern HRESULT SvrNameListToSoapMvString8(KC::ECSVRNAMELIST *, ULONG flags, struct mv_string8 **);
extern HRESULT SoapServerListToServerList(const struct serverList *, ULONG flags, KC::ECSERVERLIST **);
extern HRESULT CreateSoapTransport(const sGlobalProfileProps &, KCmdProxy **);
extern HRESULT WrapServerClientStoreEntry(const char *server_name, const entryId *store_id, ULONG *sid_size, ENTRYID **sid);
extern HRESULT UnWrapServerClientStoreEntry(ULONG sid_size, const ENTRYID *sid, ULONG *unwrap_sid_size, ENTRYID **unwrap_sid);
extern HRESULT UnWrapServerClientABEntry(ULONG abid_size, const ENTRYID *abid, ULONG *unwrap_abid_size, ENTRYID **unwrap_abid);
extern HRESULT CopySOAPNotificationToMAPINotification(void *prov, const struct notification *src, NOTIFICATION **dst, KC::convert_context * = nullptr);
extern HRESULT CopySOAPChangeNotificationToSyncState(const struct notification *src, SBinary **dst, void *base);
extern HRESULT CopyICSChangeToSOAPSourceKeys(ULONG cbChanges, const ICSCHANGE *lpsChanges, sourceKeyPairArray **lppsSKPA);
extern HRESULT Utf8ToTString(const char *utf, ULONG flags, void *bsae, KC::convert_context *, TCHAR **);
HRESULT ConvertString8ToUnicode(LPSRowSet lpRowSet);
extern HRESULT ConvertString8ToUnicode(SRow *row, void *base, KC::convert_context &);

#endif
