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
	virtual ~M4LMAPIProp(void);
	virtual HRESULT __stdcall GetLastError(HRESULT, ULONG flags, LPMAPIERROR *) _kc_override;
	virtual HRESULT __stdcall SaveChanges(ULONG flags) _kc_override;
	virtual HRESULT __stdcall GetProps(LPSPropTagArray proptag, ULONG flags, ULONG *nvals, LPSPropValue *prop) _kc_override;
	virtual HRESULT __stdcall GetPropList(ULONG flags, LPSPropTagArray *proptag) _kc_override;
	virtual HRESULT __stdcall OpenProperty(ULONG proptag, LPCIID lpiid, ULONG ifaceopts, ULONG flags, LPUNKNOWN *) _kc_override;
	virtual HRESULT __stdcall SetProps(ULONG nvals, LPSPropValue prop, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall DeleteProps(LPSPropTagArray proptag, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray exclprop, ULONG ui_param, LPMAPIPROGRESS, LPCIID iface, LPVOID dest_obj, ULONG flags, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall CopyProps(LPSPropTagArray inclprop, ULONG ui_param, LPMAPIPROGRESS, LPCIID iface, LPVOID dest_obj, ULONG flags, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall GetNamesFromIDs(LPSPropTagArray *proptag, LPGUID lpPropSetGuid, ULONG flags, ULONG *lpcPropNames, LPMAPINAMEID **lpppPropNames) _kc_override;
	virtual HRESULT __stdcall GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG flags, LPSPropTagArray *proptag) _kc_override;

	// iunknown passthru
	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **iface) _kc_override;
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
	virtual HRESULT __stdcall GetLastError(HRESULT, ULONG flags, LPMAPIERROR *) _kc_override;
	virtual HRESULT __stdcall SaveChanges(ULONG flags) _kc_override;
	virtual HRESULT __stdcall GetProps(LPSPropTagArray proptag, ULONG flags, ULONG *lpcValues, LPSPropValue *prop) _kc_override;
	virtual HRESULT __stdcall GetPropList(ULONG flags, LPSPropTagArray *) _kc_override;
	virtual HRESULT __stdcall OpenProperty(ULONG proptag, LPCIID lpiid, ULONG ifaceopts, ULONG flags, LPUNKNOWN *) _kc_override;
	virtual HRESULT __stdcall SetProps(ULONG nvals, LPSPropValue prop, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall DeleteProps(LPSPropTagArray proptag, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray exclprop, ULONG ui_param, LPMAPIPROGRESS, LPCIID lpInterface, LPVOID lpDestObj, ULONG flags, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall CopyProps(LPSPropTagArray inclprop, ULONG ui_param, LPMAPIPROGRESS, LPCIID lpInterface, LPVOID lpDestObj, ULONG flags, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall GetNamesFromIDs(LPSPropTagArray *proptag, LPGUID lpPropSetGuid, ULONG flags, ULONG *lpcPropNames, LPMAPINAMEID **lpppPropNames) _kc_override;
	virtual HRESULT __stdcall GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG flags, LPSPropTagArray *) _kc_override;

	// iunknown passthru
	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **iface) _kc_override;
};

class M4LMAPITable _kc_final : public M4LUnknown, public IMAPITable {
public:
	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG flags, LPMAPIERROR *lppMAPIError) _kc_override;
	virtual HRESULT __stdcall Advise(ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection) _kc_override;
	virtual HRESULT __stdcall Unadvise(ULONG ulConnection) _kc_override;
	virtual HRESULT __stdcall GetStatus(ULONG *lpulTableStatus, ULONG *lpulTableType) _kc_override;
	virtual HRESULT __stdcall SetColumns(const SPropTagArray *, ULONG flags) _kc_override;
	virtual HRESULT __stdcall QueryColumns(ULONG flags, LPSPropTagArray *) _kc_override;
	virtual HRESULT __stdcall GetRowCount(ULONG flags, ULONG *lpulCount) _kc_override;
	virtual HRESULT __stdcall SeekRow(BOOKMARK bkOrigin, LONG lRowCount, LONG *lplRowsSought) _kc_override;
	virtual HRESULT __stdcall SeekRowApprox(ULONG ulNumerator, ULONG ulDenominator) _kc_override;
	virtual HRESULT __stdcall QueryPosition(ULONG *lpulRow, ULONG *lpulNumerator, ULONG *lpulDenominator) _kc_override;
	virtual HRESULT __stdcall FindRow(LPSRestriction lpRestriction, BOOKMARK bkOrigin, ULONG flags) _kc_override;
	virtual HRESULT __stdcall Restrict(LPSRestriction lpRestriction, ULONG flags) _kc_override;
	virtual HRESULT __stdcall CreateBookmark(BOOKMARK *lpbkPosition) _kc_override;
	virtual HRESULT __stdcall FreeBookmark(BOOKMARK bkPosition) _kc_override;
	virtual HRESULT __stdcall SortTable(const SSortOrderSet *sort_crit, ULONG flags) _kc_override;
	virtual HRESULT __stdcall QuerySortOrder(LPSSortOrderSet *lppSortCriteria) _kc_override;
	virtual HRESULT __stdcall QueryRows(LONG lRowCount, ULONG flags, LPSRowSet *lppRows) _kc_override;
	virtual HRESULT __stdcall Abort(void) _kc_override;
	virtual HRESULT __stdcall ExpandRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulRowCount, ULONG flags, LPSRowSet *lppRows, ULONG *lpulMoreRows) _kc_override;
	virtual HRESULT __stdcall CollapseRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG flags, ULONG *lpulRowCount) _kc_override;
	virtual HRESULT __stdcall WaitForCompletion(ULONG flags, ULONG ulTimeout, ULONG *lpulTableStatus) _kc_override;
	virtual HRESULT __stdcall GetCollapseState(ULONG flags, ULONG cbInstanceKey, LPBYTE lpbInstanceKey, ULONG *lpcbCollapseState, LPBYTE *lppbCollapseState) _kc_override;
	virtual HRESULT __stdcall SetCollapseState(ULONG flags, ULONG cbCollapseState, LPBYTE pbCollapseState, BOOKMARK *lpbkLocation) _kc_override;

	// iunknown passthru
	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **iface) _kc_override;
};

