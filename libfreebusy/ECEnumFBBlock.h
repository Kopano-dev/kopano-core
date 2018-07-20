/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/**
 * @file
 * Free/busy data blocks
 *
 * @addtogroup libfreebusy
 * @{
 */

#ifndef ECENUMFBBLOCK_H
#define ECENUMFBBLOCK_H

#include <kopano/zcdefs.h>
#include "freebusy.h"
#include <kopano/ECUnknown.h>
#include <kopano/ECGuid.h>
#include <kopano/Util.h>
#include "freebusyguid.h"
#include "ECFBBlockList.h"

namespace KC {

/**
 * Implementatie of the IEnumFBBlock interface
 */
class ECEnumFBBlock _kc_final : public ECUnknown, public IEnumFBBlock {
private:
	ECEnumFBBlock(ECFBBlockList* lpFBBlock);
public:
	static HRESULT Create(ECFBBlockList* lpFBBlock, ECEnumFBBlock **lppECEnumFBBlock);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT Next(LONG celt, FBBlock_1 *pblk, LONG *pcfetch);
	virtual HRESULT Skip(LONG celt) { return m_FBBlock.Skip(celt); }
	virtual HRESULT Reset() { return m_FBBlock.Reset(); }
	virtual HRESULT Clone(IEnumFBBlock **) { return E_NOTIMPL; }
	virtual HRESULT Restrict(const FILETIME &start, const FILETIME &end) override;

	ECFBBlockList	m_FBBlock; /**< Freebusy time blocks */
	ALLOC_WRAP_FRIEND;
};

} /* namespace */

#endif // ECENUMFBBLOCK_H
/** @} */
