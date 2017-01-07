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

#include <kopano/platform.h>
#include <memory>
#include "instanceidmapper.h"
#include "Archiver.h"
#include "ECDatabase.h"
#include <kopano/stringutil.h>

namespace za { namespace operations {

HRESULT InstanceIdMapper::Create(ECLogger *lpLogger, ECConfig *lpConfig, InstanceIdMapperPtr *lpptrMapper)
{
	HRESULT hr = hrSuccess;
	std::unique_ptr<InstanceIdMapper> lpMapper;
	ECConfig *lpLocalConfig = lpConfig;

	// Get config if required.
	if (lpLocalConfig == NULL) {
		lpLocalConfig = ECConfig::Create(Archiver::GetConfigDefaults());
		if (!lpLocalConfig->LoadSettings(Archiver::GetConfigPath())) {
			// Just log warnings and errors and continue with default.
			LogConfigErrors(lpLocalConfig);
		}
	}

	lpMapper.reset(new(std::nothrow) InstanceIdMapper(lpLogger));
	if (lpMapper == nullptr) {
		hr = MAPI_E_NOT_ENOUGH_MEMORY;
		goto exit;
	}

	hr = lpMapper->Init(lpLocalConfig);
	if (hr != hrSuccess)
		goto exit;
	lpptrMapper->reset(lpMapper.release());
exit:
	if (lpConfig == NULL) {
		ASSERT(lpLocalConfig != NULL);
		delete lpLocalConfig;
	}

	return hr;
}

InstanceIdMapper::InstanceIdMapper(ECLogger *lpLogger)
: m_ptrDatabase(new ECDatabase(lpLogger))
{ }

HRESULT InstanceIdMapper::Init(ECConfig *lpConfig)
{
	ECRESULT er = erSuccess;
	
	er = m_ptrDatabase->Connect(lpConfig);
	if (er == KCERR_DATABASE_NOT_FOUND) {
		m_ptrDatabase->GetLogger()->Log(EC_LOGLEVEL_INFO, "Database not found, creating database.");
		er = m_ptrDatabase->CreateDatabase(lpConfig);
	}
	
	if (er != erSuccess)
		m_ptrDatabase->GetLogger()->Log(EC_LOGLEVEL_FATAL, "Database connection failed: %s", m_ptrDatabase->GetError());

	return kcerr_to_mapierr(er);
}

HRESULT InstanceIdMapper::GetMappedInstanceId(const SBinary &sourceServerUID, ULONG cbSourceInstanceID, LPENTRYID lpSourceInstanceID, const SBinary &destServerUID, ULONG *lpcbDestInstanceID, LPENTRYID *lppDestInstanceID)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	string strQuery;
	DB_RESULT lpResult = NULL;
	DB_ROW lpDBRow = NULL;
	DB_LENGTHS lpLengths = NULL;

	if (cbSourceInstanceID == 0 || lpSourceInstanceID == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	strQuery =
		"SELECT m_dst.val_binary FROM za_mappings AS m_dst "
			"JOIN za_mappings AS m_src ON m_dst.instance_id = m_src.instance_id AND m_dst.tag = m_src.tag AND m_src.val_binary = " + m_ptrDatabase->EscapeBinary((LPBYTE)lpSourceInstanceID, cbSourceInstanceID) + " "
			"JOIN za_servers AS s_dst ON m_dst.server_id = s_dst.id AND s_dst.guid = " + m_ptrDatabase->EscapeBinary(destServerUID.lpb, destServerUID.cb) + " "
			"JOIN za_servers AS s_src ON m_src.server_id = s_src.id AND s_src.guid = " + m_ptrDatabase->EscapeBinary(sourceServerUID.lpb, sourceServerUID.cb);

	er = m_ptrDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess) {
		hr = kcerr_to_mapierr(er);
		goto exit;
	}

	switch (m_ptrDatabase->GetNumRows(lpResult)) {
		case 0:
			hr = MAPI_E_NOT_FOUND;
			goto exit;

		case 1:
			break;

		default:	// This should be impossible.
			hr = MAPI_E_DISK_ERROR;	// MAPI version of KCERR_DATABASE_ERROR
			m_ptrDatabase->GetLogger()->Log(EC_LOGLEVEL_FATAL, "InstanceIdMapper::GetMappedInstanceId(): GetNumRows failed");
			goto exit;
	}

	lpDBRow = m_ptrDatabase->FetchRow(lpResult);
	if (lpDBRow == NULL || lpDBRow[0] == NULL) {
		m_ptrDatabase->GetLogger()->Log(EC_LOGLEVEL_FATAL, "InstanceIdMapper::GetMappedInstanceId(): FetchRow failed");
		hr = MAPI_E_DISK_ERROR;	// MAPI version of KCERR_DATABASE_ERROR
		goto exit;
	}

