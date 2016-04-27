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

#ifndef ECICS_H
#define ECICS_H

#include <kopano/zcdefs.h>
#include "ECSession.h"

#include <set>

// This class is used to pass SOURCEKEYs internally between parts of the server backend. You can use it as a char* to get the data, use size() to get the size,
// and have various ways of creating new SOURCEKEYs, including using a GUID and an ID, which is used for kopano-generated source keys.

class SOURCEKEY _zcp_final {
public:
    SOURCEKEY(void) : lpData(NULL), ulSize(0) {}
    SOURCEKEY(const SOURCEKEY &s) { 
        if(&s == this) return; 
        if(s.ulSize == 0) { 
            ulSize = 0; 
            lpData = NULL;
        } else { 
            lpData = new char[s.ulSize]; 
            memcpy(lpData, s.lpData, s.ulSize); 
            ulSize = s.ulSize;
        } 
    }
    SOURCEKEY(unsigned int ulSize, const char *lpData) { 
		if (lpData) {
			this->lpData = new char[ulSize];
			memcpy(this->lpData, lpData, ulSize);
		} else {
			this->lpData = NULL;
		}
		this->ulSize = ulSize;
    }
    SOURCEKEY(GUID guid, unsigned long long ullId) { 
        // Use 22-byte sourcekeys (16 bytes GUID + 6 bytes counter)
        ulSize = sizeof(GUID) + 6;
        lpData = new char [ulSize]; 
        memcpy(lpData, &guid, sizeof(guid));
        memcpy(lpData+sizeof(GUID), &ullId, ulSize - sizeof(GUID)); 
    }
    SOURCEKEY(struct xsd__base64Binary &sourcekey) {
        this->lpData = new char[sourcekey.__size];
        memcpy(this->lpData, sourcekey.__ptr, sourcekey.__size);
        this->ulSize = sourcekey.__size;
    }
    ~SOURCEKEY() { 
	delete[] lpData; 
    }
    
    SOURCEKEY&  operator= (const SOURCEKEY &s) { 
        if(&s == this) return *this; 
        delete[] lpData; 
        lpData = new char[s.ulSize]; 
        memcpy(lpData, s.lpData, s.ulSize); 
        ulSize = s.ulSize; 
        return *this; 
    }
    
    bool operator == (const SOURCEKEY &s) const {
        if(this == &s)
            return true;
        if(ulSize != s.ulSize)
            return false;
        return memcmp(lpData, s.lpData, s.ulSize) == 0;
    }
	
	bool operator < (const SOURCEKEY &s) const {
		if(this == &s)
			return false;
		if(ulSize == s.ulSize)
			return memcmp(lpData, s.lpData, ulSize) < 0;
		else if(ulSize > s.ulSize) {
			int d = memcmp(lpData, s.lpData, s.ulSize);
			return (d == 0) ? false : (d < 0);			// If the compared part is equal, the shortes is less (s)
		} else {
			int d = memcmp(lpData, s.lpData, ulSize);
			return (d == 0) ? true : (d < 0);			// If the compared part is equal, the shortes is less (this)
		}
	}
    
    operator unsigned char *() const { return (unsigned char *)lpData; }
    
    operator const std::string () const { return std::string(lpData, ulSize); }
    
    unsigned int 	size() const { return ulSize; }
	bool			empty() const { return ulSize == 0; } 
private:
    char *lpData;
    unsigned int ulSize;
};

ECRESULT AddChange(BTSession *lpecSession, unsigned int ulSyncId, const SOURCEKEY &sSourceKey, const SOURCEKEY &sParentSourceKey, unsigned int ulChange, unsigned int ulFlags = 0, bool fForceNewChangeKey = false, std::string *lpstrChangeKey = NULL, std::string *lpstrChangeList = NULL);
ECRESULT AddABChange(BTSession *lpecSession, unsigned int ulChange, SOURCEKEY sSourceKey, SOURCEKEY sParentSourceKey);
ECRESULT GetChanges(struct soap *soap, ECSession *lpSession, SOURCEKEY sSourceKeyFolder, unsigned int ulSyncId, unsigned int ulChangeId, unsigned int ulChangeType, unsigned int ulFlags, struct restrictTable *lpsRestrict, unsigned int *lpulMaxChangeId, icsChangesArray **lppChanges);
ECRESULT GetSyncStates(struct soap *soap, ECSession *lpSession, mv_long ulaSyncId, syncStateArray *lpsaSyncState);
void* CleanupSyncsTable(void* lpTmpMain);
void* CleanupChangesTable(void* lpTmpMain);
void *CleanupSyncedMessagesTable(void *lpTmpMain);

/**
 * Adds the message specified by sSourceKey to the last set of syncedmessages for the syncer identified by
 * ulSyncId. This causes GetChanges to know that the message is available on the client so it doesn't need
 * to send a add to the client.
 *
 * @param[in]	lpDatabase
 *					Pointer to the database.
 * @param[in]	ulSyncId
 *					The sync id of the client for whom the message is to be registered.
 * @param[in]	sSourceKey
 *					The source key of the message.
 * @param[in]	sParentSourceKey
 *					THe source key of the folder containing the message.
 *
 * @return HRESULT
 */
ECRESULT AddToLastSyncedMessagesSet(ECDatabase *lpDatabase, unsigned int ulSyncId, const SOURCEKEY &sSourceKey, const SOURCEKEY &sParentSourceKey);

ECRESULT CheckWithinLastSyncedMessagesSet(ECDatabase *lpDatabase, unsigned int ulSyncId, const SOURCEKEY &sSourceKey);
ECRESULT RemoveFromLastSyncedMessagesSet(ECDatabase *lpDatabase, unsigned int ulSyncId, const SOURCEKEY &sSourceKey, const SOURCEKEY &sParentSourceKey);

#endif
