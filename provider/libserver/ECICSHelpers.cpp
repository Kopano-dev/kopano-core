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
#include <utility>
#include <kopano/zcdefs.h>
#include <memory>
#include <kopano/platform.h>
#include <memory>
#include <kopano/stringutil.h>
#include "ics.h"

#include "ECStoreObjectTable.h"
#include "ECICSHelpers.h"
#include "ECSessionManager.h"
#include "ECMAPI.h"

#include <mapidefs.h>
#include <edkmdb.h>

#include <string>
#include <algorithm>

#include <kopano/ECLogger.h>

namespace KC {

extern ECLogger* g_lpLogger;

extern ECSessionManager*	g_lpSessionManager;

/**
 * IDbQueryCreator: Interface to the database query creators
 **/
class IDbQueryCreator {
public:
	virtual ~IDbQueryCreator(void) _kc_impdtor;
	virtual std::string CreateQuery() = 0;
};

/**
 * CommonQueryCreator: Abstract implementation of IDBQueryCreator that handles the
 *                     common part of all queries.
 **/
class CommonQueryCreator : public IDbQueryCreator {
public:
	CommonQueryCreator(unsigned int ulFlags);
	
	// IDbQueryCreator
	std::string CreateQuery(void) _kc_override;
	
private:
	virtual std::string CreateBaseQuery() = 0;
	virtual std::string CreateOrderQuery() = 0;
	
	unsigned int m_ulFlags;
};

CommonQueryCreator::CommonQueryCreator(unsigned int ulFlags)
	: m_ulFlags(ulFlags)
{ }

std::string CommonQueryCreator::CreateQuery()
{
	std::string strQuery = CreateBaseQuery();
	
	if(!strQuery.empty()) {
		
		if ((m_ulFlags & SYNC_ASSOCIATED) == 0)
			strQuery += " AND (ISNULL(hierarchy.flags) OR hierarchy.flags & " + stringify(MSGFLAG_ASSOCIATED) + " = 0) ";

		if ((m_ulFlags & SYNC_NORMAL) == 0)
			strQuery += " AND (ISNULL(hierarchy.flags) OR hierarchy.flags & " + stringify(MSGFLAG_ASSOCIATED) + " = " + stringify(MSGFLAG_ASSOCIATED) + ") ";

		strQuery += CreateOrderQuery();
	}
		
	return strQuery;
}

/**
 * IncrementalQueryCreator: Creates an incremental query. In other words only messages
 *                          that are new or have changed since the last check will be
 *                          returned (deleted is a change is this context).
 **/
class IncrementalQueryCreator _kc_final : public CommonQueryCreator {
public:
	IncrementalQueryCreator(ECDatabase *lpDatabase, unsigned int ulSyncId, unsigned int ulChangeId, const SOURCEKEY &sFolderSourceKey, unsigned int ulFlags);
	
private:
	std::string CreateBaseQuery(void) _kc_override;
	std::string CreateOrderQuery(void) _kc_override;
	
	ECDatabase		*m_lpDatabase;
	unsigned int	m_ulSyncId;
	unsigned int	m_ulChangeId;
	const SOURCEKEY	&m_sFolderSourceKey;
	unsigned int	m_ulFlags;
};

IncrementalQueryCreator::IncrementalQueryCreator(ECDatabase *lpDatabase, unsigned int ulSyncId, unsigned int ulChangeId, const SOURCEKEY &sFolderSourceKey, unsigned int ulFlags)
	: CommonQueryCreator(ulFlags)
	, m_lpDatabase(lpDatabase)
	, m_ulSyncId(ulSyncId)
	, m_ulChangeId(ulChangeId)
	, m_sFolderSourceKey(sFolderSourceKey)
	, m_ulFlags(ulFlags)
{ }
	
std::string IncrementalQueryCreator::CreateBaseQuery()
{
	std::string strQuery;

	strQuery =  "SELECT changes.id, changes.sourcekey, changes.parentsourcekey, changes.change_type, changes.flags, NULL, changes.sourcesync "
				"FROM changes ";
	if ((m_ulFlags & (SYNC_ASSOCIATED | SYNC_NORMAL)) != (SYNC_ASSOCIATED | SYNC_NORMAL))
		strQuery +=	"LEFT JOIN indexedproperties ON indexedproperties.val_binary = changes.sourcekey AND indexedproperties.tag = " + stringify(PROP_ID(PR_SOURCE_KEY)) + " " +
					"LEFT JOIN hierarchy ON hierarchy.id = indexedproperties.hierarchyid ";

	strQuery +=	"WHERE changes.id > " + stringify(m_ulChangeId) + 																	/* Get changes from change ID N onwards */
				"  AND changes.change_type & " + stringify(ICS_MESSAGE) +															/* And change type is message */
				"  AND changes.sourcesync != " + stringify(m_ulSyncId);																/* And we didn't generate this change ourselves */

	if(!m_sFolderSourceKey.empty()) 
		strQuery += "  AND changes.parentsourcekey = " + m_lpDatabase->EscapeBinary(m_sFolderSourceKey, m_sFolderSourceKey.size());	/* Where change took place in Folder X */

	if (m_ulFlags & SYNC_NO_DELETIONS)
		strQuery += " AND changes.change_type & " + stringify(ICS_ACTION_MASK) + " != " + stringify(ICS_SOFT_DELETE) +
					" AND changes.change_type & " + stringify(ICS_ACTION_MASK) + " != " + stringify(ICS_HARD_DELETE);
	else if (m_ulFlags & SYNC_NO_SOFT_DELETIONS)
		strQuery += " AND changes.change_type & " + stringify(ICS_ACTION_MASK) + " != " + stringify(ICS_SOFT_DELETE);

	if ((m_ulFlags & SYNC_READ_STATE) == 0)
		strQuery += " AND changes.change_type & " + stringify(ICS_ACTION_MASK) + " != " + stringify(ICS_FLAG);
		
	return strQuery;
}

std::string IncrementalQueryCreator::CreateOrderQuery()
{
	return " ORDER BY changes.id";
}

/**
 * FullQueryCreator: Create a query that will return all messages for a sync id. The
 *                   messages need to be processed afterwards to see what needs to be
 *                   send to the client.
 **/
class FullQueryCreator _kc_final : public CommonQueryCreator {
public:
	FullQueryCreator(ECDatabase *lpDatabase, const SOURCEKEY &sFolderSourceKey, unsigned int ulFlags, unsigned int ulFilteredSourceSync = 0);
	
private:
	std::string CreateBaseQuery(void) _kc_override;
	std::string CreateOrderQuery(void) _kc_override;
	
