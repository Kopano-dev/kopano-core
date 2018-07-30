/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECICSHELPERS_H
#define ECICSHELPERS_H

#include <kopano/zcdefs.h>
#include "ECICS.h"
#include "ECDatabase.h"

struct soap;

namespace KC {

// Indexes into the database rows.
enum {
	icsID				= 0,
	icsSourceKey		= 1,
	icsParentSourceKey	= 2,
	icsChangeType		= 3,
	icsFlags			= 4,
	icsMsgFlags			= 5,
	icsSourceSync		= 6
};


// Auxiliary Message Data (ParentSourceKey, Last ChangeId)
struct SAuxMessageData {
	SAuxMessageData(const SOURCEKEY &ps, unsigned int ct, unsigned int flags): sParentSourceKey(ps), ulChangeTypes(ct), ulFlags(flags) {}
	SOURCEKEY		sParentSourceKey;
	unsigned int	ulChangeTypes;
	unsigned int	ulFlags; // For readstate change
};
typedef std::map<SOURCEKEY,SAuxMessageData>	MESSAGESET, *LPMESSAGESET;

// Forward declarations of interfaces used by ECGetContentChangesHelper
class IDbQueryCreator;
class IMessageProcessor;

class ECGetContentChangesHelper _kc_final {
public:
	static ECRESULT Create(struct soap *, ECSession *, ECDatabase *, const SOURCEKEY &folder, unsigned int sync_id, unsigned int change_id, unsigned int flags, const struct restrictTable *, ECGetContentChangesHelper **);
	~ECGetContentChangesHelper();
	ECRESULT QueryDatabase(DB_RESULT *lppDBResult);
	ECRESULT ProcessRows(const std::vector<DB_ROW> &db_rows, const std::vector<DB_LENGTHS> &db_lengths);
	ECRESULT ProcessResidualMessages();
	ECRESULT Finalize(unsigned int *lpulMaxChange, icsChangesArray **lppChanges);
	
private:
	ECGetContentChangesHelper(struct soap *, ECSession *, ECDatabase *, const SOURCEKEY &folder, unsigned int sync_id, unsigned int change_id, unsigned int flags, const struct restrictTable *);
	ECRESULT Init();
	ECRESULT MatchRestrictions(const std::vector<DB_ROW> &db_rows, const std::vector<DB_LENGTHS> &db_lengths, const struct restrictTable *lpsRestrict, std::set<SOURCEKEY> *matches);
	ECRESULT GetSyncedMessages(unsigned int ulSyncId, unsigned int ulChangeId, LPMESSAGESET lpsetMessages);
	static bool CompareMessageEntry(const MESSAGESET::value_type &lhs, const MESSAGESET::value_type &rhs);
	bool MessageSetsDiffer() const;
	
	// Interfaces for delegated processing
	IDbQueryCreator *m_lpQueryCreator = nullptr;
	IMessageProcessor *m_lpMsgProcessor = nullptr;
	
	// Internal variables
	soap			*m_soap;
	ECSession		*m_lpSession;
	ECDatabase		*m_lpDatabase;
	const struct restrictTable *m_lpsRestrict;
	icsChangesArray *m_lpChanges = nullptr;
	const SOURCEKEY	&m_sFolderSourceKey;
	unsigned int m_ulSyncId, m_ulChangeId, m_ulFlags;
	unsigned int m_ulChangeCnt = 0, m_ulMaxFolderChange = 0;
	MESSAGESET m_setLegacyMessages, m_setNewMessages;
};

} /* namespace */

#endif // ndef ECICSHELPERS_H