class M4LProviderAdmin _kc_final : public M4LUnknown , public IProviderAdmin {
private:
	M4LMsgServiceAdmin* msa;
	char *szService;

public:
	M4LProviderAdmin(M4LMsgServiceAdmin *, const char *service);
	virtual ~M4LProviderAdmin(void);
	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG flags, LPMAPIERROR *lppMAPIError) _kc_override;
	virtual HRESULT __stdcall GetProviderTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
	virtual HRESULT __stdcall CreateProvider(LPTSTR lpszProvider, ULONG cValues, LPSPropValue lpProps, ULONG ui_param, ULONG flags, MAPIUID *lpUID) _kc_override;
	virtual HRESULT __stdcall DeleteProvider(LPMAPIUID lpUID) _kc_override;
	virtual HRESULT __stdcall OpenProfileSection(LPMAPIUID lpUID, LPCIID lpInterface, ULONG flags, LPPROFSECT *lppProfSect) _kc_override;

	// iunknown passthru
	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **iface) _kc_override;
};

class M4LMAPIAdviseSink _kc_final : public M4LUnknown, public IMAPIAdviseSink {
private:
    void *lpContext;
    LPNOTIFCALLBACK lpFn;

public:
	M4LMAPIAdviseSink(LPNOTIFCALLBACK lpFn, void *lpContext);
	virtual ULONG __stdcall OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifications) _kc_override;

	// iunknown passthru
	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _kc_override;
};


