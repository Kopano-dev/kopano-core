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

#ifndef ZCMAPIPROP_H
#define ZCMAPIPROP_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <mapidefs.h>
#include <kopano/charset/convert.h>

#include <map>

class ZCMAPIProp _no_final : public ECUnknown {
protected:
	ZCMAPIProp(ULONG ulObjType, const char *szClassName = NULL);
	virtual ~ZCMAPIProp();

	HRESULT ConvertMailUser(LPSPropTagArray lpNames, ULONG cValues, LPSPropValue lpProps, ULONG ulIndex);
	HRESULT ConvertDistList(LPSPropTagArray lpNames, ULONG cValues, LPSPropValue lpProps);
	HRESULT ConvertProps(IMAPIProp *lpContact, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulIndex);

	/* getprops helper */
	HRESULT CopyOneProp(convert_context &converter, ULONG ulFlags, const std::map<short, SPropValue>::const_iterator &i, LPSPropValue lpProp, LPSPropValue lpBase);

public:
	static HRESULT Create(IMAPIProp *lpContact, ULONG cbEntryID, LPENTRYID lpEntryID, ZCMAPIProp **lppZCMAPIProp);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;

	// From IMAPIProp
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR * lppMAPIError);
	virtual HRESULT SaveChanges(ULONG ulFlags);
	virtual HRESULT GetProps(const SPropTagArray *lpPropTagArray, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppPropArray);
	virtual HRESULT GetPropList(ULONG ulFlags, LPSPropTagArray * lppPropTagArray);
	virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN * lppUnk);
	virtual HRESULT SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray * lppProblems);
	virtual HRESULT DeleteProps(const SPropTagArray *lpPropTagArray, LPSPropProblemArray *lppProblems);
	virtual HRESULT CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray * lppProblems);
	virtual HRESULT CopyProps(const SPropTagArray *lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
	virtual HRESULT GetNamesFromIDs(LPSPropTagArray * lppPropTags, LPGUID lpPropSetGuid, ULONG ulFlags, ULONG * lpcPropNames, LPMAPINAMEID ** lpppPropNames);
	virtual HRESULT GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID * lppPropNames, ULONG ulFlags, LPSPropTagArray * lppPropTags);

private:
	class xMAPIProp _kc_final : public IMAPIProp {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IMAPIProp.hpp>
	} m_xMAPIProp;

private:
	LPSPropValue m_base;
	WCHAR empty[1];
	std::map<short, SPropValue> m_mapProperties;
	ULONG m_ulObject;
};

#endif
