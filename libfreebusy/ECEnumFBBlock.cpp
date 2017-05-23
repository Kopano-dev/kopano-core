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
#include <new>
#include <kopano/platform.h>
#include <kopano/ECInterfaceDefs.h>
#include "ECEnumFBBlock.h"
#include "freebusyutil.h"
#include <kopano/stringutil.h>

namespace KC {

/**
 * @param[in] lpFBBlock Pointer to a list of free/busy blocks
 */
ECEnumFBBlock::ECEnumFBBlock(ECFBBlockList* lpFBBlock)
{
	FBBlock_1 sBlock;

	lpFBBlock->Reset();

	while(lpFBBlock->Next(&sBlock) == hrSuccess)
		m_FBBlock.Add(&sBlock);
}

/**
 * Create ECEnumFBBlock object
 * 
 * @param[in]	lpFBBlock		Pointer to a list of free/busy blocks
 * @param[out]	lppEnumFBBlock	Address of the pointer that receives the object ECEnumFBBlock pointer
 */
HRESULT ECEnumFBBlock::Create(ECFBBlockList* lpFBBlock, ECEnumFBBlock **lppEnumFBBlock)
{
	return alloc_wrap<ECEnumFBBlock>(lpFBBlock).put(lppEnumFBBlock);
}

/**
 * This method returns a pointer to a specified interface on an object to which a client 
 * currently holds an interface pointer.
 *
 * @param[in]	iid			Identifier of the interface being requested. 
 * @param[out]	ppvObject	Address of the pointer that receives the interface pointer requested in riid.
 *
 * @retval hrSuccess						Indicates that the interface is supported.
 * @retval MAPI_E_INTERFACE_NOT_SUPPORTED	Indicates that the interface is not supported.
 */
HRESULT ECEnumFBBlock::QueryInterface(REFIID refiid , void** lppInterface)
{
	REGISTER_INTERFACE2(ECEnumFBBlock, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IEnumFBBlock, &this->m_xEnumFBBlock);
	REGISTER_INTERFACE2(IUnknown, &this->m_xEnumFBBlock);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

/*! @copydoc IEnumFBBlock::Next */
HRESULT ECEnumFBBlock::Next(LONG celt, FBBlock_1 *pblk, LONG *pcfetch)
{

	LONG cEltFound = 0;

	for (LONG i = 0; i < celt; ++i) {
		if(m_FBBlock.Next(&pblk[i]) != hrSuccess)
			break;
		++cEltFound;
	}

	if(pcfetch)
		*pcfetch = cEltFound;
	return cEltFound == 0;
}

/*! @copydoc IEnumFBBlock::Skip */
HRESULT ECEnumFBBlock::Skip(LONG celt)
{
	return m_FBBlock.Skip(celt);
}

/*! @copydoc IEnumFBBlock::Reset */
HRESULT ECEnumFBBlock::Reset()
{
	return m_FBBlock.Reset();
}

/*! @copydoc IEnumFBBlock::Restrict */
HRESULT ECEnumFBBlock::Restrict(FILETIME ftmStart, FILETIME ftmEnd)
{
	LONG rtmStart = 0;
	LONG rtmEnd = 0;

	FileTimeToRTime(&ftmStart, &rtmStart);
	FileTimeToRTime(&ftmEnd, &rtmEnd);

	return m_FBBlock.Restrict(rtmStart, rtmEnd);
}

DEF_HRMETHOD1(TRACE_MAPI, ECEnumFBBlock, EnumFBBlock, QueryInterface, (REFIID, refiid), (void**, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECEnumFBBlock, EnumFBBlock, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECEnumFBBlock, EnumFBBlock, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECEnumFBBlock, EnumFBBlock, Next, (LONG, celt), (FBBlock_1 *, pblk), (LONG *, pcfetch))
DEF_HRMETHOD1(TRACE_MAPI, ECEnumFBBlock, EnumFBBlock, Skip, (LONG, celt))
DEF_HRMETHOD1(TRACE_MAPI, ECEnumFBBlock, EnumFBBlock, Reset, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECEnumFBBlock, EnumFBBlock, Clone, (IEnumFBBlock **, ppclone))
DEF_HRMETHOD1(TRACE_MAPI, ECEnumFBBlock, EnumFBBlock, Restrict, (FILETIME, ftmStart), (FILETIME, ftmEnd))

} /* namespace */
