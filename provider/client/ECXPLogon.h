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

#ifndef ECXPLOGON_H
#define ECXPLOGON_H

#include <kopano/zcdefs.h>
#include <condition_variable>
#include <mutex>
#include <kopano/ECUnknown.h>
#include "IMAPIOffline.h"
#include <string>

/*typedef struct _MAILBOX_INFO {
	std::string		strFullName;

}MAILBOX_INFO, LPMAILBOX_INFO*;
*/
class ECXPProvider;

class ECXPLogon : public ECUnknown
{
protected:
	ECXPLogon(const std::string &strProfileName, BOOL bOffline, ECXPProvider *lpXPProvider, LPMAPISUP lpMAPISup);
	virtual ~ECXPLogon();

public:
	static  HRESULT Create(const std::string &strProfileName, BOOL bOffline, ECXPProvider *lpXPProvider, LPMAPISUP lpMAPISup, ECXPLogon **lppECXPLogon);

	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);

	virtual HRESULT AddressTypes(ULONG * lpulFlags, ULONG * lpcAdrType, LPTSTR ** lpppszAdrTypeArray, ULONG * lpcMAPIUID, LPMAPIUID  ** lpppUIDArray);
	virtual HRESULT RegisterOptions(ULONG * lpulFlags, ULONG * lpcOptions, LPOPTIONDATA * lppOptions);
	virtual HRESULT TransportNotify(ULONG * lpulFlags, LPVOID * lppvData);
	virtual HRESULT Idle(ULONG ulFlags);
	virtual HRESULT TransportLogoff(ULONG ulFlags);

	virtual HRESULT SubmitMessage(ULONG ulFlags, LPMESSAGE lpMessage, ULONG * lpulMsgRef, ULONG * lpulReturnParm);
	virtual HRESULT EndMessage(ULONG ulMsgRef, ULONG * lpulFlags);
	virtual HRESULT Poll(ULONG * lpulIncoming);
	virtual HRESULT StartMessage(ULONG ulFlags, LPMESSAGE lpMessage, ULONG * lpulMsgRef);
	virtual HRESULT OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG * lpulObjType, LPMAPISTATUS * lppEntry);
	virtual HRESULT ValidateState(ULONG ulUIParam, ULONG ulFlags);
	virtual HRESULT FlushQueues(ULONG ulUIParam, ULONG cbTargetTransport, LPENTRYID lpTargetTransport, ULONG ulFlags);

	class xXPLogon _zcp_final : public IXPLogon {
		// IUnknown
		virtual ULONG __stdcall AddRef(void) _zcp_override;
		virtual ULONG __stdcall Release(void) _zcp_override;
		virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;

		//IXPLogon
		virtual HRESULT __stdcall AddressTypes(ULONG * lpulFlags, ULONG * lpcAdrType, LPTSTR ** lpppszAdrTypeArray, ULONG * lpcMAPIUID, LPMAPIUID ** lpppUIDArray);
		virtual HRESULT __stdcall RegisterOptions(ULONG * lpulFlags, ULONG * lpcOptions, LPOPTIONDATA * lppOptions);
		virtual HRESULT __stdcall TransportNotify(ULONG * lpulFlags, LPVOID * lppvData);
		virtual HRESULT __stdcall Idle(ULONG ulFlags);
		virtual HRESULT __stdcall TransportLogoff(ULONG ulFlags);
		virtual HRESULT __stdcall SubmitMessage(ULONG ulFlags, LPMESSAGE lpMessage, ULONG * lpulMsgRef, ULONG * lpulReturnParm);
		virtual HRESULT __stdcall EndMessage(ULONG ulMsgRef, ULONG * lpulFlags);
		virtual HRESULT __stdcall Poll(ULONG * lpulIncoming);
		virtual HRESULT __stdcall StartMessage(ULONG ulFlags, LPMESSAGE lpMessage, ULONG * lpulMsgRef);
		virtual HRESULT __stdcall OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG * lpulObjType, LPMAPISTATUS * lppEntry);
		virtual HRESULT __stdcall ValidateState(ULONG ulUIParam, ULONG ulFlags);
		virtual HRESULT __stdcall FlushQueues(ULONG ulUIParam, ULONG cbTargetTransport, LPENTRYID lpTargetTransport, ULONG ulFlags);

	} m_xXPLogon;

private:
	class xMAPIAdviseSink _zcp_final : public IMAPIAdviseSink {
	public:
		HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;
		ULONG __stdcall AddRef(void) _zcp_override;
		ULONG __stdcall Release(void) _zcp_override;

		ULONG __stdcall OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifs);
	} m_xMAPIAdviseSink;

	ULONG OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifs);

	HRESULT HrUpdateTransportStatus();
	HRESULT SetOutgoingProps (LPMESSAGE lpMessage);
	HRESULT ClearOldSubmittedMessages(LPMAPIFOLDER lpFolder);
private:
	LPMAPISUP		m_lpMAPISup;
	LPTSTR			*m_lppszAdrTypeArray;
	ULONG			m_ulTransportStatus;
	ECXPProvider	*m_lpXPProvider;
	bool			m_bCancel;
	std::condition_variable m_hExitSignal;
	std::mutex m_hExitMutex;
	ULONG			m_bOffline;
};

#endif // #ifndef ECXPLOGON_H
