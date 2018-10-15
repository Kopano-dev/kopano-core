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

class _kc_export InstanceIdMapper final {
	public:
	static HRESULT Create(std::shared_ptr<ECLogger>, ECConfig *, InstanceIdMapperPtr *);
	_kc_hidden HRESULT GetMappedInstanceId(const SBinary &src_server_uid, ULONG src_instance_id_size, LPENTRYID src_instance_id, const SBinary &dst_server_uid, ULONG *dst_instance_id_size, LPENTRYID *dst_instance_id);
	_kc_hidden HRESULT SetMappedInstances(ULONG prop_id, const SBinary &src_server_uid, ULONG src_instance_id_size, LPENTRYID src_instance_id, const SBinary &dst_server_uid, ULONG dst_instance_id_size, LPENTRYID dst_instance_id);

	private:
	_kc_hidden InstanceIdMapper(std::shared_ptr<ECLogger>);
	_kc_hidden HRESULT Init(ECConfig *);

	std::shared_ptr<KCMDatabaseMySQL> m_ptrDatabase;
};

}} /* namespace */

#endif // ndef instanceidmapper_INCLUDED
