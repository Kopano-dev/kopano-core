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
 * Updates the freebusy data
 *
 * @addtogroup libfreebusy
 * @{
 */

#ifndef ECFREEBUSYUPDATE_H
#define ECFREEBUSYUPDATE_H

#include <kopano/zcdefs.h>
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
class ECFreeBusyUpdate _kc_final : public ECUnknown, public IFreeBusyUpdate {
private:
	ECFreeBusyUpdate(IMessage* lpMessage);
public:
	static HRESULT Create(IMessage* lpMessage, ECFreeBusyUpdate **lppECFreeBusyUpdate);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
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
