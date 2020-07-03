/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <new>
#include <utility>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/scope.hpp>
#include "instanceidmapper.h"
#include "Archiver.h"
#include <kopano/stringutil.h>
#include "arc_mysql.hpp"

namespace KC { namespace operations {

HRESULT InstanceIdMapper::Create(std::shared_ptr<ECLogger> lpLogger,
    ECConfig *lpConfig, std::shared_ptr<InstanceIdMapper> *lpptrMapper)
{
	std::unique_ptr<InstanceIdMapper> lpMapper;
	std::unique_ptr<ECConfig> lpLocalConfig;

	// Get config if required.
	if (lpConfig == nullptr) {
		lpLocalConfig.reset(ECConfig::Create(Archiver::GetConfigDefaults()));
		if (!lpLocalConfig->LoadSettings(Archiver::GetConfigPath()))
			// Just log warnings and errors and continue with default.
			LogConfigErrors(lpLocalConfig.get());
		lpConfig = lpLocalConfig.get();
	}
	lpMapper.reset(new(std::nothrow) InstanceIdMapper(std::move(lpLogger)));
	if (lpMapper == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	auto hr = lpMapper->Init(lpConfig);
	if (hr != hrSuccess)
		return hr;
	static_assert(sizeof(InstanceIdMapper) || true, "incomplete type must not be used");
	*lpptrMapper = std::move(lpMapper);
	return hrSuccess;
}

InstanceIdMapper::InstanceIdMapper(std::shared_ptr<ECLogger>) :
	m_ptrDatabase(new KCMDatabaseMySQL)
{ }

HRESULT InstanceIdMapper::Init(ECConfig *lpConfig)
{
	auto er = m_ptrDatabase->Connect(lpConfig);
	if (er == KCERR_DATABASE_NOT_FOUND) {
		ec_log_info("Database not found, creating database.");
		er = m_ptrDatabase->CreateDatabase(lpConfig, true);
		if (er == erSuccess)
			er = m_ptrDatabase->CreateTables(lpConfig);
	}
	if (er != erSuccess)
		ec_log_crit("Database connection failed: %s", m_ptrDatabase->GetError());
	return kcerr_to_mapierr(er);
}

HRESULT InstanceIdMapper::GetMappedInstanceId(const SBinary &sourceServerUID, ULONG cbSourceInstanceID, LPENTRYID lpSourceInstanceID, const SBinary &destServerUID, ULONG *lpcbDestInstanceID, LPENTRYID *lppDestInstanceID)
{
	if (cbSourceInstanceID == 0 || lpSourceInstanceID == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	DB_RESULT lpResult;
	auto strQuery =
		"SELECT m_dst.val_binary FROM za_mappings AS m_dst "
		"JOIN za_mappings AS m_src ON m_dst.instance_id = m_src.instance_id AND m_dst.tag = m_src.tag AND m_src.val_binary = " + m_ptrDatabase->EscapeBinary(lpSourceInstanceID, cbSourceInstanceID) + " "
		"JOIN za_servers AS s_dst ON m_dst.server_id = s_dst.id AND s_dst.guid = " + m_ptrDatabase->EscapeBinary(destServerUID) + " "
		"JOIN za_servers AS s_src ON m_src.server_id = s_src.id AND s_src.guid = " + m_ptrDatabase->EscapeBinary(sourceServerUID) +
		" LIMIT 2";
	auto er = m_ptrDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		return kcerr_to_mapierr(er);

	switch (lpResult.get_num_rows()) {
	case 0:
		return MAPI_E_NOT_FOUND;
	case 1:
		break;
	default:	// This should be impossible.
		ec_log_crit("InstanceIdMapper::GetMappedInstanceId(): GetNumRows failed");
		return MAPI_E_DISK_ERROR; // MAPI version of KCERR_DATABASE_ERROR
	}

	auto lpDBRow = lpResult.fetch_row();
	if (lpDBRow == NULL || lpDBRow[0] == NULL) {
		ec_log_crit("InstanceIdMapper::GetMappedInstanceId(): FetchRow failed");
		return MAPI_E_DISK_ERROR; // MAPI version of KCERR_DATABASE_ERROR
	}
	auto lpLengths = lpResult.fetch_row_lengths();
	if (lpLengths == NULL || lpLengths[0] == 0) {
		ec_log_crit("InstanceIdMapper::GetMappedInstanceId(): FetchRowLengths failed");
		return MAPI_E_DISK_ERROR; // MAPI version of KCERR_DATABASE_ERROR
	}
	auto hr = KAllocCopy(lpDBRow[0], lpLengths[0], reinterpret_cast<void **>(lppDestInstanceID));
	if (hr != hrSuccess)
		return hr;
	*lpcbDestInstanceID = lpLengths[0];
	return hrSuccess;
}

HRESULT InstanceIdMapper::SetMappedInstances(ULONG ulPropTag, const SBinary &sourceServerUID, ULONG cbSourceInstanceID, LPENTRYID lpSourceInstanceID, const SBinary &destServerUID, ULONG cbDestInstanceID, LPENTRYID lpDestInstanceID)
{
	if (cbSourceInstanceID == 0 || lpSourceInstanceID == nullptr ||
	    cbDestInstanceID == 0 || lpDestInstanceID == nullptr)
		return kcerr_to_mapierr(KCERR_INVALID_PARAMETER);

	ECRESULT er = erSuccess;
	DB_RESULT lpResult;
	auto dtx = m_ptrDatabase->Begin(er);
	if (er != erSuccess)
		return kcerr_to_mapierr(er);
	// Make sure the server entries exist.
	auto strQuery = "INSERT IGNORE INTO za_servers (guid) VALUES (" + m_ptrDatabase->EscapeBinary(sourceServerUID) + "),(" +  m_ptrDatabase->EscapeBinary(destServerUID) + ")";
	er = m_ptrDatabase->DoInsert(strQuery, nullptr, nullptr);
	if (er != erSuccess)
		return kcerr_to_mapierr(er);
	// Now first see if the source instance is available.
	strQuery = "SELECT instance_id FROM za_mappings AS m JOIN za_servers AS s ON m.server_id = s.id AND s.guid = " + m_ptrDatabase->EscapeBinary(sourceServerUID) + " "
	           "WHERE m.val_binary = " + m_ptrDatabase->EscapeBinary(lpSourceInstanceID, cbSourceInstanceID) + " AND tag = " + stringify(PROP_ID(ulPropTag)) + " LIMIT 1";
	er = m_ptrDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		return kcerr_to_mapierr(er);

	auto lpDBRow = lpResult.fetch_row();
	if (lpDBRow == NULL) {
		unsigned int ulNewId;

		strQuery = "INSERT INTO za_instances (tag) VALUES (" + stringify(PROP_ID(ulPropTag)) + ")";
		er = m_ptrDatabase->DoInsert(strQuery, &ulNewId, NULL);
		if (er != erSuccess)
			return kcerr_to_mapierr(er);
		strQuery = "INSERT IGNORE INTO za_mappings (server_id, val_binary, tag, instance_id) VALUES "
		           "((SELECT id FROM za_servers WHERE guid = " + m_ptrDatabase->EscapeBinary(sourceServerUID) + ")," + m_ptrDatabase->EscapeBinary(lpSourceInstanceID, cbSourceInstanceID) + "," + stringify(PROP_ID(ulPropTag)) + "," + stringify(ulNewId) + "),"
		           "((SELECT id FROM za_servers WHERE guid = " + m_ptrDatabase->EscapeBinary(destServerUID) + ")," + m_ptrDatabase->EscapeBinary(lpDestInstanceID, cbDestInstanceID) + "," + stringify(PROP_ID(ulPropTag)) + "," + stringify(ulNewId) + ")";
	} else {	// Source instance id is known
		strQuery = "REPLACE INTO za_mappings (server_id, val_binary, tag, instance_id) VALUES "
		           "((SELECT id FROM za_servers WHERE guid = " + m_ptrDatabase->EscapeBinary(destServerUID) + ")," + m_ptrDatabase->EscapeBinary(lpDestInstanceID, cbDestInstanceID) + "," + stringify(PROP_ID(ulPropTag)) + "," + lpDBRow[0] + ")";
	}
	er = m_ptrDatabase->DoInsert(strQuery, NULL, NULL);
	if (er != erSuccess)
		return kcerr_to_mapierr(er);
	return dtx.commit();
}

}} /* namespace */