	ECDatabase		*m_lpDatabase;
	const SOURCEKEY	&m_sFolderSourceKey;
	unsigned int	m_ulFilteredSourceSync;
};

FullQueryCreator::FullQueryCreator(ECDatabase *lpDatabase, const SOURCEKEY &sFolderSourceKey, unsigned int ulFlags, unsigned int ulFilteredSourceSync)
	: CommonQueryCreator(ulFlags)
	, m_lpDatabase(lpDatabase)
	, m_sFolderSourceKey(sFolderSourceKey)
	, m_ulFilteredSourceSync(ulFilteredSourceSync)
{ }
	
std::string FullQueryCreator::CreateBaseQuery()
{
	std::string strQuery;

	assert(!m_sFolderSourceKey.empty());
	strQuery =  "SELECT changes.id as id, sourcekey.val_binary as sourcekey, parentsourcekey.val_binary, " + stringify(ICS_MESSAGE_NEW) + ", NULL, hierarchy.flags, changes.sourcesync "
				"FROM hierarchy "
				"JOIN indexedproperties as sourcekey ON sourcekey.hierarchyid = hierarchy.id AND sourcekey.tag=" + stringify(PROP_ID(PR_SOURCE_KEY)) + " "
				"JOIN indexedproperties as parentsourcekey ON parentsourcekey.hierarchyid = hierarchy.parent AND parentsourcekey.tag=" + stringify(PROP_ID(PR_SOURCE_KEY)) +
				" LEFT JOIN changes on changes.sourcekey=sourcekey.val_binary AND changes.parentsourcekey=parentsourcekey.val_binary AND changes.change_type=" + stringify(ICS_MESSAGE_NEW) + " ";
				
	strQuery +=	"WHERE parentsourcekey.val_binary = " + m_lpDatabase->EscapeBinary(m_sFolderSourceKey, m_sFolderSourceKey.size()) +
				"  AND hierarchy.type=" + stringify(MAPI_MESSAGE) + " AND hierarchy.flags & 1024 = 0";
	
	if (m_ulFilteredSourceSync)
		strQuery += " AND (changes.sourcesync is NULL OR changes.sourcesync!=" + stringify(m_ulFilteredSourceSync) + ")";
		
	return strQuery;
}

std::string FullQueryCreator::CreateOrderQuery()
{
	return " ORDER BY hierarchy.id DESC";
}

/**
 * NullQueryCreator: Returns no query at all. This is only used for SYNC_CATCHUP syncs that
 * do not have a restriction set. (When a restriction is set, we still need to generate the message set
 * so we cannot optimize anything out then).
 **/
class NullQueryCreator _kc_final : public CommonQueryCreator {
public:
	NullQueryCreator();
	
private:
	std::string CreateBaseQuery(void) _kc_override;
	std::string CreateOrderQuery(void) _kc_override;
};

NullQueryCreator::NullQueryCreator() : CommonQueryCreator(SYNC_CATCHUP)
{ }
	
std::string NullQueryCreator::CreateBaseQuery()
{
	return std::string();
}

std::string NullQueryCreator::CreateOrderQuery()
{
	return std::string();
}

/**
 * IMessageProcessor: Interface to the message processors.
 **/
class IMessageProcessor {
public:
	virtual ~IMessageProcessor(void) _kc_impdtor;
	virtual ECRESULT ProcessAccepted(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType, unsigned int *lpulFlags) = 0;
	virtual ECRESULT ProcessRejected(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType) = 0;
	virtual ECRESULT GetResidualMessages(LPMESSAGESET lpsetResiduals) = 0;
	virtual unsigned int GetMaxChangeId() const = 0;
};

/**
 * NonLegacyIncrementalProcessor: Processes accepted and rejected messages without the burden of tracking
 *                                legacy or checking for presence of messages.
 *                                This processor expects to be used in conjunction with the IncrementalQueryCreator,
 *                                which implies that all changes are genuin changes and no messages will be
 *                                rejected through a restriction.
 **/
class NonLegacyIncrementalProcessor _kc_final : public IMessageProcessor {
public:
	NonLegacyIncrementalProcessor(unsigned int ulMaxChangeId);
	ECRESULT ProcessAccepted(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType, unsigned int *lpulFlags) _kc_override;
	ECRESULT ProcessRejected(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType) _kc_override;
	ECRESULT GetResidualMessages(LPMESSAGESET lpsetResiduals) _kc_override
	{
		/* No legacy, no residuals. */
		return erSuccess;
	}
	unsigned int GetMaxChangeId(void) const _kc_override { return m_ulMaxChangeId; }
	
private:
	unsigned int m_ulMaxChangeId;
};

NonLegacyIncrementalProcessor::NonLegacyIncrementalProcessor(unsigned int ulMaxChangeId)
	: m_ulMaxChangeId(ulMaxChangeId)
{ }

ECRESULT NonLegacyIncrementalProcessor::ProcessAccepted(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType, unsigned int *lpulFlags)
{
	// Since all changes are truly new changes, we'll just set the changetype to whatever we receive
	assert(lpulChangeType != NULL);
	assert(lpDBRow != NULL && lpDBRow[icsChangeType] != NULL);
	assert(lpDBRow != NULL && lpDBRow[icsID] != NULL);
	
	*lpulChangeType = atoui(lpDBRow[icsChangeType]);
	*lpulFlags = lpDBRow[icsFlags] ? atoui(lpDBRow[icsFlags]) : 0;

	ec_log(EC_LOGLEVEL_ICS, "NonLegacyIncrementalAccepted: sourcekey=%s, changetype=%d", bin2hex(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey])).c_str(), *lpulChangeType);
	return erSuccess;
}

ECRESULT NonLegacyIncrementalProcessor::ProcessRejected(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType)
{
	// Since no restriction can be applied when using this processor, we'll never get a reject.
	// We'll set the changetype to 0 anyway.
	assert(false);
	*lpulChangeType = 0;
	ec_log(EC_LOGLEVEL_ICS, "NonLegacyIncrementalRejected: sourcekey=%s, changetype=0", bin2hex(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey])).c_str());
	return erSuccess;
}

/**
 * NonLegacyFullProcessor: Processes accepted and rejected messages without the burden of tracking
 *                         legacy, but allowing messages to be processed that were synced to the 
 *                         client previously. Since we don't have legacy, we assume all messages
 *                         up to the current changeId are on the client.
 **/
