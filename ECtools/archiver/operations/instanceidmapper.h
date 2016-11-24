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
typedef std::shared_ptr<KCMDatabaseMySQL> DatabasePtr;

namespace operations {

class _kc_export InstanceIdMapper _kc_final {
	public:
		static HRESULT Create(ECLogger *lpLogger, ECConfig *lpConfig, InstanceIdMapperPtr *lpptrMapper);
	_kc_hidden HRESULT GetMappedInstanceId(const SBinary &src_server_uid, ULONG src_instance_id_size, LPENTRYID src_instance_id, const SBinary &dst_server_uid, ULONG *dst_instance_id_size, LPENTRYID *dst_instance_id);
	_kc_hidden HRESULT SetMappedInstances(ULONG prop_id, const SBinary &src_server_uid, ULONG src_instance_id_size, LPENTRYID src_instance_id, const SBinary &dst_server_uid, ULONG dst_instance_id_size, LPENTRYID dst_instance_id);

	private:
	_kc_hidden InstanceIdMapper(ECLogger *);
	_kc_hidden HRESULT Init(ECConfig *);

	private:
		DatabasePtr m_ptrDatabase;
};

}} /* namespace */

#endif // ndef instanceidmapper_INCLUDED
