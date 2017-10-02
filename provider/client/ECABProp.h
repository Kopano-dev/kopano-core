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

#ifndef ECABPROP_H
#define ECABPROP_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>

#include "ECGenericProp.h"
#include "ECABLogon.h"
#include "WSTransport.h"

class ECABProp : public ECGenericProp {
protected:
	ECABProp(void* lpProvider, ULONG ulObjType, BOOL fModify, const char *szClassName = NULL);
	virtual ~ECABProp(void) = default;
public:
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	static HRESULT DefaultABGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	static HRESULT TableRowGetProp(void* lpProvider, struct propVal *lpsPropValSrc, LPSPropValue lpsPropValDst, void **lpBase, ULONG ulType);

	ECABLogon* GetABStore();
};

#endif