class NonLegacyFullProcessor _kc_final : public IMessageProcessor {
public:
	NonLegacyFullProcessor(unsigned int ulChangeId, unsigned int ulSyncId);
	ECRESULT ProcessAccepted(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType, unsigned int *lpulFlags) _kc_override;
	ECRESULT ProcessRejected(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType) _kc_override;
	ECRESULT GetResidualMessages(LPMESSAGESET lpsetResiduals) _kc_override
	{
		/* No legacy, no residuals. */
		return erSuccess;
	}
	unsigned int GetMaxChangeId(void) const _kc_override { return m_ulMaxChangeId; }
	
private:
	unsigned int m_ulChangeId;
	unsigned int m_ulSyncId;
	unsigned int m_ulMaxChangeId;
};

NonLegacyFullProcessor::NonLegacyFullProcessor(unsigned int ulChangeId, unsigned int ulSyncId)
	: m_ulChangeId(ulChangeId)
	, m_ulSyncId(ulSyncId)
	, m_ulMaxChangeId(ulChangeId)
{ }

ECRESULT NonLegacyFullProcessor::ProcessAccepted(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType, unsigned int *lpulFlags)
{
	// This processor will always be used with the FullQueryGenerator, which means that the provided
	// changetype is always ICS_MESSAGE_NEW. However, we do have the message flags so we can see if
	// a message is deleted.
	assert(lpulChangeType != NULL);
	assert(lpDBRow != NULL && lpDBRow[icsChangeType] != NULL && lpDBRow[icsMsgFlags] != NULL);
	assert(atoui(lpDBRow[icsChangeType]) == ICS_MESSAGE_NEW);

	unsigned int ulChange = (lpDBRow[icsID] ? atoui(lpDBRow[icsID]) : 0);
	if (atoui(lpDBRow[icsMsgFlags]) & MSGFLAG_DELETED) {
		if (ulChange <= m_ulChangeId)	// Only delete if present remotely.
			*lpulChangeType = ICS_HARD_DELETE;
	} else {
		unsigned int ulSourceSync = (lpDBRow[icsSourceSync] ? atoui(lpDBRow[icsSourceSync]) : 0);
		// Only add if not present remotely and not created by the current client
		if (ulChange > m_ulChangeId && (ulSourceSync == 0 || ulSourceSync != m_ulSyncId))	
			*lpulChangeType = ICS_MESSAGE_NEW;
	}
	
	*lpulFlags = 0; // Flags are only useful for ICS_FLAG

	if (ulChange > m_ulMaxChangeId)
		m_ulMaxChangeId = ulChange;
		
	ec_log(EC_LOGLEVEL_ICS, "NonLegacyFullAccepted: sourcekey=%s, changetype=%d", bin2hex(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey])).c_str(), *lpulChangeType);
	return erSuccess;
}

ECRESULT NonLegacyFullProcessor::ProcessRejected(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType)
{
	// We assume the client has all messages, so we need to send a delete for any non-matching message.
	assert(lpulChangeType != NULL);

	unsigned int ulChange = (lpDBRow[icsID] ? atoui(lpDBRow[icsID]) : 0);
	if (ulChange <= m_ulChangeId)
		*lpulChangeType = ICS_HARD_DELETE;

	if (ulChange > m_ulMaxChangeId)
		m_ulMaxChangeId = ulChange;

	ec_log(EC_LOGLEVEL_ICS, "NonLegacyFullRejected: sourcekey=%s, changetype=%d", bin2hex(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey])).c_str(), *lpulChangeType);
	return erSuccess;
}

/**
 * LegacyProcessor: Processes accepted and rejected messages while keeping track of legacy messages.
 **/
class LegacyProcessor _kc_final : public IMessageProcessor {
public:
	LegacyProcessor(unsigned int ulChangeId, unsigned int ulSyncId, const MESSAGESET &setMessages, unsigned int ulMaxFolderChange);
	ECRESULT ProcessAccepted(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType, unsigned int *lpulFlags) _kc_override;
	ECRESULT ProcessRejected(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType) _kc_override;
	ECRESULT GetResidualMessages(LPMESSAGESET lpsetResiduals) _kc_override;
	unsigned int GetMaxChangeId(void) const _kc_override { return m_ulMaxChangeId; }
	
private:
	unsigned int	m_ulSyncId;
	MESSAGESET		m_setMessages;
	unsigned int	m_ulMaxFolderChange;
	unsigned int	m_ulMaxChangeId;
};

LegacyProcessor::LegacyProcessor(unsigned int ulChangeId, unsigned int ulSyncId, const MESSAGESET &setMessages, unsigned int ulMaxFolderChange)
	: m_ulSyncId(ulSyncId)
	, m_setMessages(setMessages)
	, m_ulMaxFolderChange(ulMaxFolderChange)
	, m_ulMaxChangeId(ulChangeId)
{ 
	/**
	 * We'll never get an empty set when a restriction was used in the previous run. However it is
	 * possible that the previous run returned an empty set. In that case setMessages contains exactly
	 * one entry with the sourcekey set to 0x00. If that's the case we'll just empty the set and
	 * continue as usual.
	 **/
	if (m_setMessages.size() == 1 && m_setMessages.find(SOURCEKEY(1, "\x00")) != m_setMessages.end())
		m_setMessages.clear();
}

ECRESULT LegacyProcessor::ProcessAccepted(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType, unsigned int *lpulFlags)
{
	unsigned int			ulMsgFlags = 0;

	// When we get here we're accepting a message that has matched the restriction (or if there was no
	// restriction). However since we have legacy, this messages might be present already, in which
	// case we need to do nothing unless its deleted or changed since the last check.
	assert(lpulChangeType != NULL);
	assert(lpDBRow != NULL && lpDBRow[icsSourceKey] != NULL && lpDBRow[icsChangeType] && lpDBRow[icsMsgFlags] != NULL);
	assert(atoui(lpDBRow[icsChangeType]) == ICS_MESSAGE_NEW);
	
	*lpulFlags = 0;
	ulMsgFlags = atoui(lpDBRow[icsMsgFlags]);
	auto iterMessage = m_setMessages.find(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey]));
	if (iterMessage == m_setMessages.cend()) {
		// The message is not synced yet!
		unsigned int ulSourceSync = (lpDBRow[icsSourceSync] ? atoui(lpDBRow[icsSourceSync]) : 0);
		if (ulMsgFlags & MSGFLAG_DELETED || (ulSourceSync != 0 && ulSourceSync == m_ulSyncId))		// Deleted or created by current client
			*lpulChangeType = 0;	// Ignore
		else
			*lpulChangeType = ICS_MESSAGE_NEW;

		ec_log(EC_LOGLEVEL_ICS, "LegacyAccepted: not synced, sourcekey=%s, changetype=%d", bin2hex(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey])).c_str(), *lpulChangeType);
	} else {
		// The message is synced!
		if (ulMsgFlags & MSGFLAG_DELETED)		// Deleted
			*lpulChangeType = ICS_HARD_DELETE;
		else if (iterMessage->second.ulChangeTypes) {		// Modified
			if(iterMessage->second.ulChangeTypes & (ICS_CHANGE_FLAG_NEW | ICS_CHANGE_FLAG_CHANGE))
				*lpulChangeType = ICS_MESSAGE_CHANGE;
			else if(iterMessage->second.ulChangeTypes & ICS_CHANGE_FLAG_FLAG) {
				*lpulChangeType = ICS_MESSAGE_FLAG;
				*lpulFlags = iterMessage->second.ulFlags;
			}
		}
		else
			*lpulChangeType = 0;	// Ignore
		
		ec_log(EC_LOGLEVEL_ICS, "LegacyAccepted: synced, sourcekey=%s , changetype=%d", bin2hex(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey])).c_str(), *lpulChangeType);
		m_setMessages.erase(iterMessage);
	}
	
	if (*lpulChangeType != 0)
		m_ulMaxChangeId = m_ulMaxFolderChange;
	
	return erSuccess;
}

