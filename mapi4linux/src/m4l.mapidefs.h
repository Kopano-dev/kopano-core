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

#ifndef __M4L_MAPIDEFS_IMPL_H
#define __M4L_MAPIDEFS_IMPL_H

#include <kopano/zcdefs.h>
#include "m4l.common.h"
#include <mapidefs.h>
#include <mapispi.h>
#include <list>
#include <map>

using namespace std;

class M4LMsgServiceAdmin;

class M4LMAPIProp : public M4LUnknown, public virtual IMAPIProp {
private:
    // variables
    list<LPSPropValue> properties;

public:
    virtual ~M4LMAPIProp();

    virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _zcp_override;
    virtual HRESULT __stdcall SaveChanges(ULONG ulFlags) _zcp_override;
    virtual HRESULT __stdcall GetProps(LPSPropTagArray lpPropTagArray, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppPropArray) _zcp_override;
    virtual HRESULT __stdcall GetPropList(ULONG ulFlags, LPSPropTagArray *lppPropTagArray) _zcp_override;
    virtual HRESULT __stdcall OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk) _zcp_override;
    virtual HRESULT __stdcall SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall DeleteProps(LPSPropTagArray lpPropTagArray, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall GetNamesFromIDs(LPSPropTagArray *lppPropTags, LPGUID lpPropSetGuid, ULONG ulFlags, ULONG *lpcPropNames, LPMAPINAMEID **lpppPropNames) _zcp_override;
    virtual HRESULT __stdcall GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID* lppPropNames, ULONG ulFlags, LPSPropTagArray *lppPropTags) _zcp_override;

    // iunknown passthru
    virtual ULONG __stdcall AddRef(void) _zcp_override;
    virtual ULONG __stdcall Release(void) _zcp_override;
    virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _zcp_override;
};

class M4LProfSect _kc_final : public IProfSect, public M4LMAPIProp {
private:
	BOOL bGlobalProf;
public:
    M4LProfSect(BOOL bGlobalProf = FALSE);

    virtual HRESULT __stdcall ValidateState(ULONG ulUIParam, ULONG ulFlags);
    virtual HRESULT __stdcall SettingsDialog(ULONG ulUIParam, ULONG ulFlags);
    virtual HRESULT __stdcall ChangePassword(LPTSTR lpOldPass, LPTSTR lpNewPass, ULONG ulFlags);
    virtual HRESULT __stdcall FlushQueues(ULONG ulUIParam, ULONG cbTargetTransport, LPENTRYID lpTargetTransport, ULONG ulFlags);

    // imapiprop passthru
    virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _zcp_override;
    virtual HRESULT __stdcall SaveChanges(ULONG ulFlags) _zcp_override;
    virtual HRESULT __stdcall GetProps(LPSPropTagArray lpPropTagArray, ULONG ulFlags, ULONG* lpcValues, LPSPropValue *lppPropArray) _zcp_override;
    virtual HRESULT __stdcall GetPropList(ULONG ulFlags, LPSPropTagArray *lppPropTagArray) _zcp_override;
    virtual HRESULT __stdcall OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk) _zcp_override;
    virtual HRESULT __stdcall SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall DeleteProps(LPSPropTagArray lpPropTagArray, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall GetNamesFromIDs(LPSPropTagArray *lppPropTags, LPGUID lpPropSetGuid, ULONG ulFlags, ULONG *lpcPropNames, LPMAPINAMEID **lpppPropNames) _zcp_override;
    virtual HRESULT __stdcall GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG ulFlags, LPSPropTagArray *lppPropTags) _zcp_override;

    // iunknown passthru
    virtual ULONG __stdcall AddRef(void) _zcp_override;
    virtual ULONG __stdcall Release(void) _zcp_override;
    virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _zcp_override;
};

