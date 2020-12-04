/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <memory>
#include <kopano/zcdefs.h>
#include <mapidefs.h>

namespace KC {

class Config;
class Logger;
class KCMDatabaseMySQL;

namespace operations {

class KC_EXPORT InstanceIdMapper final {
	public:
	static HRESULT Create(std::shared_ptr<Logger>, Config *, std::shared_ptr<InstanceIdMapper> *);
	KC_HIDDEN HRESULT GetMappedInstanceId(const SBinary &src_server_uid, unsigned int src_instance_id_size, ENTRYID *src_instance_id, const SBinary &dst_server_uid, unsigned int *dst_instance_id_size, ENTRYID **dst_instance_id);
	KC_HIDDEN HRESULT SetMappedInstances(unsigned int prop_id, const SBinary &src_server_uid, unsigned int src_instance_id_size, ENTRYID *src_instance_id, const SBinary &dst_server_uid, unsigned int dst_instance_id_size, ENTRYID *dst_instance_id);

	private:
	KC_HIDDEN InstanceIdMapper(std::shared_ptr<Logger>);
	KC_HIDDEN HRESULT Init(Config *);

	std::shared_ptr<KCMDatabaseMySQL> m_ptrDatabase;
};

}} /* namespace */