ECRESULT LegacyProcessor::ProcessRejected(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType)
{
	// When we get here we're rejecting a message that has not-matched the restriction. 
	// However since we have legacy, this messages might not be present anyway, in which
	// case we need to do nothing.
	assert(lpulChangeType != NULL);
	assert(lpDBRow != NULL && lpDBRow[icsSourceKey] != NULL && lpDBRow[icsChangeType] && lpDBRow[icsMsgFlags] != NULL);
	assert(atoui(lpDBRow[icsChangeType]) == ICS_MESSAGE_NEW);
	
	auto iterMessage = m_setMessages.find(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey]));
	if (iterMessage == m_setMessages.cend()) {
		// The message is not synced yet!
		*lpulChangeType = 0;	// Ignore
		ec_log(EC_LOGLEVEL_ICS, "LegacyRejected: not synced, sourcekey=%s, changetype=%d", bin2hex(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey])).c_str(), *lpulChangeType);
	} else {
		// The message is synced!
		*lpulChangeType = ICS_HARD_DELETE;
		m_setMessages.erase(iterMessage);
		ec_log(EC_LOGLEVEL_ICS, "LegacyRejected: synced, sourcekey=%s, changetype=%d", bin2hex(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey])).c_str(), *lpulChangeType);
	}

	if (*lpulChangeType != 0)
		m_ulMaxChangeId = m_ulMaxFolderChange;
	
	return erSuccess;
}

ECRESULT LegacyProcessor::GetResidualMessages(LPMESSAGESET lpsetResiduals)
{
	assert(lpsetResiduals != NULL);
	std::copy(m_setMessages.begin(), m_setMessages.end(), std::inserter(*lpsetResiduals, lpsetResiduals->begin()));
	return erSuccess;
}

/**
 * FirstSyncProcessor: Processes accepted and rejected messages for initial syncs. And because
 *                     it is the first sync we assume there are no messages on the device yet.
 **/
class FirstSyncProcessor _kc_final : public IMessageProcessor {
public:
	FirstSyncProcessor(unsigned int ulMaxFolderChange);
	ECRESULT ProcessAccepted(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType, unsigned int *lpulFlags) _kc_override;
	ECRESULT ProcessRejected(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType) _kc_override;
	ECRESULT GetResidualMessages(LPMESSAGESET lpsetResiduals) _kc_override
	{
		/* No legacy, no residuals. */
		return erSuccess;
	}
	unsigned int GetMaxChangeId(void) const _kc_override { return m_ulMaxFolderChange; }
	
private:
	unsigned int m_ulMaxFolderChange;
};

FirstSyncProcessor::FirstSyncProcessor(unsigned int ulMaxFolderChange)
	: m_ulMaxFolderChange(ulMaxFolderChange)
{ }

ECRESULT FirstSyncProcessor::ProcessAccepted(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType, unsigned int *lpulFlags)
{
	// This processor will always be used with the FullQueryGenerator, which means that the provided
	// changetype is always ICS_MESSAGE_NEW. However, we do have the message flags so we can see if
	// a message is deleted.
	assert(lpulChangeType != NULL);
	assert(lpDBRow != NULL && lpDBRow[icsChangeType] != NULL && lpDBRow[icsMsgFlags] != NULL);
	assert(atoui(lpDBRow[icsChangeType]) == ICS_MESSAGE_NEW);
	
	*lpulFlags = 0; // Only useful for ICS_FLAG type changes
	if (atoui(lpDBRow[icsMsgFlags]) & MSGFLAG_DELETED)
		*lpulChangeType = 0;	// Ignore
	else
		*lpulChangeType = ICS_MESSAGE_NEW;
		
	ec_log(EC_LOGLEVEL_ICS, "FirstSyncAccepted: sourcekey=%s, changetype=%d", bin2hex(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey])).c_str(), *lpulChangeType);
	return erSuccess;
}

ECRESULT FirstSyncProcessor::ProcessRejected(DB_ROW lpDBRow, DB_LENGTHS lpDBLen, unsigned int *lpulChangeType)
{
	assert(lpulChangeType != NULL);
	*lpulChangeType = 0;	// Ignore

	ec_log(EC_LOGLEVEL_ICS, "FirstSyncRejected: sourcekey=%s, changetype=0", bin2hex(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey])).c_str());
	return erSuccess;
}

/**
 * ECGetContentChangesHelper definitions
 **/
ECRESULT ECGetContentChangesHelper::Create(struct soap *soap, ECSession *lpSession, ECDatabase *lpDatabase, const SOURCEKEY &sFolderSourceKey, unsigned int ulSyncId, unsigned int ulChangeId, unsigned int ulFlags, struct restrictTable *lpsRestrict, ECGetContentChangesHelper **lppHelper)
{
	ECRESULT					er = erSuccess;
	std::unique_ptr<ECGetContentChangesHelper> lpHelper(
		new ECGetContentChangesHelper(soap, lpSession, lpDatabase,
		sFolderSourceKey, ulSyncId, ulChangeId, ulFlags, lpsRestrict));
	er = lpHelper->Init();
	if (er != erSuccess)
		return er;
	assert(lppHelper != NULL);
	*lppHelper = lpHelper.release();
	return erSuccess;
}

