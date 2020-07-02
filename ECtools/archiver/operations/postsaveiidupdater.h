/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <memory>
#include "postsaveaction.h"
#include "instanceidmapper_fwd.h"
#include <kopano/archiver-common.h>
#include <kopano/memory.hpp>
#include <list>

namespace KC { namespace operations {

class TaskBase {
public:
	TaskBase(IAttach *src, IMessage *dst, unsigned int dst_at_idx);
	HRESULT Execute(ULONG ulPropTag, const InstanceIdMapperPtr &ptrMapper);

private:
	HRESULT GetUniqueIDs(IAttach *lpAttach, LPSPropValue *lppServerUID, ULONG *lpcbInstanceID, LPENTRYID *lppInstanceID);
	virtual HRESULT DoExecute(ULONG ulPropTag, const InstanceIdMapperPtr &ptrMapper, const SBinary &sourceServerUID, ULONG cbSourceInstanceID, LPENTRYID lpSourceInstanceID, const SBinary &destServerUID, ULONG cbDestInstanceID, LPENTRYID lpDestInstanceID) = 0;

	object_ptr<IAttach> m_ptrSourceAttach;
	object_ptr<IMessage> m_ptrDestMsg;
	ULONG 	m_ulDestAttachIdx;
};
typedef std::shared_ptr<TaskBase> TaskPtr;

class TaskMapInstanceId final : public TaskBase {
public:
	TaskMapInstanceId(IAttach *src, IMessage *dst, unsigned int dst_at_num);
	HRESULT DoExecute(unsigned int proptag, const InstanceIdMapperPtr &, const SBinary &src_server_uid, unsigned int src_size, ENTRYID *src_inst, const SBinary &dest_server_uid, unsigned int dest_size, ENTRYID *dest_inst) override;
};

class TaskVerifyAndUpdateInstanceId final : public TaskBase {
public:
	TaskVerifyAndUpdateInstanceId(IAttach *src, IMessage *dst, unsigned int dst_at_num, unsigned int dst_instance_idsize, ENTRYID *dst_instance_id);
	HRESULT DoExecute(unsigned int proptag, const InstanceIdMapperPtr &, const SBinary &src_server_uid, unsigned int src_size, ENTRYID *src_inst, const SBinary &dest_server_uid, unsigned int dest_size, ENTRYID *dest_inst) override;

private:
	entryid_t m_destInstanceID;
};

class PostSaveInstanceIdUpdater final : public IPostSaveAction {
public:
	PostSaveInstanceIdUpdater(unsigned int tag, const InstanceIdMapperPtr &, const std::list<TaskPtr> &deferred);
	HRESULT Execute() override;

private:
	ULONG m_ulPropTag;
	InstanceIdMapperPtr m_ptrMapper;
	std::list<TaskPtr> m_lstDeferred;
};

}} /* namespace */
