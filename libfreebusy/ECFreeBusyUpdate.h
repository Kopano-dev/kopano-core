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
#include <kopano/ECDebug.h>
#include <kopano/ECGuid.h>
#include <kopano/Util.h>
#include <mapi.h>
#include <mapidefs.h>

#include "ECFBBlockList.h"

namespace KC {

/**
 * Implementatie of the IFreeBusyUpdate interface
 */
class ECFreeBusyUpdate _kc_final : public ECUnknown {
private:
	ECFreeBusyUpdate(IMessage* lpMessage);
	~ECFreeBusyUpdate(void);
public:
	static HRESULT Create(IMessage* lpMessage, ECFreeBusyUpdate **lppECFreeBusyUpdate);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT Reload(void) { return S_OK; }
	virtual HRESULT PublishFreeBusy(FBBlock_1 *lpBlocks, ULONG nBlocks);
	virtual HRESULT RemoveAppt(void) { return S_OK; }
	virtual HRESULT ResetPublishedFreeBusy();
	virtual HRESULT ChangeAppt(void) { return S_OK; }
	virtual HRESULT SaveChanges(FILETIME ftStart, FILETIME ftEnd);
	virtual HRESULT GetFBTimes(void) { return S_OK; }
	virtual HRESULT Intersect(void) { return S_OK; }

	class xFreeBusyUpdate _kc_final : public IFreeBusyUpdate {
		#include <kopano/xclsfrag/IUnknown.hpp>
		// <kopano/xclsfrag/IFreeBusyUpdate.hpp>
		virtual HRESULT __stdcall Reload(void) _kc_override;
		virtual HRESULT __stdcall PublishFreeBusy(FBBlock_1 *lpBlocks, ULONG nBlocks) _kc_override;
		virtual HRESULT __stdcall RemoveAppt(void) _kc_override;
		virtual HRESULT __stdcall ResetPublishedFreeBusy(void) _kc_override;
		virtual HRESULT __stdcall ChangeAppt(void) _kc_override;
		virtual HRESULT __stdcall SaveChanges(FILETIME ftBegin, FILETIME ftEnd) _kc_override;
		virtual HRESULT __stdcall GetFBTimes(void) _kc_override;
		virtual HRESULT __stdcall Intersect(void) _kc_override;
	} m_xFreeBusyUpdate;

private:
	IMessage*		m_lpMessage; /**< Pointer to the free/busy message received from GetFreeBusyMessage */
	ECFBBlockList	m_fbBlockList; /**< Freebusy time blocks */
	ALLOC_WRAP_FRIEND;
};

} /* namespace */

#endif // ECFREEBUSYUPDATE_H

/** @} */