ECGetContentChangesHelper::ECGetContentChangesHelper(struct soap *soap,
    ECSession *lpSession, ECDatabase *lpDatabase,
    const SOURCEKEY &sFolderSourceKey, unsigned int ulSyncId,
    unsigned int ulChangeId, unsigned int ulFlags,
    struct restrictTable *lpsRestrict) :
	m_soap(soap), m_lpSession(lpSession), m_lpDatabase(lpDatabase),
	m_lpsRestrict(lpsRestrict), m_sFolderSourceKey(sFolderSourceKey),
	m_ulSyncId(ulSyncId), m_ulChangeId(ulChangeId), m_ulFlags(ulFlags)
{ }

ECRESULT ECGetContentChangesHelper::Init()
{
	ECRESULT	er = erSuccess;
	DB_RESULT lpDBResult;
	DB_ROW		lpDBRow;
	std::string	strQuery;

	assert(m_lpDatabase != NULL);
	if (m_sFolderSourceKey.empty() && m_ulChangeId == 0 &&
	    !(m_ulFlags & SYNC_CATCHUP)) {
		// Disallow full initial exports on server level since they are insanely large
		er = KCERR_NO_SUPPORT;
		goto exit;
	}

	strQuery = "SELECT MAX(id) FROM changes";
	if(!m_sFolderSourceKey.empty())
		strQuery += " WHERE parentsourcekey=" + m_lpDatabase->EscapeBinary(m_sFolderSourceKey, m_sFolderSourceKey.size());
		
	er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		goto exit;
		
	if ((lpDBRow = m_lpDatabase->FetchRow(lpDBResult)) == NULL || lpDBRow == NULL) {
		er = KCERR_DATABASE_ERROR;
		ec_log_err("ECGetContentChangesHelper::Init(): fetchrow failed");
		goto exit;
	}
	
	if (lpDBRow[0])
		m_ulMaxFolderChange = atoui(lpDBRow[0]);
	
	m_lpDatabase->FreeResult(lpDBResult);
	lpDBResult = NULL;

	// Here we setup the classes to delegate specific work to	
	if (m_ulChangeId == 0) {
		/*
		 * Initial sync
		 * We want all message that were not created by the current client (m_ulSyncId).
		 */
	    if(m_sFolderSourceKey.empty()) {
			// Optimization: when doing SYNC_CATCHUP on a non-filtered sync, we can skip looking for any changes
			assert(m_ulFlags & SYNC_CATCHUP);
			m_lpQueryCreator = new NullQueryCreator();
		} else {
			m_lpQueryCreator = new FullQueryCreator(m_lpDatabase, m_sFolderSourceKey, m_ulFlags, m_ulSyncId);
		}
		m_lpMsgProcessor = new FirstSyncProcessor(m_ulMaxFolderChange);
	} else {
		/*
		 * Incremental sync
		 * We first need to determine if the previous sync was with or without
		 * restriction and if a restriction is requested now.
		 */
		er = GetSyncedMessages(m_ulSyncId, m_ulChangeId, &m_setLegacyMessages);
		if (er != erSuccess)
			goto exit;
			
		if (m_setLegacyMessages.empty()) {
			/*
			 * Previous request was without restriction.
			 */
			if (m_lpsRestrict == NULL) {
				/*
				 * This request is also without a restriction. We can use an
				 * incremental query.
				 */
				m_lpQueryCreator = new IncrementalQueryCreator(m_lpDatabase, m_ulSyncId, m_ulChangeId, m_sFolderSourceKey, m_ulFlags);
				m_lpMsgProcessor = new NonLegacyIncrementalProcessor(m_ulMaxFolderChange);
			} else {
				/*
				 * This request is WITH a restriction. This means the client
				 * switched from using no restriction to using a restriction.
				 * Note: In practice this won't happen very often.
				 * We need to perform a full query to be able te decide which
				 * messages match the restriction and which don't.
				 * Since the previous request was without a restriction, we
				 * assume all messages that were present during the last sync
				 * are on the device.
				 * We do want to filter all messages that were created since
				 * the last sync and were created by the current client. The
				 * processor should do that because that's too complex for the
				 * query creator to do.
				 */
				m_lpQueryCreator = new FullQueryCreator(m_lpDatabase, m_sFolderSourceKey, m_ulFlags);
				m_lpMsgProcessor = new NonLegacyFullProcessor(m_ulChangeId, m_ulSyncId);
			}
		} else {
			/*
			 * The previous request was with a restriction, so we can't do an
			 * incremental sync in any case, as that will only get us add's and
			 * deletes for changes that happened after the last sync. But we
			 * can also have adds because certain older messages might not have
			 * matched the previous restriction, but do match the current (where
			 * no restriction is seen as a match-all restriction).
			 * We do want to filter all messages that were created since
			 * the last sync and were created by the current client. The
			 * processor should do that because that's too complex for the
			 * query creator to do.
			 */
			m_lpQueryCreator = new FullQueryCreator(m_lpDatabase, m_sFolderSourceKey, m_ulFlags);
			m_lpMsgProcessor = new LegacyProcessor(m_ulChangeId, m_ulSyncId, m_setLegacyMessages, m_ulMaxFolderChange);
		}
	}
		
exit:
	if (lpDBResult)
		m_lpDatabase->FreeResult(lpDBResult);

	return er;
}
 
ECGetContentChangesHelper::~ECGetContentChangesHelper()
{
	delete m_lpQueryCreator;
	delete m_lpMsgProcessor;
}
	
ECRESULT ECGetContentChangesHelper::QueryDatabase(DB_RESULT *lppDBResult)
{
	ECRESULT er;
	DB_RESULT lpDBResult;
	std::string		strQuery;
	unsigned int	ulChanges = 0;

	assert(m_lpQueryCreator != NULL);
	strQuery = m_lpQueryCreator->CreateQuery();
	
	if(!strQuery.empty()) {
		assert(m_lpDatabase != NULL);
		er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
		if (er != erSuccess)
			return er;
	} else {
		lpDBResult = NULL;
		ulChanges = 0;
	}
		
	if(lpDBResult)
		ulChanges = m_lpDatabase->GetNumRows(lpDBResult) + m_setLegacyMessages.size();
	else
		ulChanges = 0;
		
	m_lpChanges = (icsChangesArray*)soap_malloc(m_soap, sizeof *m_lpChanges);
	m_lpChanges->__ptr = (icsChange*)soap_malloc(m_soap, sizeof *m_lpChanges->__ptr * ulChanges);
	m_lpChanges->__size = 0;
	assert(lppDBResult != NULL);
	*lppDBResult = std::move(lpDBResult);
	return erSuccess;
}

