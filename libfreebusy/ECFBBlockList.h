/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/**
 * @file
 * Free/Busy block list
 *
 * @addtogroup libfreebusy
 * @{
 */

#ifndef ECFBBLOCKLIST_H
#define ECFBBLOCKLIST_H

#include "freebusy.h"
#include <map>
#include <kopano/zcdefs.h>

namespace KC {

typedef std::map<LONG, FBBlock_1>mapFB;

class ECFBBlockList KC_FINAL {
public:
	ECFBBlockList(void);
	ECFBBlockList(const ECFBBlockList &);
	void operator=(const ECFBBlockList &) = delete; /* not implemented */
	HRESULT Add(const FBBlock_1 &);
	HRESULT Next(FBBlock_1* pblk);
	HRESULT Reset();
	HRESULT Skip(LONG items);
	HRESULT Restrict(LONG tmStart, LONG tmEnd);
	void Clear();
	ULONG Size();
	HRESULT Merge(const FBBlock_1 &);
	HRESULT GetEndTime(LONG *rtmEnd);
private:
	mapFB			m_FBMap;
	mapFB::iterator	m_FBIter;
	LONG m_tmRestictStart = 0, m_tmRestictEnd = 0;
	bool m_bInitIter = false;
};

} /* namespace */

#endif // ECFBBLOCKLIST_H
/** @} */