class M4LMAPITable _kc_final : public M4LUnknown, public IMAPITable {
private:

public:
    virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _zcp_override;
    virtual HRESULT __stdcall Advise(ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection) _zcp_override;
    virtual HRESULT __stdcall Unadvise(ULONG ulConnection) _zcp_override;
    virtual HRESULT __stdcall GetStatus(ULONG *lpulTableStatus, ULONG *lpulTableType) _zcp_override;
    virtual HRESULT __stdcall SetColumns(LPSPropTagArray lpPropTagArray, ULONG ulFlags) _zcp_override;
    virtual HRESULT __stdcall QueryColumns(ULONG ulFlags, LPSPropTagArray *lpPropTagArray) _zcp_override;
    virtual HRESULT __stdcall GetRowCount(ULONG ulFlags, ULONG *lpulCount) _zcp_override;
    virtual HRESULT __stdcall SeekRow(BOOKMARK bkOrigin, LONG lRowCount, LONG *lplRowsSought) _zcp_override;
    virtual HRESULT __stdcall SeekRowApprox(ULONG ulNumerator, ULONG ulDenominator) _zcp_override;
    virtual HRESULT __stdcall QueryPosition(ULONG *lpulRow, ULONG *lpulNumerator, ULONG *lpulDenominator) _zcp_override;
    virtual HRESULT __stdcall FindRow(LPSRestriction lpRestriction, BOOKMARK bkOrigin, ULONG ulFlags) _zcp_override;
    virtual HRESULT __stdcall Restrict(LPSRestriction lpRestriction, ULONG ulFlags) _zcp_override;
    virtual HRESULT __stdcall CreateBookmark(BOOKMARK *lpbkPosition) _zcp_override;
    virtual HRESULT __stdcall FreeBookmark(BOOKMARK bkPosition) _zcp_override;
    virtual HRESULT __stdcall SortTable(LPSSortOrderSet lpSortCriteria, ULONG ulFlags) _zcp_override;
    virtual HRESULT __stdcall QuerySortOrder(LPSSortOrderSet *lppSortCriteria) _zcp_override;
    virtual HRESULT __stdcall QueryRows(LONG lRowCount, ULONG ulFlags, LPSRowSet *lppRows) _zcp_override;
    virtual HRESULT __stdcall Abort(void) _zcp_override;
    virtual HRESULT __stdcall ExpandRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulRowCount, ULONG ulFlags, LPSRowSet *lppRows, ULONG *lpulMoreRows) _zcp_override;
    virtual HRESULT __stdcall CollapseRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulFlags, ULONG *lpulRowCount) _zcp_override;
    virtual HRESULT __stdcall WaitForCompletion(ULONG ulFlags, ULONG ulTimeout, ULONG *lpulTableStatus) _zcp_override;
    virtual HRESULT __stdcall GetCollapseState(ULONG ulFlags, ULONG cbInstanceKey, LPBYTE lpbInstanceKey, ULONG *lpcbCollapseState, LPBYTE *lppbCollapseState) _zcp_override;
    virtual HRESULT __stdcall SetCollapseState(ULONG ulFlags, ULONG cbCollapseState, LPBYTE pbCollapseState, BOOKMARK *lpbkLocation) _zcp_override;

    // iunknown passthru
    virtual ULONG __stdcall AddRef(void) _zcp_override;
    virtual ULONG __stdcall Release(void) _zcp_override;
    virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _zcp_override;
};

class M4LProviderAdmin _kc_final : public M4LUnknown , public IProviderAdmin {
private:
	M4LMsgServiceAdmin* msa;
	char *szService;

public:
    M4LProviderAdmin(M4LMsgServiceAdmin* new_ma, char *szService);
    virtual ~M4LProviderAdmin();

    virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _zcp_override;
    virtual HRESULT __stdcall GetProviderTable(ULONG ulFlags, LPMAPITABLE *lppTable) _zcp_override;
    virtual HRESULT __stdcall CreateProvider(LPTSTR lpszProvider, ULONG cValues, LPSPropValue lpProps, ULONG ulUIParam, ULONG ulFlags, MAPIUID *lpUID) _zcp_override;
    virtual HRESULT __stdcall DeleteProvider(LPMAPIUID lpUID) _zcp_override;
    virtual HRESULT __stdcall OpenProfileSection(LPMAPIUID lpUID, LPCIID lpInterface, ULONG ulFlags, LPPROFSECT *lppProfSect) _zcp_override;

    // iunknown passthru
    virtual ULONG __stdcall AddRef(void) _zcp_override;
    virtual ULONG __stdcall Release(void) _zcp_override;
    virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _zcp_override;
};

class M4LMAPIAdviseSink _kc_final : public M4LUnknown, public IMAPIAdviseSink {
private:
    void *lpContext;
    LPNOTIFCALLBACK lpFn;

public:
    M4LMAPIAdviseSink(LPNOTIFCALLBACK lpFn, void *lpContext);
    virtual ULONG __stdcall OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifications) _zcp_override;

    // iunknown passthru
    virtual ULONG __stdcall AddRef(void) _zcp_override;
    virtual ULONG __stdcall Release(void) _zcp_override;
    virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _zcp_override;
};


