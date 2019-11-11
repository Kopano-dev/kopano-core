/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef instanceidmapper_INCLUDED
#define instanceidmapper_INCLUDED

#include <memory>
#include <kopano/zcdefs.h>
#include "instanceidmapper_fwd.h"
#include <mapidefs.h>

namespace KC {

class ECConfig;
class ECLogger;
class KCMDatabaseMySQL;

namespace operations {

class KC_EXPORT InstanceIdMapper final {
	public:
	static HRESULT Create(std::shared_ptr<ECLogger>, ECConfig *, InstanceIdMapperPtr *);
	KC_HIDDEN HRESULT GetMappedInstanceId(const SBinary &src_server_uid, unsigned int src_instance_id_size, ENTRYID *src_instance_id, const SBinary &dst_server_uid, unsigned int *dst_instance_id_size, ENTRYID **dst_instance_id);
	KC_HIDDEN HRESULT SetMappedInstances(unsigned int prop_id, const SBinary &src_server_uid, unsigned int src_instance_id_size, ENTRYID *src_instance_id, const SBinary &dst_server_uid, unsigned int dst_instance_id_size, ENTRYID *dst_instance_id);

	private:
	KC_HIDDEN InstanceIdMapper(std::shared_ptr<ECLogger>);
	KC_HIDDEN HRESULT Init(ECConfig *);

	std::shared_ptr<KCMDatabaseMySQL> m_ptrDatabase;
};

}} /* namespace */

#endif // ndef instanceidmapper_INCLUDED
