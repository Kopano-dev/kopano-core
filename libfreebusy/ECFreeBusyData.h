/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/**
 * @file
 * Free/busy data for one user
 *
 * @addtogroup libfreebusy
 * @{
 */

#ifndef ECFREEBUSYDATA_H
#define ECFREEBUSYDATA_H

#include <kopano/zcdefs.h>
#include "freebusy.h"
#include "freebusyguid.h"
#include <kopano/ECUnknown.h>
#include <kopano/ECGuid.h>
#include <kopano/Util.h>
#include "ECFBBlockList.h"

namespace KC {

/**
 * Implementatie of the IFreeBusyData interface
 */
class ECFreeBusyData KC_FINAL_OPG : public ECUnknown, public IFreeBusyData {
public:
	static HRESULT Create(LONG start, LONG end, const ECFBBlockList &, ECFreeBusyData **);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT Reload(void *) { return E_NOTIMPL; }
	virtual HRESULT EnumBlocks(IEnumFBBlock **ppenumfb, const FILETIME &start, const FILETIME &end) override;
	virtual HRESULT GetDelegateInfo(void *) { return E_NOTIMPL; }
	virtual HRESULT SetFBRange(LONG rtmStart, LONG rtmEnd);
	virtual HRESULT GetFBPublishRange(LONG *prtmStart, LONG *prtmEnd);

private:
	ECFreeBusyData(LONG start, LONG end, const ECFBBlockList &);

	ECFBBlockList	m_fbBlockList;
	LONG m_rtmStart = 0, m_rtmEnd = 0; /* PR_FREEBUSY_START_RANGE, PR_FREEBUSY_END_RANGE */
	ALLOC_WRAP_FRIEND;
};

} /* namespace */

#endif // ECFREEBUSYDATA_H

/** @} */
