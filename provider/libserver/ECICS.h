/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECICS_H
#define ECICS_H

#include <kopano/zcdefs.h>
#include "ECSession.h"
#include <set>

struct soap;

namespace KC {

// This class is used to pass SOURCEKEYs internally between parts of the server backend. You can use it as a char* to get the data, use size() to get the size,
// and have various ways of creating new SOURCEKEYs, including using a GUID and an ID, which is used for kopano-generated source keys.

/* Variable size, but can also be prominently 22 bytes */
class SOURCEKEY final {
public:
	SOURCEKEY(void) : ulSize(0) {}
	SOURCEKEY(const SOURCEKEY &s) : ulSize(s.ulSize)
	{
		if (ulSize > 0) {
			lpData.reset(new char[s.ulSize]);
			assert(s.lpData != nullptr);
			memcpy(lpData.get(), s.lpData.get(), s.ulSize);
		}
	}
	SOURCEKEY(SOURCEKEY &&o) :
	    ulSize(o.ulSize), lpData(std::move(o.lpData))
	{}
	SOURCEKEY(unsigned int z, const void *d) : ulSize(z)
	{
		if (d != nullptr && z > 0) {
			lpData.reset(new char[ulSize]);
			memcpy(lpData.get(), d, ulSize);
		}
	}
	SOURCEKEY(const GUID &guid, unsigned long long ullId) :
		ulSize(sizeof(GUID) + 6), lpData(new char[ulSize])
	{
		memcpy(&lpData[0], &guid, sizeof(guid));
		/* Ensure little endian order */
		lpData[sizeof(GUID)]   = ullId;
		lpData[sizeof(GUID)+1] = ullId >> 8;
		lpData[sizeof(GUID)+2] = ullId >> 16;
		lpData[sizeof(GUID)+3] = ullId >> 24;
		lpData[sizeof(GUID)+4] = ullId >> 32;
		lpData[sizeof(GUID)+5] = ullId >> 40;
	}
	SOURCEKEY(const struct xsd__base64Binary &sourcekey) :
		ulSize(sourcekey.__size)
	{
		if (ulSize > 0) {
			lpData.reset(new char[ulSize]);
			assert(sourcekey.__ptr != nullptr);
			memcpy(lpData.get(), sourcekey.__ptr, sourcekey.__size);
		}
	}
    SOURCEKEY&  operator= (const SOURCEKEY &s) {
        if(&s == this) return *this;
		lpData.reset(new char[s.ulSize]);
		ulSize = s.ulSize;
		if (ulSize > 0) {
			assert(s.lpData != nullptr);
			memcpy(lpData.get(), s.lpData.get(), ulSize);
		}
        return *this;
    }

    bool operator == (const SOURCEKEY &s) const {
		return this == &s || (ulSize == s.ulSize &&
		       memcmp(lpData.get(), s.lpData.get(), s.ulSize) == 0);
    }

	bool operator < (const SOURCEKEY &s) const {
		if(this == &s)
			return false;
		if(ulSize == s.ulSize)
			return memcmp(lpData.get(), s.lpData.get(), ulSize) < 0;
		else if(ulSize > s.ulSize) {
			int d = memcmp(lpData.get(), s.lpData.get(), s.ulSize);
			return (d == 0) ? false : (d < 0);			// If the compared part is equal, the shortes is less (s)
		} else {
			int d = memcmp(lpData.get(), s.lpData.get(), ulSize);
			return (d == 0) ? true : (d < 0);			// If the compared part is equal, the shortes is less (this)
		}
	}

	operator const unsigned char *(void) const { return reinterpret_cast<const unsigned char *>(lpData != nullptr ? lpData.get() : ""); }
	explicit operator std::string() const { return std::string(lpData != nullptr ? lpData.get() : "", ulSize); }
	operator SBinary() const { return SBinary{ulSize, reinterpret_cast<BYTE *>(lpData.get())}; }
    unsigned int 	size() const { return ulSize; }
	bool			empty() const { return ulSize == 0; }
private:
	unsigned int ulSize;
	std::unique_ptr<char[]> lpData;
};

ECRESULT AddChange(BTSession *lpecSession, unsigned int ulSyncId, const SOURCEKEY &sSourceKey, const SOURCEKEY &sParentSourceKey, unsigned int ulChange, unsigned int ulFlags = 0, bool fForceNewChangeKey = false, std::string *lpstrChangeKey = NULL, std::string *lpstrChangeList = NULL);
extern ECRESULT AddABChange(BTSession *, unsigned int change, SOURCEKEY &&sk, SOURCEKEY &&parent);
ECRESULT GetChanges(struct soap *soap, ECSession *lpSession, SOURCEKEY sSourceKeyFolder, unsigned int ulSyncId, unsigned int ulChangeId, unsigned int ulChangeType, unsigned int ulFlags, struct restrictTable *lpsRestrict, unsigned int *lpulMaxChangeId, icsChangesArray **lppChanges);
ECRESULT GetSyncStates(struct soap *soap, ECSession *lpSession, mv_long ulaSyncId, syncStateArray *lpsaSyncState);
extern KC_EXPORT void *CleanupSyncsTable(void *);
extern KC_EXPORT void *CleanupSyncedMessagesTable(void *);

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
 */
ECRESULT AddToLastSyncedMessagesSet(ECDatabase *lpDatabase, unsigned int ulSyncId, const SOURCEKEY &sSourceKey, const SOURCEKEY &sParentSourceKey);

ECRESULT CheckWithinLastSyncedMessagesSet(ECDatabase *lpDatabase, unsigned int ulSyncId, const SOURCEKEY &sSourceKey);
ECRESULT RemoveFromLastSyncedMessagesSet(ECDatabase *lpDatabase, unsigned int ulSyncId, const SOURCEKEY &sSourceKey, const SOURCEKEY &sParentSourceKey);

} /* namespace */

#endif