ECRESULT ECGetContentChangesHelper::ProcessRows(const std::vector<DB_ROW> &db_rows, const std::vector<DB_LENGTHS> &db_lengths)
{
	ECRESULT		er = erSuccess;
	unsigned int	ulChangeType = 0;
	unsigned int	ulFlags = 0;
	DB_ROW lpDBRow;
	DB_LENGTHS lpDBLen;
	std::set<SOURCEKEY> matches;

	if (m_lpsRestrict) {
		assert(m_lpSession != NULL);
		er = MatchRestrictions(db_rows, db_lengths, m_lpsRestrict, &matches);
		if (er != erSuccess)
			return er;
	}

	assert(m_lpMsgProcessor != NULL);
	for (size_t i = 0; i < db_rows.size(); ++i) {
		bool fMatch = true;

		lpDBRow = db_rows[i];
		lpDBLen = db_lengths[i];

		if (m_lpsRestrict != NULL)
			fMatch = matches.find(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey])) != matches.end();

		ec_log(EC_LOGLEVEL_ICS, "Processing: %s, match=%d", bin2hex(SOURCEKEY(lpDBLen[icsSourceKey], lpDBRow[icsSourceKey])).c_str(), fMatch);
		ulChangeType = 0;
		ulFlags = 0;
		if (fMatch) {
			er = m_lpMsgProcessor->ProcessAccepted(lpDBRow, lpDBLen, &ulChangeType, &ulFlags);
			if (m_lpsRestrict != NULL)
				m_setNewMessages.insert(MESSAGESET::value_type(SOURCEKEY(lpDBLen[icsSourceKey],
					lpDBRow[icsSourceKey]), SAuxMessageData(SOURCEKEY(lpDBLen[icsParentSourceKey],
					lpDBRow[icsParentSourceKey]), ICS_CHANGE_FLAG_NEW, ulFlags)));
		} else {
			er = m_lpMsgProcessor->ProcessRejected(lpDBRow, lpDBLen, &ulChangeType);
		}
		if (er != erSuccess)
			return er;

		// If ulChangeType equals 0 we can skip this message
		if (ulChangeType == 0)
			continue;

		m_lpChanges->__ptr[m_ulChangeCnt].ulChangeId = lpDBRow[icsID] ? atoui(lpDBRow[icsID]) : 0;

		m_lpChanges->__ptr[m_ulChangeCnt].sSourceKey.__ptr = (unsigned char *)soap_malloc(m_soap, lpDBLen[icsSourceKey]);
		m_lpChanges->__ptr[m_ulChangeCnt].sSourceKey.__size = lpDBLen[icsSourceKey];
		memcpy(m_lpChanges->__ptr[m_ulChangeCnt].sSourceKey.__ptr, lpDBRow[icsSourceKey], lpDBLen[icsSourceKey]);

		m_lpChanges->__ptr[m_ulChangeCnt].sParentSourceKey.__ptr = (unsigned char *)soap_malloc(m_soap, lpDBLen[icsParentSourceKey]);
		m_lpChanges->__ptr[m_ulChangeCnt].sParentSourceKey.__size = lpDBLen[icsParentSourceKey];
		memcpy(m_lpChanges->__ptr[m_ulChangeCnt].sParentSourceKey.__ptr, lpDBRow[icsParentSourceKey], lpDBLen[icsParentSourceKey]);

		m_lpChanges->__ptr[m_ulChangeCnt].ulChangeType = ulChangeType;

		m_lpChanges->__ptr[m_ulChangeCnt].ulFlags = ulFlags;
		++m_ulChangeCnt;
	}
	return erSuccess;
}

ECRESULT ECGetContentChangesHelper::ProcessResidualMessages()
{
	ECRESULT er;
	MESSAGESET				setResiduals;

	assert(m_lpMsgProcessor != NULL);
	er = m_lpMsgProcessor->GetResidualMessages(&setResiduals);
	if (er != erSuccess)
		return er;
	
	for (const auto &p : setResiduals) {
		if (p.first.size() == 1 && memcmp(p.first, "\0", 1) == 0)
			continue;	// Skip empty restricted set marker,
	
		ec_log(EC_LOGLEVEL_ICS, "ProcessResidualMessages: sourcekey=%s", bin2hex(p.first).c_str());
		m_lpChanges->__ptr[m_ulChangeCnt].ulChangeId = 0;
		m_lpChanges->__ptr[m_ulChangeCnt].sSourceKey.__ptr = (unsigned char *)soap_malloc(m_soap, p.first.size());
		m_lpChanges->__ptr[m_ulChangeCnt].sSourceKey.__size = p.first.size();
		memcpy(m_lpChanges->__ptr[m_ulChangeCnt].sSourceKey.__ptr, p.first, p.first.size());

		m_lpChanges->__ptr[m_ulChangeCnt].sParentSourceKey.__ptr = (unsigned char *)soap_malloc(m_soap, p.second.sParentSourceKey.size());
		m_lpChanges->__ptr[m_ulChangeCnt].sParentSourceKey.__size = p.second.sParentSourceKey.size();
		memcpy(m_lpChanges->__ptr[m_ulChangeCnt].sParentSourceKey.__ptr, p.second.sParentSourceKey, p.second.sParentSourceKey.size());
		
		m_lpChanges->__ptr[m_ulChangeCnt].ulChangeType = ICS_HARD_DELETE;
		
		m_lpChanges->__ptr[m_ulChangeCnt].ulFlags = 0;
		++m_ulChangeCnt;
	}	
	return erSuccess;
}

