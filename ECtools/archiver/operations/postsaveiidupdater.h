/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef postsaveiidupdater_INCLUDED
#define postsaveiidupdater_INCLUDED

#include <memory>
#include "postsaveaction.h"
#include <kopano/mapi_ptr.h>
#include "instanceidmapper_fwd.h"
#include <kopano/archiver-common.h>
#include <list>

namespace KC { namespace operations {

class TaskBase {
public:
	TaskBase(const AttachPtr &ptrSourceAttach, const MessagePtr &ptrDestMsg, ULONG ulDestAttachIdx);

	HRESULT Execute(ULONG ulPropTag, const InstanceIdMapperPtr &ptrMapper);

private:
	HRESULT GetUniqueIDs(IAttach *lpAttach, LPSPropValue *lppServerUID, ULONG *lpcbInstanceID, LPENTRYID *lppInstanceID);
	virtual HRESULT DoExecute(ULONG ulPropTag, const InstanceIdMapperPtr &ptrMapper, const SBinary &sourceServerUID, ULONG cbSourceInstanceID, LPENTRYID lpSourceInstanceID, const SBinary &destServerUID, ULONG cbDestInstanceID, LPENTRYID lpDestInstanceID) = 0;

	AttachPtr	m_ptrSourceAttach;
	MessagePtr	m_ptrDestMsg;
	ULONG 	m_ulDestAttachIdx;
};
typedef std::shared_ptr<TaskBase> TaskPtr;
typedef std::list<TaskPtr> TaskList;

class TaskMapInstanceId final : public TaskBase {
public:
	TaskMapInstanceId(const AttachPtr &ptrSourceAttach, const MessagePtr &ptrDestMsg, ULONG ulDestAttachNum);
	HRESULT DoExecute(unsigned int proptag, const InstanceIdMapperPtr &, const SBinary &src_server_uid, unsigned int src_size, ENTRYID *src_inst, const SBinary &dest_server_uid, unsigned int dest_size, ENTRYID *dest_inst) override;
};

class TaskVerifyAndUpdateInstanceId final : public TaskBase {
public:
	TaskVerifyAndUpdateInstanceId(const AttachPtr &ptrSourceAttach, const MessagePtr &ptrDestMsg, ULONG ulDestAttachNum, ULONG cbDestInstanceID, LPENTRYID lpDestInstanceID);
	HRESULT DoExecute(unsigned int proptag, const InstanceIdMapperPtr &, const SBinary &src_server_uid, unsigned int src_size, ENTRYID *src_inst, const SBinary &dest_server_uid, unsigned int dest_size, ENTRYID *dest_inst) override;

private:
	entryid_t m_destInstanceID;
};

class PostSaveInstanceIdUpdater final : public IPostSaveAction {
public:

	PostSaveInstanceIdUpdater(ULONG ulPropTag, const InstanceIdMapperPtr &ptrMapper, const TaskList &lstDeferred);
	HRESULT Execute() override;

private:
	ULONG m_ulPropTag;
	InstanceIdMapperPtr m_ptrMapper;
	TaskList m_lstDeferred;
};

}} /* namespace */

#endif // ndef postsaveiidupdater_INCLUDED
