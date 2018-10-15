/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/**
 * @file
 * Updates the freebusy data
 *
 * @addtogroup libfreebusy
 * @{
 */

#ifndef ECFREEBUSYUPDATE_H
#define ECFREEBUSYUPDATE_H

#include "freebusy.h"
#include "freebusyguid.h"
#include <kopano/ECUnknown.h>
#include <kopano/ECGuid.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include <mapi.h>
#include <mapidefs.h>
#include "ECFBBlockList.h"

namespace KC {

/**
 * Implementatie of the IFreeBusyUpdate interface
 */
class ECFreeBusyUpdate final : public ECUnknown, public IFreeBusyUpdate {
private:
	ECFreeBusyUpdate(IMessage* lpMessage);
public:
	static HRESULT Create(IMessage* lpMessage, ECFreeBusyUpdate **lppECFreeBusyUpdate);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	virtual HRESULT Reload(void) { return S_OK; }
	virtual HRESULT PublishFreeBusy(const FBBlock_1 *, ULONG nblks);
	virtual HRESULT RemoveAppt(void) { return S_OK; }
	virtual HRESULT ResetPublishedFreeBusy();
	virtual HRESULT ChangeAppt(void) { return S_OK; }
	virtual HRESULT SaveChanges(const FILETIME &start, const FILETIME &end) override;
	virtual HRESULT GetFBTimes(void) { return S_OK; }
	virtual HRESULT Intersect(void) { return S_OK; }

private:
	object_ptr<IMessage> m_lpMessage; /**< Pointer to the free/busy message received from GetFreeBusyMessage */
	ECFBBlockList	m_fbBlockList; /**< Freebusy time blocks */
	ALLOC_WRAP_FRIEND;
};

} /* namespace */

#endif // ECFREEBUSYUPDATE_H

/** @} */
