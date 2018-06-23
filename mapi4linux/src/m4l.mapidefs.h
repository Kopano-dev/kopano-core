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
#include <kopano/memory.hpp>

class M4LMsgServiceAdmin;

class M4LMAPIProp : public M4LUnknown, public virtual IMailUser {
private:
    // variables
	std::list<LPSPropValue> properties;

public:
	virtual ~M4LMAPIProp(void);
	virtual HRESULT GetLastError(HRESULT, ULONG flags, LPMAPIERROR *) _kc_override;
	virtual HRESULT SaveChanges(ULONG flags) _kc_override;
	virtual HRESULT GetProps(const SPropTagArray *proptag, ULONG flags, ULONG *nvals, LPSPropValue *prop) _kc_override;
	virtual HRESULT GetPropList(ULONG flags, LPSPropTagArray *proptag) _kc_override;
	virtual HRESULT OpenProperty(ULONG proptag, LPCIID lpiid, ULONG ifaceopts, ULONG flags, LPUNKNOWN *) _kc_override __attribute__((nonnull(3)));
	virtual HRESULT SetProps(ULONG nvals, const SPropValue *prop, LPSPropProblemArray *) _kc_override;
	virtual HRESULT DeleteProps(const SPropTagArray *proptag, LPSPropProblemArray *) _kc_override;
	virtual HRESULT CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, const SPropTagArray *exclprop, ULONG ui_param, LPMAPIPROGRESS, LPCIID iface, LPVOID dest_obj, ULONG flags, LPSPropProblemArray *) _kc_override;
	virtual HRESULT CopyProps(const SPropTagArray *inclprop, ULONG ui_param, LPMAPIPROGRESS, LPCIID iface, LPVOID dest_obj, ULONG flags, LPSPropProblemArray *) _kc_override;
	virtual HRESULT GetNamesFromIDs(SPropTagArray **tags, const GUID *propset, ULONG flags, ULONG *nvals, MAPINAMEID ***names) override;
	virtual HRESULT GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG flags, LPSPropTagArray *proptag) _kc_override;
	virtual HRESULT QueryInterface(REFIID refiid, void **iface) _kc_override;
};

class M4LProfSect _kc_final : public IProfSect, public M4LMAPIProp {
private:
	BOOL bGlobalProf;
public:
	M4LProfSect(BOOL gp = false) : bGlobalProf(gp) {}
	virtual HRESULT ValidateState(ULONG ulUIParam, ULONG ulFlags);
	virtual HRESULT SettingsDialog(ULONG ulUIParam, ULONG ulFlags);
	virtual HRESULT ChangePassword(const TCHAR *oldpw, const TCHAR *newpw, ULONG flags);
	virtual HRESULT FlushQueues(ULONG ulUIParam, ULONG cbTargetTransport, LPENTRYID lpTargetTransport, ULONG ulFlags);
	virtual HRESULT QueryInterface(REFIID refiid, void **iface) _kc_override;
};

class M4LMAPITable _kc_final : public M4LUnknown, public IMAPITable {
public:
	virtual HRESULT GetLastError(HRESULT hResult, ULONG flags, LPMAPIERROR *lppMAPIError) _kc_override;
	virtual HRESULT Advise(ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection) _kc_override;
	virtual HRESULT Unadvise(ULONG ulConnection) _kc_override;
	virtual HRESULT GetStatus(ULONG *lpulTableStatus, ULONG *lpulTableType) _kc_override;
	virtual HRESULT SetColumns(const SPropTagArray *, ULONG flags) _kc_override;
	virtual HRESULT QueryColumns(ULONG flags, LPSPropTagArray *) _kc_override;
	virtual HRESULT GetRowCount(ULONG flags, ULONG *lpulCount) _kc_override;
	virtual HRESULT SeekRow(BOOKMARK bkOrigin, LONG lRowCount, LONG *lplRowsSought) _kc_override;
	virtual HRESULT SeekRowApprox(ULONG ulNumerator, ULONG ulDenominator) _kc_override;
	virtual HRESULT QueryPosition(ULONG *lpulRow, ULONG *lpulNumerator, ULONG *lpulDenominator) _kc_override;
	virtual HRESULT FindRow(const SRestriction *, BOOKMARK origin, ULONG flags) _kc_override;
	virtual HRESULT Restrict(const SRestriction *, ULONG flags) _kc_override;
	virtual HRESULT CreateBookmark(BOOKMARK *lpbkPosition) _kc_override;
	virtual HRESULT FreeBookmark(BOOKMARK bkPosition) _kc_override;
	virtual HRESULT SortTable(const SSortOrderSet *sort_crit, ULONG flags) _kc_override;
	virtual HRESULT QuerySortOrder(LPSSortOrderSet *lppSortCriteria) _kc_override;
	virtual HRESULT QueryRows(LONG lRowCount, ULONG flags, LPSRowSet *lppRows) _kc_override;
	virtual HRESULT Abort(void) _kc_override;
	virtual HRESULT ExpandRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulRowCount, ULONG flags, LPSRowSet *lppRows, ULONG *lpulMoreRows) _kc_override;
	virtual HRESULT CollapseRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG flags, ULONG *lpulRowCount) _kc_override;
	virtual HRESULT WaitForCompletion(ULONG flags, ULONG ulTimeout, ULONG *lpulTableStatus) _kc_override;
	virtual HRESULT GetCollapseState(ULONG flags, ULONG cbInstanceKey, LPBYTE lpbInstanceKey, ULONG *lpcbCollapseState, LPBYTE *lppbCollapseState) _kc_override;
	virtual HRESULT SetCollapseState(ULONG flags, ULONG cbCollapseState, LPBYTE pbCollapseState, BOOKMARK *lpbkLocation) _kc_override;
	virtual HRESULT QueryInterface(REFIID refiid, void **iface) _kc_override;
};