	lpLengths = m_ptrDatabase->FetchRowLengths(lpResult);
	if (lpLengths == NULL || lpLengths[0] == 0) {
		m_ptrDatabase->GetLogger()->Log(EC_LOGLEVEL_FATAL, "InstanceIdMapper::GetMappedInstanceId(): FetchRowLengths failed");
		hr = MAPI_E_DISK_ERROR;	// MAPI version of KCERR_DATABASE_ERROR
		goto exit;
	}

	hr = MAPIAllocateBuffer(lpLengths[0], (LPVOID*)lppDestInstanceID);
	if (hr != hrSuccess)
		goto exit;

	memcpy(*lppDestInstanceID, lpDBRow[0], lpLengths[0]);
	*lpcbDestInstanceID = lpLengths[0];

exit:
	if (lpResult)
		m_ptrDatabase->FreeResult(lpResult);

	return hr;
}

HRESULT InstanceIdMapper::SetMappedInstances(ULONG ulPropTag, const SBinary &sourceServerUID, ULONG cbSourceInstanceID, LPENTRYID lpSourceInstanceID, const SBinary &destServerUID, ULONG cbDestInstanceID, LPENTRYID lpDestInstanceID)
{
	ECRESULT er = erSuccess;
	string strQuery;
	DB_RESULT lpResult = NULL;
	DB_ROW lpDBRow = NULL;

	if (cbSourceInstanceID == 0 || lpSourceInstanceID == NULL || cbDestInstanceID == 0 || lpDestInstanceID == NULL) {
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	er = m_ptrDatabase->Begin();
	if (er != erSuccess)
		goto exit;

	// Make sure the server entries exist.
	strQuery = "INSERT IGNORE INTO za_servers (guid) VALUES (" + m_ptrDatabase->EscapeBinary(sourceServerUID.lpb, sourceServerUID.cb) + "),(" +  m_ptrDatabase->EscapeBinary(destServerUID.lpb, destServerUID.cb) + ")";
	er = m_ptrDatabase->DoInsert(strQuery, NULL, NULL);
	if (er != erSuccess)
		goto exit;

	// Now first see if the source instance is available.
	strQuery = "SELECT instance_id FROM za_mappings AS m JOIN za_servers AS s ON m.server_id = s.id AND s.guid = " + m_ptrDatabase->EscapeBinary(sourceServerUID.lpb, sourceServerUID.cb) + " "
					"WHERE m.val_binary = " + m_ptrDatabase->EscapeBinary((LPBYTE)lpSourceInstanceID, cbSourceInstanceID) + " AND tag = " + stringify(PROP_ID(ulPropTag));
	er = m_ptrDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		goto exit;

	lpDBRow = m_ptrDatabase->FetchRow(lpResult);
	if (lpDBRow == NULL) {
		unsigned int ulNewId;

		m_ptrDatabase->FreeResult(lpResult);
		lpResult = NULL;
		
		strQuery = "INSERT INTO za_instances (tag) VALUES (" + stringify(PROP_ID(ulPropTag)) + ")";
		er = m_ptrDatabase->DoInsert(strQuery, &ulNewId, NULL);
		if (er != erSuccess)
			goto exit;

		strQuery = "INSERT IGNORE INTO za_mappings (server_id, val_binary, tag, instance_id) VALUES "
						"((SELECT id FROM za_servers WHERE guid = " + m_ptrDatabase->EscapeBinary(sourceServerUID.lpb, sourceServerUID.cb) + ")," + m_ptrDatabase->EscapeBinary((LPBYTE)lpSourceInstanceID, cbSourceInstanceID) + "," + stringify(PROP_ID(ulPropTag)) + "," + stringify(ulNewId) + "),"
						"((SELECT id FROM za_servers WHERE guid = " + m_ptrDatabase->EscapeBinary(destServerUID.lpb, destServerUID.cb) + ")," + m_ptrDatabase->EscapeBinary((LPBYTE)lpDestInstanceID, cbDestInstanceID) + "," + stringify(PROP_ID(ulPropTag)) + "," + stringify(ulNewId) + ")";
	} else {	// Source instance id is known
		strQuery = "REPLACE INTO za_mappings (server_id, val_binary, tag, instance_id) VALUES "
						"((SELECT id FROM za_servers WHERE guid = " + m_ptrDatabase->EscapeBinary(destServerUID.lpb, destServerUID.cb) + ")," + m_ptrDatabase->EscapeBinary((LPBYTE)lpDestInstanceID, cbDestInstanceID) + "," + stringify(PROP_ID(ulPropTag)) + "," + lpDBRow[0] + ")";

		m_ptrDatabase->FreeResult(lpResult);
		lpResult = NULL;
	}
	er = m_ptrDatabase->DoInsert(strQuery, NULL, NULL);
	if (er != erSuccess)
		goto exit;

	er = m_ptrDatabase->Commit();

exit:
	if (er != erSuccess)
		m_ptrDatabase->Rollback();

	return kcerr_to_mapierr(er);
}

}} // namespaces operations, za