/* for ABContainer */
class M4LMAPIContainer : public M4LMAPIProp, public virtual IMAPIContainer {
public:
    virtual HRESULT __stdcall GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable) _zcp_override;
    virtual HRESULT __stdcall GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable) _zcp_override;
    virtual HRESULT __stdcall OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk) _zcp_override;
    virtual HRESULT __stdcall SetSearchCriteria(LPSRestriction lpRestriction, LPENTRYLIST lpContainerList, ULONG ulSearchFlags) _zcp_override;
    virtual HRESULT __stdcall GetSearchCriteria(ULONG ulFlags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState) _zcp_override;

    // imapiprop passthru
    virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _zcp_override;
    virtual HRESULT __stdcall SaveChanges(ULONG ulFlags) _zcp_override;
    virtual HRESULT __stdcall GetProps(LPSPropTagArray lpPropTagArray, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppPropArray) _zcp_override;
    virtual HRESULT __stdcall GetPropList(ULONG ulFlags, LPSPropTagArray *lppPropTagArray) _zcp_override;
    virtual HRESULT __stdcall OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk) _zcp_override;
    virtual HRESULT __stdcall SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall DeleteProps(LPSPropTagArray lpPropTagArray, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall GetNamesFromIDs(LPSPropTagArray *lppPropTags, LPGUID lpPropSetGuid, ULONG ulFlags, ULONG *lpcPropNames, LPMAPINAMEID **lpppPropNames) _zcp_override;
    virtual HRESULT __stdcall GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG ulFlags, LPSPropTagArray *lppPropTags) _zcp_override;

    // iunknown passthru
    virtual ULONG __stdcall AddRef(void) _zcp_override;
    virtual ULONG __stdcall Release(void) _zcp_override;
    virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _zcp_override;
};


typedef struct _s_abentry {
	MAPIUID muid;
	string displayname;
	LPABPROVIDER lpABProvider;
	LPABLOGON lpABLogon;
} abEntry;

class M4LABContainer _kc_final : public IABContainer, public M4LMAPIContainer {
private:
	/*  */
	const std::list<abEntry> &m_lABEntries;

public:
	M4LABContainer(const std::list<abEntry> &lABEntries);

	virtual HRESULT __stdcall CreateEntry(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulCreateFlags, LPMAPIPROP *lppMAPIPropEntry) _zcp_override;
	virtual HRESULT __stdcall CopyEntries(LPENTRYLIST lpEntries, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags) _zcp_override;
	virtual HRESULT __stdcall DeleteEntries(LPENTRYLIST lpEntries, ULONG ulFlags) _zcp_override;
	virtual HRESULT __stdcall ResolveNames(LPSPropTagArray lpPropTagArray, ULONG ulFlags, LPADRLIST lpAdrList, LPFlagList lpFlagList) _zcp_override;

	// imapicontainer passthu
    virtual HRESULT __stdcall GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable) _zcp_override;
    virtual HRESULT __stdcall GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable) _zcp_override;
    virtual HRESULT __stdcall OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk) _zcp_override;
    virtual HRESULT __stdcall SetSearchCriteria(LPSRestriction lpRestriction, LPENTRYLIST lpContainerList, ULONG ulSearchFlags) _zcp_override;
    virtual HRESULT __stdcall GetSearchCriteria(ULONG ulFlags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState) _zcp_override;

    // imapiprop passthru
    virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _zcp_override;
    virtual HRESULT __stdcall SaveChanges(ULONG ulFlags) _zcp_override;
    virtual HRESULT __stdcall GetProps(LPSPropTagArray lpPropTagArray, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppPropArray) _zcp_override;
    virtual HRESULT __stdcall GetPropList(ULONG ulFlags, LPSPropTagArray *lppPropTagArray) _zcp_override;
    virtual HRESULT __stdcall OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk) _zcp_override;
    virtual HRESULT __stdcall SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall DeleteProps(LPSPropTagArray lpPropTagArray, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) _zcp_override;
    virtual HRESULT __stdcall GetNamesFromIDs(LPSPropTagArray *lppPropTags, LPGUID lpPropSetGuid, ULONG ulFlags, ULONG *lpcPropNames, LPMAPINAMEID **lpppPropNames) _zcp_override;
    virtual HRESULT __stdcall GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG ulFlags, LPSPropTagArray *lppPropTags) _zcp_override;

    // iunknown passthru
    virtual ULONG __stdcall AddRef(void) _zcp_override;
    virtual ULONG __stdcall Release(void) _zcp_override;
    virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _zcp_override;
};

#endif
