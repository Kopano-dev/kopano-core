/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <memory>
#include "postsaveaction.h"
#include <kopano/archiver-common.h>
#include <kopano/memory.hpp>
#include <list>

namespace KC { namespace operations {

class InstanceIdMapper;

class TaskBase {
public:
	TaskBase(IAttach *src, IMessage *dst, unsigned int dst_at_idx);
	HRESULT Execute(unsigned int tag, const std::shared_ptr<InstanceIdMapper> &);

private:
	HRESULT GetUniqueIDs(IAttach *lpAttach, LPSPropValue *lppServerUID, ULONG *lpcbInstanceID, LPENTRYID *lppInstanceID);
	virtual HRESULT DoExecute(unsigned int tag, const std::shared_ptr<InstanceIdMapper> &, const SBinary &src_server_uid, unsigned int src_size, ENTRYID *src_inst, const SBinary &dst_server_uid, unsigned int dest_size, ENTRYID *dest_inst) = 0;

	object_ptr<IAttach> m_ptrSourceAttach;
	object_ptr<IMessage> m_ptrDestMsg;
	ULONG 	m_ulDestAttachIdx;
};

class TaskMapInstanceId final : public TaskBase {
public:
	TaskMapInstanceId(IAttach *src, IMessage *dst, unsigned int dst_at_num);
	HRESULT DoExecute(unsigned int proptag, const std::shared_ptr<InstanceIdMapper> &, const SBinary &src_server_uid, unsigned int src_size, ENTRYID *src_inst, const SBinary &dest_server_uid, unsigned int dest_size, ENTRYID *dest_inst) override;
};

class TaskVerifyAndUpdateInstanceId final : public TaskBase {
public:
	TaskVerifyAndUpdateInstanceId(IAttach *src, IMessage *dst, unsigned int dst_at_num, unsigned int dst_instance_idsize, ENTRYID *dst_instance_id);
	HRESULT DoExecute(unsigned int proptag, const std::shared_ptr<InstanceIdMapper> &, const SBinary &src_server_uid, unsigned int src_size, ENTRYID *src_inst, const SBinary &dest_server_uid, unsigned int dest_size, ENTRYID *dest_inst) override;

private:
	entryid_t m_destInstanceID;
};

class PostSaveInstanceIdUpdater final : public IPostSaveAction {
public:
	PostSaveInstanceIdUpdater(unsigned int tag, const std::shared_ptr<InstanceIdMapper> &, const std::list<std::shared_ptr<TaskBase>> &deferred);
	HRESULT Execute() override;

private:
	ULONG m_ulPropTag;
	std::shared_ptr<InstanceIdMapper> m_ptrMapper;
	std::list<std::shared_ptr<TaskBase>> m_lstDeferred;
};

}} /* namespace */