/* for ABContainer */
class M4LMAPIContainer : public M4LMAPIProp, public virtual IMAPIContainer {
public:
	virtual HRESULT __stdcall GetContentsTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
	virtual HRESULT __stdcall GetHierarchyTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
	virtual HRESULT __stdcall OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG flags, ULONG *lpulObjType, LPUNKNOWN *lppUnk) _kc_override;
	virtual HRESULT __stdcall SetSearchCriteria(LPSRestriction lpRestriction, LPENTRYLIST lpContainerList, ULONG ulSearchFlags) _kc_override;
	virtual HRESULT __stdcall GetSearchCriteria(ULONG flags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState) _kc_override;

	// imapiprop passthru
	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG flags, LPMAPIERROR *lppMAPIError) _kc_override;
	virtual HRESULT __stdcall SaveChanges(ULONG flags) _kc_override;
	virtual HRESULT __stdcall GetProps(LPSPropTagArray, ULONG flags, ULONG *lpcValues, LPSPropValue *lppPropArray) _kc_override;
	virtual HRESULT __stdcall GetPropList(ULONG flags, LPSPropTagArray *) _kc_override;
	virtual HRESULT __stdcall OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG flags, LPUNKNOWN *lppUnk) _kc_override;
	virtual HRESULT __stdcall SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall DeleteProps(LPSPropTagArray, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray exclprop, ULONG ui_param, LPMAPIPROGRESS, LPCIID lpInterface, LPVOID lpDestObj, ULONG flags, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall CopyProps(LPSPropTagArray inclprop, ULONG ui_param, LPMAPIPROGRESS, LPCIID lpInterface, LPVOID lpDestObj, ULONG flags, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall GetNamesFromIDs(LPSPropTagArray *, LPGUID lpPropSetGuid, ULONG flags, ULONG *lpcPropNames, LPMAPINAMEID **lpppPropNames) _kc_override;
	virtual HRESULT __stdcall GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG flags, LPSPropTagArray *) _kc_override;

	// iunknown passthru
	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _kc_override;
};

struct abEntry {
	MAPIUID muid;
	string displayname;
	LPABPROVIDER lpABProvider;
	LPABLOGON lpABLogon;
};

class M4LABContainer _kc_final : public IABContainer, public M4LMAPIContainer {
private:
	/*  */
	const std::list<abEntry> &m_lABEntries;

public:
	M4LABContainer(const std::list<abEntry> &lABEntries);

	virtual HRESULT __stdcall CreateEntry(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulCreateFlags, LPMAPIPROP *lppMAPIPropEntry) _kc_override;
	virtual HRESULT __stdcall CopyEntries(LPENTRYLIST lpEntries, ULONG ui_param, LPMAPIPROGRESS, ULONG flags) _kc_override;
	virtual HRESULT __stdcall DeleteEntries(LPENTRYLIST lpEntries, ULONG flags) _kc_override;
	virtual HRESULT __stdcall ResolveNames(LPSPropTagArray, ULONG flags, LPADRLIST lpAdrList, LPFlagList lpFlagList) _kc_override;

	// imapicontainer passthu
	virtual HRESULT __stdcall GetContentsTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
	virtual HRESULT __stdcall GetHierarchyTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
	virtual HRESULT __stdcall OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG flags, ULONG *lpulObjType, LPUNKNOWN *lppUnk) _kc_override;
	virtual HRESULT __stdcall SetSearchCriteria(LPSRestriction lpRestriction, LPENTRYLIST lpContainerList, ULONG ulSearchFlags) _kc_override;
	virtual HRESULT __stdcall GetSearchCriteria(ULONG flags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState) _kc_override;

	// imapiprop passthru
	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG flags, LPMAPIERROR *lppMAPIError) _kc_override;
	virtual HRESULT __stdcall SaveChanges(ULONG flags) _kc_override;
	virtual HRESULT __stdcall GetProps(LPSPropTagArray, ULONG flags, ULONG *lpcValues, LPSPropValue *lppPropArray) _kc_override;
	virtual HRESULT __stdcall GetPropList(ULONG flags, LPSPropTagArray *) _kc_override;
	virtual HRESULT __stdcall OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG flags, LPUNKNOWN *lppUnk) _kc_override;
	virtual HRESULT __stdcall SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall DeleteProps(LPSPropTagArray, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray exclprop, ULONG ui_param, LPMAPIPROGRESS, LPCIID lpInterface, LPVOID lpDestObj, ULONG flags, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall CopyProps(LPSPropTagArray inclprop, ULONG ui_param, LPMAPIPROGRESS, LPCIID lpInterface, LPVOID lpDestObj, ULONG flags, LPSPropProblemArray *) _kc_override;
	virtual HRESULT __stdcall GetNamesFromIDs(LPSPropTagArray *, LPGUID lpPropSetGuid, ULONG flags, ULONG *lpcPropNames, LPMAPINAMEID **lpppPropNames) _kc_override;
	virtual HRESULT __stdcall GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG flags, LPSPropTagArray *) _kc_override;

	// iunknown passthru
	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _kc_override;
};

#endif
