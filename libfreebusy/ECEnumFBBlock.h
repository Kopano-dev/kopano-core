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
#include <kopano/Trace.h>
#include <kopano/ECDebug.h>
#include <kopano/ECGuid.h>
#include "freebusyguid.h"

#include "ECFBBlockList.h"

/**
 * Implementatie of the IEnumFBBlock interface
 */
class ECEnumFBBlock _kc_final : public ECUnknown {
private:
	ECEnumFBBlock(ECFBBlockList* lpFBBlock);
public:
	static HRESULT Create(ECFBBlockList* lpFBBlock, ECEnumFBBlock **lppECEnumFBBlock);
	
	virtual HRESULT QueryInterface(REFIID refiid, void** lppInterface);
	virtual HRESULT Next(LONG celt, FBBlock_1 *pblk, LONG *pcfetch);
	virtual HRESULT Skip(LONG celt);
	virtual HRESULT Reset();
	virtual HRESULT Clone(IEnumFBBlock **) { return E_NOTIMPL; }
	virtual HRESULT Restrict(FILETIME ftmStart, FILETIME ftmEnd);

public:
	/* IEnumFBBlock wrapper class */
	class xEnumFBBlock _kc_final : public IEnumFBBlock {
		#include <kopano/xclsfrag/IUnknown.hpp>

			// <kopano/xclsfrag/IEnumFBBlock.hpp>
			virtual HRESULT __stdcall Next(LONG celt, FBBlock_1 *pblk, LONG *pcfetch) _kc_override;
			virtual HRESULT __stdcall Skip(LONG celt) _kc_override;
			virtual HRESULT __stdcall Reset(void) _kc_override;
			virtual HRESULT __stdcall Clone(IEnumFBBlock **ppclone) _kc_override;
			virtual HRESULT __stdcall Restrict(FILETIME start, FILETIME end) _kc_override;
	} m_xEnumFBBlock;

	ECFBBlockList	m_FBBlock; /**< Freebusy time blocks */
};

#endif // ECENUMFBBLOCK_H
/** @} */
