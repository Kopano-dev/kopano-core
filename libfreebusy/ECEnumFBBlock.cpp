/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <kopano/platform.h>
#include "ECEnumFBBlock.h"
#include "freebusyutil.h"
#include <kopano/stringutil.h>
#include <kopano/timeutil.hpp>

namespace KC {

/**
 * @param[in] lpFBBlock Pointer to a list of free/busy blocks
 */
ECEnumFBBlock::ECEnumFBBlock(ECFBBlockList* lpFBBlock)
{
	FBBlock_1 sBlock;
	lpFBBlock->Reset();
	while(lpFBBlock->Next(&sBlock) == hrSuccess)
		m_FBBlock.Add(sBlock);
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
	REGISTER_INTERFACE2(IEnumFBBlock, this);
	REGISTER_INTERFACE2(IUnknown, this);
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

/*! @copydoc IEnumFBBlock::Restrict */
HRESULT ECEnumFBBlock::Restrict(const FILETIME &ftmStart,
    const FILETIME &ftmEnd)
{
	return m_FBBlock.Restrict(FileTimeToRTime(ftmStart), FileTimeToRTime(ftmEnd));
}

} /* namespace */
