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
#include "instanceidmapper_fwd.h"
#include <mapidefs.h>

class ECConfig;
class ECLogger;
class ECDatabaseMySQL;
typedef std::shared_ptr<ECDatabaseMySQL> DatabasePtr;

namespace za { namespace operations {

class InstanceIdMapper
{
	public:
		static HRESULT Create(ECLogger *lpLogger, ECConfig *lpConfig, InstanceIdMapperPtr *lpptrMapper);
		HRESULT GetMappedInstanceId(const SBinary &sourceServerUID, ULONG cbSourceInstanceID, LPENTRYID lpSourceInstanceID, const SBinary &destServerUID, ULONG *lpcbDestInstanceID, LPENTRYID *lppDestInstanceID);
		HRESULT SetMappedInstances(ULONG ulPropId, const SBinary &sourceServerUID, ULONG cbSourceInstanceID, LPENTRYID lpSourceInstanceID, const SBinary &destServerUID, ULONG cbDestInstanceID, LPENTRYID lpDestInstanceID);

	private:
		InstanceIdMapper(ECLogger *lpLogger);
		HRESULT Init(ECConfig *lpConfig);

	private:
		DatabasePtr m_ptrDatabase;
};

}} // namespace operations, za


#endif // ndef instanceidmapper_INCLUDED
