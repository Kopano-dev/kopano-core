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

#ifndef KC_ICS_CLIENT_HPP
#define KC_ICS_CLIENT_HPP 1

#include <string>
#include <list>
#include <mapidefs.h>	// SBinary

typedef ULONG syncid_t;
typedef ULONG changeid_t;
typedef ULONG connection_t;

// Client-side type definitions for ICS
struct ICSCHANGE {
	SBinary sSourceKey, sParentSourceKey, sMovedFromSourceKey;
	unsigned int ulChangeId, ulChangeType, ulFlags;
};

/**
 * SSyncState: This structure uniquely defines a sync state.
 */
struct SSyncState {
	syncid_t	ulSyncId;		//!< The sync id uniquely specifies a folder in a syncronization context.
	changeid_t	ulChangeId;		//!< The change id specifies the syncronization state for a specific sync id.
};

/**
 * SSyncAdvise: This structure combines a sync state with a notification connection.
 */
struct SSyncAdvise {
	SSyncState		sSyncState;	//!< The sync state that's for which a change notifications have been registered.
	connection_t	ulConnection;		//!< The connection on which notifications for the folder specified by the sync state are received.
};

/**
 * Extract the sync id from binary data that is known to be a valid sync state.
 */
#define SYNCID(lpb) (((SSyncState*)(lpb))->ulSyncId)

/**
 * Extract the change id from binary data that is known to be a valid sync state.
 */
#define CHANGEID(lpb) (((SSyncState*)(lpb))->ulChangeId)

typedef std::list<syncid_t> ECLISTSYNCID;
typedef std::list<SSyncState> ECLISTSYNCSTATE;
typedef std::list<SSyncAdvise> ECLISTSYNCADVISE;

#endif /* KC_ICS_CLIENT_HPP */