ECRESULT ECGetContentChangesHelper::Finalize(unsigned int *lpulMaxChange, icsChangesArray **lppChanges)
{
	ECRESULT					er = erSuccess;
	std::string					strQuery;
	unsigned int				ulMaxChange = 0;
	unsigned int				ulNewChange = 0;
	DB_RESULT lpDBResult;
	DB_ROW						lpDBRow;
	
	assert(lppChanges != NULL);
	assert(lpulMaxChange != NULL);
	m_lpChanges->__size = m_ulChangeCnt;
	*lppChanges = m_lpChanges;
	assert(m_lpMsgProcessor != NULL);
	ulMaxChange = m_lpMsgProcessor->GetMaxChangeId();

	if (m_ulFlags & SYNC_NO_DB_CHANGES) {
		*lpulMaxChange = ulMaxChange;
		goto exit;
	}
	
	// If there were no changes and this was not the initial sync, we only need to purge all too-new-syncedmessages.
	// If this is the initial sync, we might need to write the empty restricted set marker, so we can't
	// stop doing work here. Also, if we have converted from a non-restricted to a restricted set, we have to write
	// the new set of messages, even if there are no changes.
	if (m_ulChangeCnt == 0 && m_ulChangeId > 0 && !(m_setLegacyMessages.empty() && m_lpsRestrict) ) {
		assert(ulMaxChange >= m_ulChangeId);
		*lpulMaxChange = ulMaxChange;
		
		// Delete all entries that have a changeid that are greater to the new change id.
		strQuery = "DELETE FROM syncedmessages WHERE sync_id=" + stringify(m_ulSyncId) + " AND change_id>" + stringify(ulMaxChange);
		er = m_lpDatabase->DoDelete(strQuery);
		goto exit;
	}
	
	if (ulMaxChange == m_ulChangeId) {
		/**
		 * If we get here, we had at least one change but the max changeid for the server is the
		 * same as the changeid in the request. This means the change was caused by either a modified
		 * restriction.
		 * When this happens a new changeid must be generated in order to return a unique state to the
		 * client that can be used in subsequent requests. We do this by creating a dummy change in the
		 * changes table.
		 */
		// Bump the changeid
		strQuery = "REPLACE INTO changes (sourcekey,parentsourcekey,sourcesync) VALUES (0, " + m_lpDatabase->EscapeBinary(m_sFolderSourceKey, m_sFolderSourceKey.size()) + "," + stringify(m_ulSyncId) + ")";
		er = m_lpDatabase->DoInsert(strQuery, &ulNewChange);
		if (er != erSuccess)
			goto exit;
		assert(ulNewChange > ulMaxChange);
		ulMaxChange = ulNewChange;
		assert(ulMaxChange > m_ulChangeId);
	}
	
	/**
	 * If a restriction is set, but the set of synced messages is empty we'll make a placeholder entry
	 * so we can differentiate between having all messages and having no messages on the client.
	 *
	 * It's actually backwards to put in a placeholder when we have no message and put in nothing when
	 * we have all messages, but having all message (because no restriction was set) never stores anything
	 * in the syncedmessages table, so this scheme is compatible. On top of that, having no messages synced
	 * at all is rare, having all messages isn't.
	 **/
	if (m_lpsRestrict && m_setNewMessages.empty())
		m_setNewMessages.insert(MESSAGESET::value_type(SOURCEKEY(1, "\x00"), SAuxMessageData(m_sFolderSourceKey, 0, 0)));

	if (!m_setNewMessages.empty()) {
		std::set<unsigned int> setChangeIds;
		
		strQuery = "SELECT DISTINCT change_id FROM syncedmessages WHERE sync_id=" + stringify(m_ulSyncId);
		er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
		if (er != erSuccess)
			goto exit;

		while ((lpDBRow = m_lpDatabase->FetchRow(lpDBResult))) {
			if (lpDBRow == NULL || lpDBRow[0] == NULL) {
				er = KCERR_DATABASE_ERROR; // this should never happen
				ec_log_err("ECGetContentChangesHelper::Finalize(): row null or column null");
				goto exit;
			}
			setChangeIds.insert(atoui(lpDBRow[0]));
		}

		m_lpDatabase->FreeResult(lpDBResult);
		lpDBResult = NULL;

		if (!setChangeIds.empty()) {
			std::set<unsigned int> setDeleteIds;
			
			/* Remove obsolete states
			 *
			 * rules:
			 * 1) Remove any states that are newer than the state that was requested
			 *    We do this since if the client requests state X, it can never request state X+1
			 *    later unless X+1 is the state that was generated from this request. We can therefore
			 *    remove any state > X at this point, since state X+1 will be inserted later
			 * 2) Remove any states that are older than the state that was requested minus nine
			 *    We cannot remove state X since the client may re-request this state (eg if the export
			 *    failed due to network error, or if the export is interrupted before ending). We also
			 *    do not remove state X-9 to X-1 so that we support some sort of rollback of the client.
			 *    This may happen if the client is restored to an old state. In practice removing X-9 to
			 *    X-1 will probably not cause any real problems though, and the number 9 is pretty
			 *    arbitrary.
			 */

			// Delete any message state that is higher than the changeset that changes were
			// requested from (rule 1)
			auto iter = setChangeIds.upper_bound(m_ulChangeId);
			if (iter != setChangeIds.cend())
				std::copy(iter, setChangeIds.end(), std::inserter(setDeleteIds, setDeleteIds.begin()));

			// Find all message states that are equal or lower than the changeset that changes were requested from
			iter = setChangeIds.lower_bound(m_ulChangeId);
			// Reverse up to nine message states (less if they do not exist)
			for (int i = 0; iter != setChangeIds.begin() && i < 9; ++i, --iter);
			// Remove message states that are older than X-9 (rule 2)
			std::copy(setChangeIds.begin(), iter, std::inserter(setDeleteIds, setDeleteIds.begin()));

			if (!setDeleteIds.empty()) {
				assert(setChangeIds.size() - setDeleteIds.size() <= 9);
				strQuery = "DELETE FROM syncedmessages WHERE sync_id=" + stringify(m_ulSyncId) + " AND change_id IN (";
				for (auto del_id : setDeleteIds) {
					strQuery.append(stringify(del_id));
					strQuery.append(1, ',');
				}
				strQuery.resize(strQuery.size() - 1);	// Remove trailing ','
				strQuery.append(1, ')');

				er = m_lpDatabase->DoDelete(strQuery);
				if (er != erSuccess)
					goto exit;
			}
		}
	
		// Create the insert query
		strQuery = "INSERT INTO syncedmessages (sync_id,change_id,sourcekey,parentsourcekey) VALUES ";
		for (const auto &p : m_setNewMessages)
			strQuery += "(" + stringify(m_ulSyncId) + "," + stringify(ulMaxChange) + "," +
				m_lpDatabase->EscapeBinary(p.first, p.first.size()) + "," +
				m_lpDatabase->EscapeBinary(p.second.sParentSourceKey, p.second.sParentSourceKey.size()) + "),";

		strQuery.resize(strQuery.size() - 1);
		er = m_lpDatabase->DoInsert(strQuery);
		if (er != erSuccess)
			goto exit;
	}
	
	*lpulMaxChange = ulMaxChange;

exit:
	if (lpDBResult)
		m_lpDatabase->FreeResult(lpDBResult);
		
	return er;
}

