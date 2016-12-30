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

#ifndef ECDISTLIST
#define ECDISTLIST

#include <kopano/zcdefs.h>
#include "ECABContainer.h"

class ECDistList _kc_final : public ECABContainer {
protected:
	ECDistList(void* lpProvider, BOOL fModify);
public:
	
	static HRESULT Create(void* lpProvider, BOOL fModify, ECDistList** lppDistList);
	static HRESULT TableRowGetProp(void* lpProvider, struct propVal *lpsPropValSrc, LPSPropValue lpsPropValDst, void **lpBase, ULONG ulType);

	// Override IMAPIProp
	virtual HRESULT CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
	virtual HRESULT CopyProps(const SPropTagArray *lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);

	// override IUnknown
	virtual HRESULT	QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk);

	class xDistList _kc_final : public IDistList {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IABContainer.hpp>
		#include <kopano/xclsfrag/IMAPIContainer.hpp>
		#include <kopano/xclsfrag/IMAPIProp.hpp>
	} m_xDistList;
};

#endif // #ifndef ECDISTLIST
