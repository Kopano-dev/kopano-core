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
#include <kopano/platform.h>
#include <utility>
#include <kopano/ECRestriction.h>
#include <kopano/Util.h>
#include <kopano/mapi_ptr.h>
#include <mapicode.h>
#include <mapix.h>

namespace KC {

/**
 * Allocate and populate a MAPI SRestriction structure based on the current
 * ECRestriction object.
 * @param[out]	lppRestriction	The resulting SRestriction structure.
 * @retval	MAPI_E_INVALID_PARAMTER	lppRestriction is NULL.
 */
HRESULT ECRestriction::CreateMAPIRestriction(LPSRestriction *lppRestriction, ULONG ulFlags) const {
	SRestrictionPtr ptrRestriction;

	if (lppRestriction == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = MAPIAllocateBuffer(sizeof(SRestriction), &~ptrRestriction);
	if (hr != hrSuccess)
		return hr;
	hr = GetMAPIRestriction(ptrRestriction, ptrRestriction, ulFlags);
	if (hr != hrSuccess)
		return hr;
	*lppRestriction = ptrRestriction.release();
	return hrSuccess;
}

HRESULT ECRestriction::RestrictTable(IMAPITable *lpTable,
    unsigned int flags) const
{
	SRestrictionPtr ptrRestriction;

	if (lpTable == NULL)
		return MAPI_E_INVALID_PARAMETER;
	HRESULT hr = CreateMAPIRestriction(&~ptrRestriction, ECRestriction::Cheap);
	if (hr != hrSuccess)
		return hr;
	return lpTable->Restrict(ptrRestriction, flags);
}

HRESULT ECRestriction::FindRowIn(LPMAPITABLE lpTable, BOOKMARK BkOrigin, ULONG ulFlags) const
{
	SRestrictionPtr ptrRestriction;

	if (lpTable == NULL)
		return MAPI_E_INVALID_PARAMETER;
	HRESULT hr = CreateMAPIRestriction(&~ptrRestriction, ECRestriction::Cheap);
	if (hr != hrSuccess)
		return hr;
	return lpTable->FindRow(ptrRestriction, BkOrigin, ulFlags);
}

/**
 * Copy a property into a newly allocated SPropValue.
 * @param[in]	lpPropSrc	The source property.
 * @param[in]	lpBase		An optional base pointer for MAPIAllocateMore.
 * @param[out]	lppPropDst	Pointer to an SPropValue pointer that will be set to 
 *							the address of the newly allocated SPropValue.
 * @retval	MAPI_E_INVALID_PARAMETER	lpPropSrc or lppPropDst is NULL.
 */
HRESULT ECRestriction::CopyProp(SPropValue *lpPropSrc, void *lpBase,
    ULONG ulFlags, SPropValue **lppPropDst)
{
	memory_ptr<SPropValue> lpPropDst;

	if (lpPropSrc == nullptr || lppPropDst == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = MAPIAllocateMore(sizeof(*lpPropDst), lpBase, &~lpPropDst);
	if (hr != hrSuccess)
		return hr;
	if (ulFlags & Shallow)
		hr = Util::HrCopyPropertyByRef(lpPropDst, lpPropSrc);
	else
		hr = Util::HrCopyProperty(lpPropDst, lpPropSrc, lpBase ? lpBase : lpPropDst);
	if (hr != hrSuccess)
		return hr;

	*lppPropDst = lpPropDst.release();

	return hr;
}

inline void ECRestriction::DummyFree(LPVOID) {}

/**
 * ECAndRestriction
 */
ECAndRestriction::ECAndRestriction(const ECRestrictionList &list): m_lstRestrictions(list.m_list) {}

void ECAndRestriction::operator+=(const ECRestrictionList &list)
{
	m_lstRestrictions.insert(m_lstRestrictions.end(), list.m_list.begin(), list.m_list.end());
}

void ECAndRestriction::operator+=(ECRestrictionList &&o)
{
	ResList &dst = m_lstRestrictions, &src = o.m_list;
	if (dst.empty()) {
		dst = std::move(src);
		return;
	}
	std::move(std::begin(src), std::end(src), std::back_inserter(dst));
	src.clear();
}

HRESULT ECAndRestriction::GetMAPIRestriction(LPVOID lpBase, LPSRestriction lpRestriction, ULONG ulFlags) const {
	SRestriction restriction = {0};
	ULONG i = 0;

	if (lpBase == NULL || lpRestriction == NULL)
		return MAPI_E_INVALID_PARAMETER;
	restriction.rt = RES_AND;
	restriction.res.resAnd.cRes = m_lstRestrictions.size();

	HRESULT hr = MAPIAllocateMore(restriction.res.resAnd.cRes *
	             sizeof(*restriction.res.resAnd.lpRes), lpBase,
	             reinterpret_cast<LPVOID *>(&restriction.res.resAnd.lpRes));
	if (hr != hrSuccess)
		return hr;

	for (const auto &r : m_lstRestrictions) {
		hr = r->GetMAPIRestriction(lpBase, restriction.res.resAnd.lpRes + i, ulFlags);
		if (hr != hrSuccess)
			return hr;
		++i;
	}

	*lpRestriction = std::move(restriction);
	return hrSuccess;
}

ECRestriction *ECAndRestriction::Clone(void) const &
{
	auto lpNew = new ECAndRestriction;
	lpNew->m_lstRestrictions.assign(m_lstRestrictions.begin(), m_lstRestrictions.end());
	return lpNew;
}

/**
 * ECOrRestriction
 */
ECOrRestriction::ECOrRestriction(const ECRestrictionList &list): m_lstRestrictions(list.m_list) {}

void ECOrRestriction::operator+=(const ECRestrictionList &list)
{
	m_lstRestrictions.insert(m_lstRestrictions.end(), list.m_list.begin(), list.m_list.end());
}

void ECOrRestriction::operator+=(ECRestrictionList &&o)
{
	ResList &dst = m_lstRestrictions, &src = o.m_list;
	if (dst.empty()) {
		dst = std::move(src);
		return;
	}
	std::move(std::begin(src), std::end(src), std::back_inserter(dst));
	src.clear();
}

HRESULT ECOrRestriction::GetMAPIRestriction(LPVOID lpBase, LPSRestriction lpRestriction, ULONG ulFlags) const {
	SRestriction restriction = {0};
	ULONG i = 0;

	if (lpBase == NULL || lpRestriction == NULL)
		return MAPI_E_INVALID_PARAMETER;
	restriction.rt = RES_OR;
	restriction.res.resOr.cRes = m_lstRestrictions.size();

	HRESULT hr = MAPIAllocateMore(restriction.res.resOr.cRes *
	             sizeof(*restriction.res.resOr.lpRes), lpBase,
	             reinterpret_cast<LPVOID *>(&restriction.res.resOr.lpRes));
	if (hr != hrSuccess)
		return hr;

	for (const auto &r : m_lstRestrictions) {
		hr = r->GetMAPIRestriction(lpBase, restriction.res.resOr.lpRes + i, ulFlags);
		if (hr != hrSuccess)
			return hr;
		++i;
	}

	*lpRestriction = std::move(restriction);
	return hrSuccess;
}

ECRestriction *ECOrRestriction::Clone(void) const &
{
	auto lpNew = new ECOrRestriction;
	lpNew->m_lstRestrictions.assign(m_lstRestrictions.begin(), m_lstRestrictions.end());
	return lpNew;
}

/**
 * ECNotRestriction
 */
ECNotRestriction::ECNotRestriction(ResPtr ptrRestriction): m_ptrRestriction(ptrRestriction) {}

HRESULT ECNotRestriction::GetMAPIRestriction(LPVOID lpBase, LPSRestriction lpRestriction, ULONG ulFlags) const {
	SRestriction restriction = {0};

	if (lpBase == NULL || lpRestriction == NULL)
		return MAPI_E_INVALID_PARAMETER;
	restriction.rt = RES_NOT;
	HRESULT hr = MAPIAllocateMore(sizeof(*restriction.res.resNot.lpRes),
	             lpBase, reinterpret_cast<LPVOID *>(&restriction.res.resNot.lpRes));
	if (hr != hrSuccess)
		return hr;
	hr = m_ptrRestriction->GetMAPIRestriction(lpBase, restriction.res.resNot.lpRes, ulFlags);
	if (hr != hrSuccess)
		return hr;
	*lpRestriction = std::move(restriction);
	return hrSuccess;
}

ECRestriction *ECNotRestriction::Clone(void) const &
{
	return new ECNotRestriction(m_ptrRestriction);
}

/**
 * ECContentRestriction
 */
ECContentRestriction::ECContentRestriction(ULONG ulFuzzyLevel, ULONG ulPropTag,
    const SPropValue *lpProp, ULONG ulFlags) :
	m_ulFuzzyLevel(ulFuzzyLevel), m_ulPropTag(ulPropTag)
{
	auto np = const_cast<SPropValue *>(lpProp);
	if (ulFlags & ECRestriction::Cheap)
		m_ptrProp.reset(np, &ECRestriction::DummyFree);
	else if (CopyProp(np, nullptr, ulFlags, &np) == hrSuccess)
		m_ptrProp.reset(np, &MAPIFreeBuffer);
}

ECContentRestriction::ECContentRestriction(ULONG ulFuzzyLevel, ULONG ulPropTag, PropPtr ptrProp)
: m_ulFuzzyLevel(ulFuzzyLevel)
, m_ulPropTag(ulPropTag)
, m_ptrProp(ptrProp)
{}

HRESULT ECContentRestriction::GetMAPIRestriction(LPVOID lpBase, LPSRestriction lpRestriction, ULONG ulFlags) const {
	SRestriction restriction = {0};

	if (lpBase == NULL || lpRestriction == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (!m_ptrProp)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	restriction.rt = RES_CONTENT;
	restriction.res.resContent.ulFuzzyLevel = m_ulFuzzyLevel;
	restriction.res.resContent.ulPropTag = m_ulPropTag;

	if (ulFlags & ECRestriction::Cheap)
		restriction.res.resContent.lpProp = m_ptrProp.get();
	else {
		HRESULT hr = CopyProp(m_ptrProp.get(), lpBase, ulFlags, &restriction.res.resContent.lpProp);
		if (hr != hrSuccess)
			return hr;
	}

	*lpRestriction = std::move(restriction);
	return hrSuccess;
}

ECRestriction *ECContentRestriction::Clone(void) const &
{
	return new ECContentRestriction(m_ulFuzzyLevel, m_ulPropTag, m_ptrProp);
}

/**
 * ECBitMaskRestriction
 */
HRESULT ECBitMaskRestriction::GetMAPIRestriction(LPVOID lpBase, LPSRestriction lpRestriction, ULONG /*ulFlags*/) const {
	if (lpBase == NULL || lpRestriction == NULL)
		return MAPI_E_INVALID_PARAMETER;
	lpRestriction->rt = RES_BITMASK;
	lpRestriction->res.resBitMask.relBMR = m_relBMR;
	lpRestriction->res.resBitMask.ulMask = m_ulMask;
	lpRestriction->res.resBitMask.ulPropTag = m_ulPropTag;
	return hrSuccess;
}

ECRestriction *ECBitMaskRestriction::Clone(void) const &
{
	return new ECBitMaskRestriction(m_relBMR, m_ulPropTag, m_ulMask);
}

/**
 * ECPropertyRestriction
 */
ECPropertyRestriction::ECPropertyRestriction(ULONG relop, ULONG ulPropTag,
    const SPropValue *lpProp, ULONG ulFlags) :
	m_relop(relop), m_ulPropTag(ulPropTag)
{
	auto np = const_cast<SPropValue *>(lpProp);
	if (ulFlags & ECRestriction::Cheap)
		m_ptrProp.reset(np, &ECRestriction::DummyFree);
	else if (CopyProp(np, nullptr, ulFlags, &np) == hrSuccess)
		m_ptrProp.reset(np, &MAPIFreeBuffer);
}

ECPropertyRestriction::ECPropertyRestriction(ULONG relop, ULONG ulPropTag, PropPtr ptrProp)
: m_relop(relop)
, m_ulPropTag(ulPropTag)
, m_ptrProp(ptrProp)
{}

HRESULT ECPropertyRestriction::GetMAPIRestriction(LPVOID lpBase, LPSRestriction lpRestriction, ULONG ulFlags) const {
	SRestriction restriction = {0};

	if (lpBase == NULL || lpRestriction == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (!m_ptrProp)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	restriction.rt = RES_PROPERTY;
	restriction.res.resProperty.relop = m_relop;
	restriction.res.resProperty.ulPropTag = m_ulPropTag;

	if (ulFlags & ECRestriction::Cheap)
		restriction.res.resProperty.lpProp = m_ptrProp.get();
	else {
		HRESULT hr = CopyProp(m_ptrProp.get(), lpBase, ulFlags, &restriction.res.resContent.lpProp);
		if (hr != hrSuccess)
			return hr;
	}

	*lpRestriction = std::move(restriction);
	return hrSuccess;
}

ECRestriction *ECPropertyRestriction::Clone(void) const &
{
	return new ECPropertyRestriction(m_relop, m_ulPropTag, m_ptrProp);
}

/**
 * ECComparePropsRestriction
 */
HRESULT ECComparePropsRestriction::GetMAPIRestriction(LPVOID lpBase, LPSRestriction lpRestriction, ULONG /*ulFlags*/) const {
	if (lpBase == NULL || lpRestriction == NULL)
		return MAPI_E_INVALID_PARAMETER;
	lpRestriction->rt = RES_COMPAREPROPS;
	lpRestriction->res.resCompareProps.relop = m_relop;
	lpRestriction->res.resCompareProps.ulPropTag1 = m_ulPropTag1;
	lpRestriction->res.resCompareProps.ulPropTag2 = m_ulPropTag2;
	return hrSuccess;
}

ECRestriction *ECComparePropsRestriction::Clone(void) const &
{
	return new ECComparePropsRestriction(m_relop, m_ulPropTag1, m_ulPropTag2);
}

/**
 * ECExistRestriction
 */
HRESULT ECExistRestriction::GetMAPIRestriction(LPVOID lpBase, LPSRestriction lpRestriction, ULONG /*ulFlags*/) const {
	if (lpBase == NULL || lpRestriction == NULL)
		return MAPI_E_INVALID_PARAMETER;
	lpRestriction->rt = RES_EXIST;
	lpRestriction->res.resExist.ulPropTag = m_ulPropTag;
	return hrSuccess;
}

ECRestriction *ECExistRestriction::Clone(void) const &
{
	return new ECExistRestriction(m_ulPropTag);
}

/**
 * ECRawRestriction
 */
ECRawRestriction::ECRawRestriction(const SRestriction *lpRestriction,
    ULONG ulFlags)
{
	if (ulFlags & ECRestriction::Cheap) {
		m_ptrRestriction.reset(const_cast<SRestriction *>(lpRestriction), &ECRestriction::DummyFree);
		return;
	}
	SRestrictionPtr ptrResTmp;
	auto hr = MAPIAllocateBuffer(sizeof(SRestriction), &~ptrResTmp);
	if (hr != hrSuccess)
		return;
	if (ulFlags & ECRestriction::Shallow)
		*ptrResTmp = *lpRestriction;
	else {
		hr = Util::HrCopySRestriction(ptrResTmp, lpRestriction, ptrResTmp);
		if (hr != hrSuccess)
			return;
	}
	m_ptrRestriction.reset(ptrResTmp.release(), &MAPIFreeBuffer);
}

ECRawRestriction::ECRawRestriction(RawResPtr ptrRestriction)
: m_ptrRestriction(ptrRestriction) 
{ }

HRESULT ECRawRestriction::GetMAPIRestriction(LPVOID lpBase, LPSRestriction lpRestriction, ULONG ulFlags) const {
	if (lpBase == NULL || lpRestriction == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (!m_ptrRestriction)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	if (!(ulFlags & (ECRestriction::Cheap | ECRestriction::Shallow)))
		return Util::HrCopySRestriction(lpRestriction, m_ptrRestriction.get(), lpBase);
	*lpRestriction = *m_ptrRestriction;
	return hrSuccess;
}

ECRestriction *ECRawRestriction::Clone(void) const &
{
	return new ECRawRestriction(m_ptrRestriction);
}

} /* namespace KC */