ECRESULT ECGetContentChangesHelper::MatchRestrictions(const std::vector<DB_ROW> &db_rows,
    const std::vector<DB_LENGTHS> &db_lengths,
    struct restrictTable *restrict, std::set<SOURCEKEY> *matches_p)
{
	ECRESULT er = erSuccess;
	unsigned int ulObjId = 0;
	ECObjectTableList lstRows;
	ECObjectTableList::value_type sRow;
	ECODStore sODStore;
	bool fMatch = false;
	std::vector<SOURCEKEY> source_keys;
	std::map<ECsIndexProp, unsigned int> index_objs;
	struct propTagArray *lpPropTags = NULL;
	struct rowSet *lpRowSet = NULL;
	std::set<SOURCEKEY> matches;
	std::vector<unsigned int> cbdata;
	std::vector<unsigned char *> lpdata;
	std::vector<unsigned int> objectids;

	memset(&sODStore, 0, sizeof(sODStore));

	ec_log(EC_LOGLEVEL_ICS, "MatchRestrictions: matching %zu rows", db_rows.size());

	for (size_t i = 0; i < db_rows.size(); ++i) {
		lpdata.push_back(reinterpret_cast<unsigned char *>(db_rows[i][icsSourceKey]));
		cbdata.push_back(db_lengths[i][icsSourceKey]);
	}

	er = g_lpSessionManager->GetCacheManager()->GetObjectsFromProp(PROP_ID(PR_SOURCE_KEY), cbdata, lpdata, index_objs);
	if (er != erSuccess)
		goto exit;

	for (const auto &i : index_objs) {
		sRow.ulObjId = i.second;
		sRow.ulOrderId = 0;
		lstRows.push_back(sRow);
		source_keys.push_back(SOURCEKEY(i.first.cbData, reinterpret_cast<const char *>(i.first.lpData)));
		ulObjId = i.second; /* no need to split QueryRowData call per-objtype (always same) */
	}

	er = g_lpSessionManager->GetCacheManager()->GetObject(ulObjId, NULL, NULL, NULL, &sODStore.ulObjType);
	if (er != erSuccess)
		goto exit;

	er = ECGenericObjectTable::GetRestrictPropTags(restrict, NULL, &lpPropTags);
	if (er != erSuccess)
		goto exit;

	sODStore.lpGuid = new GUID;

	er = g_lpSessionManager->GetCacheManager()->GetStore(ulObjId, &sODStore.ulStoreId, sODStore.lpGuid);
	if (er != erSuccess)
		goto exit;

	assert(m_lpSession != NULL);
	// NULL for soap, not m_soap. We'll free this ourselves
	er = ECStoreObjectTable::QueryRowData(NULL, NULL, m_lpSession, &lstRows, lpPropTags, &sODStore, &lpRowSet, false, false);
	if (er != erSuccess)
		goto exit;

	if (lpRowSet->__size < 0 ||
	    static_cast<size_t>(lpRowSet->__size) != lstRows.size()) {
		er = KCERR_DATABASE_ERROR;
		ec_log_err("ECGetContentChangesHelper::MatchRestriction(): unexpected row count");
		goto exit;
	}

	for (gsoap_size_t j = 0; j < lpRowSet->__size; ++j) {
		// @todo: Get a proper locale for the case insensitive comparisons inside MatchRowRestrict
		er = ECGenericObjectTable::MatchRowRestrict(g_lpSessionManager->GetCacheManager(), &lpRowSet->__ptr[j], restrict, NULL, createLocaleFromName(""), &fMatch);
		if(er != erSuccess)
			goto exit;
		if (fMatch)
			matches.insert(source_keys[j]);
	}

	ec_log(EC_LOGLEVEL_ICS, "MatchRestrictions: %zu match(es) out of %d rows (%d properties)",
		matches.size(), lpRowSet->__size, lpPropTags->__size);
	*matches_p = std::move(matches);
exit:
	delete sODStore.lpGuid;

	if(lpPropTags)
		FreePropTagArray(lpPropTags);
	if(lpRowSet)
		FreeRowSet(lpRowSet, true);
	return er;
}

ECRESULT ECGetContentChangesHelper::GetSyncedMessages(unsigned int ulSyncId, unsigned int ulChangeId, LPMESSAGESET lpsetMessages)
{
	ECRESULT		er = erSuccess;
	std::string		strSubQuery;
	std::string		strQuery;
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow;
	DB_LENGTHS		lpDBLen;
	
	strQuery = 
		"SELECT m.sourcekey, m.parentsourcekey, c.change_type, c.flags "
		"FROM syncedmessages as m "
			"LEFT JOIN changes as c "
				"ON m.sourcekey=c.sourcekey AND m.parentsourcekey=c.parentsourcekey AND c.id > " + stringify(ulChangeId) + " AND c.sourcesync != " + stringify(ulSyncId) + " "
		"WHERE sync_id=" + stringify(ulSyncId) + " AND change_id=" + stringify(ulChangeId);
	assert(m_lpDatabase != NULL);
	er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		goto exit;
		
	while ((lpDBRow = m_lpDatabase->FetchRow(lpDBResult))) {
		lpDBLen = m_lpDatabase->FetchRowLengths(lpDBResult);
		if (lpDBRow == NULL || lpDBLen == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL) {
			er = KCERR_DATABASE_ERROR; // this should never happen
			ec_log_err("ECGetContentChangesHelper::GetSyncedMessages(): row or columns null");
			goto exit;
		}

		auto iResult = lpsetMessages->insert(MESSAGESET::value_type(SOURCEKEY(lpDBLen[0], lpDBRow[0]), SAuxMessageData(SOURCEKEY(lpDBLen[1], lpDBRow[1]), 1 << (lpDBRow[2]?atoui(lpDBRow[2]):0), lpDBRow[3]?atoui(lpDBRow[3]):0)));
		if (iResult.second == false && lpDBRow[2] != nullptr)
			iResult.first->second.ulChangeTypes |= 1 << (lpDBRow[2]?atoui(lpDBRow[2]):0);
	}
	
exit:
	if (lpDBResult)
		m_lpDatabase->FreeResult(lpDBResult);

	return er;
}

bool ECGetContentChangesHelper::CompareMessageEntry(const MESSAGESET::value_type &lhs, const MESSAGESET::value_type &rhs)
{
	return lhs.first == rhs.first;
}

bool ECGetContentChangesHelper::MessageSetsDiffer() const
{
	if (m_setLegacyMessages.size() != m_setNewMessages.size())
		return true;
	
	return !std::equal(m_setLegacyMessages.begin(), m_setLegacyMessages.end(), m_setNewMessages.begin(), &CompareMessageEntry);
}

} /* namespaces */