class M4LProviderAdmin _kc_final : public M4LUnknown , public IProviderAdmin {
private:
	M4LMsgServiceAdmin* msa;
	char *szService;

public:
	M4LProviderAdmin(M4LMsgServiceAdmin *, const char *service);
	virtual ~M4LProviderAdmin(void);
	virtual HRESULT GetLastError(HRESULT hResult, ULONG flags, LPMAPIERROR *lppMAPIError) _kc_override;
	virtual HRESULT GetProviderTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
	virtual HRESULT CreateProvider(const TCHAR *name, ULONG nprops, const SPropValue *lpProps, ULONG ui_param, ULONG flags, MAPIUID *lpUID) _kc_override;
	virtual HRESULT DeleteProvider(const MAPIUID *uid) _kc_override;
	virtual HRESULT OpenProfileSection(const MAPIUID *uid, const IID *intf, ULONG flags, IProfSect **) _kc_override;
	virtual HRESULT QueryInterface(REFIID refiid, void **iface) _kc_override;
};

class M4LMAPIAdviseSink _kc_final : public M4LUnknown, public IMAPIAdviseSink {
private:
    void *lpContext;
    LPNOTIFCALLBACK lpFn;

public:
	M4LMAPIAdviseSink(NOTIFCALLBACK *f, void *ctx) : lpContext(ctx), lpFn(f) {}
	virtual ULONG OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifications) _kc_override;
	virtual HRESULT QueryInterface(REFIID refiid, void **lpvoid) _kc_override;
};


/* for ABContainer */
class M4LMAPIContainer : public M4LMAPIProp, public virtual IMAPIContainer {
public:
	virtual HRESULT GetContentsTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
	virtual HRESULT GetHierarchyTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
//	OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG flags, ULONG *lpulObjType, LPUNKNOWN *lppUnk) _kc_override;
	virtual HRESULT SetSearchCriteria(const SRestriction *, const ENTRYLIST *container, ULONG flags) override;
	virtual HRESULT GetSearchCriteria(ULONG flags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState) _kc_override;
	virtual HRESULT QueryInterface(REFIID refiid, void **lpvoid) _kc_override;
};

struct abEntry {
	MAPIUID muid;
	std::string displayname;
	KC::object_ptr<IABProvider> lpABProvider;
	KC::object_ptr<IABLogon> lpABLogon;
};

class M4LABContainer _kc_final : public IABContainer, public M4LMAPIContainer {
private:
	/*  */
	const std::list<abEntry> &m_lABEntries;

public:
	M4LABContainer(const std::list<abEntry> &lABEntries);
	virtual HRESULT CreateEntry(ULONG eid_size, const ENTRYID *eid, ULONG flags, IMAPIProp **) _kc_override;
	virtual HRESULT CopyEntries(const ENTRYLIST *, ULONG ui_param, IMAPIProgress *, ULONG flags) override;
	virtual HRESULT DeleteEntries(const ENTRYLIST *, ULONG flags) override;
	virtual HRESULT ResolveNames(const SPropTagArray *, ULONG flags, LPADRLIST lpAdrList, LPFlagList lpFlagList) _kc_override;
	virtual HRESULT GetHierarchyTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	virtual HRESULT QueryInterface(REFIID refiid, void **lpvoid) _kc_override;
};

#endif
