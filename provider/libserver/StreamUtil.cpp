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
#include <new>
#include <cstdint>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include "StreamUtil.h"
#include "StorageUtil.h"
#include "cmdutil.hpp"
#include "ECAttachmentStorage.h"
#include "ECSecurity.h"
#include "ECSessionManager.h"
#include "ECGenProps.h"
#include "ECTPropsPurge.h"
#include "ECICS.h"
#include "ECMemStream.h"
#include <kopano/MAPIErrors.h>

#include <kopano/charset/convert.h>
#include <kopano/charset/utf8string.h>

#include <ECFifoBuffer.h>
#include <ECSerializer.h>
#include <kopano/stringutil.h>
#include <mapitags.h>
#include <kopano/mapiext.h>
#include <mapidefs.h>
#include <edkmdb.h>

#include <set>
#include <map>
#include <string>

/*
	streams are as follows:
	  <version>
	  <message>

	messages are as follows:
	  <# of props>:32bit
	    <property>
	  <# of sub objects>:32bit
	    <subobject>

	properties are as follows:
	  <prop tag>:32bit
	  [<length in bytes>:32bit]
	  <data bytes>
	  [<guid, kind, [id|bytes+string>]

	subobjects are as follows:
	  <type>:32bit
	  <id>:32bit
	  <message>
*/

using namespace KCHL;

static inline bool operator<(const GUID &lhs, const GUID &rhs)
{
	return memcmp(&lhs, &rhs, sizeof(GUID)) < 0;
}

namespace KC {

class ECStreamSerializer _kc_final : public ECSerializer {
	public:
	ECStreamSerializer(IStream *lpBuffer);
	virtual ECRESULT SetBuffer(void *) _kc_override;
	virtual ECRESULT Write(const void *ptr, size_t size, size_t nmemb) _kc_override;
	virtual ECRESULT Read(void *ptr, size_t size, size_t nmemb) _kc_override;
	virtual ECRESULT Skip(size_t size, size_t nmemb) _kc_override;
	virtual ECRESULT Flush(void) _kc_override;
	virtual ECRESULT Stat(ULONG *have_read, ULONG *have_written) _kc_override;

	private:
	IStream *m_lpBuffer;
	ULONG m_ulRead = 0, m_ulWritten = 0;
};

const static struct StreamCaps {
	bool bSupportUnicode;
} g_StreamCaps[] = {
	{ false },		// version 0
	{ true },		// version 1
};

#define FIELD_NR_NAMEID		(FIELD_NR_MAX + 1)
#define FIELD_NR_NAMESTR	(FIELD_NR_MAX + 2)
#define FIELD_NR_NAMEGUID	(FIELD_NR_MAX + 3)

#define STREAM_VERSION			1	// encode strings in UTF-8.
#define STREAM_CAPS_CURRENT		(&g_StreamCaps[STREAM_VERSION])

#define CHARSET_WIN1252	"WINDOWS-1252//TRANSLIT"

// External objects
extern ECSessionManager *g_lpSessionManager;	// ECServerEntrypoint.cpp

// Helper class for mapping named properties from the stream to local proptags
class NamedPropertyMapper {
public:
	NamedPropertyMapper(ECDatabase *lpDatabase);

	ECRESULT GetId(const GUID &guid, unsigned int ulNameId, unsigned int *lpId);
	ECRESULT GetId(const GUID &guid, const std::string &strNameString, unsigned int *lpId);

private:
	typedef std::pair<GUID,unsigned int> nameidkey_t;
	typedef std::pair<GUID,std::string> namestringkey_t;
	typedef std::map<nameidkey_t,unsigned int> nameidmap_t;
	typedef std::map<namestringkey_t,unsigned int> namestringmap_t;

	ECDatabase *m_lpDatabase;
	nameidmap_t m_mapNameIds;
	namestringmap_t m_mapNameStrings;
};

ECStreamSerializer::ECStreamSerializer(IStream *lpBuffer)
{
	SetBuffer(lpBuffer);
}

ECRESULT ECStreamSerializer::SetBuffer(void *lpBuffer)
{
	m_lpBuffer = static_cast<IStream *>(lpBuffer);
	return erSuccess;
}

ECRESULT ECStreamSerializer::Write(const void *ptr, size_t size, size_t nmemb)
{
	ECRESULT er = erSuccess;
	ULONG cbWritten = 0;
	union {
		char c[8];
		short s;
		int i;
		long long ll;
	} tmp;

	if (ptr == NULL)
		return KCERR_INVALID_PARAMETER;

	switch (size) {
	case 1:
		er = m_lpBuffer->Write(ptr, nmemb, &cbWritten);
		break;
	case 2:
		for (size_t x = 0; x < nmemb && er == erSuccess; ++x) {
			tmp.s = htons(static_cast<const short *>(ptr)[x]);
			er = m_lpBuffer->Write(&tmp, size, &cbWritten);
		}
		break;
	case 4:
		for (size_t x = 0; x < nmemb && er == erSuccess; ++x) {
			tmp.i = htonl(static_cast<const int *>(ptr)[x]);
			er = m_lpBuffer->Write(&tmp, size, &cbWritten);
		}
		break;
	case 8:
		for (size_t x = 0; x < nmemb && er == erSuccess; ++x) {
			tmp.ll = cpu_to_be64(static_cast<const uint64_t *>(ptr)[x]);
			er = m_lpBuffer->Write(&tmp, size, &cbWritten);
		}
		break;
	default:
		er = KCERR_INVALID_PARAMETER;
		break;
	}
	m_ulWritten += size * nmemb;
	return er;
}

ECRESULT ECStreamSerializer::Read(void *ptr, size_t size, size_t nmemb)
{
	ECRESULT er;
	ULONG cbRead = 0;

	if (ptr == nullptr)
		return KCERR_INVALID_PARAMETER;
	er = m_lpBuffer->Read(ptr, size * nmemb, &cbRead);
	if (er != erSuccess)
		return er;
	m_ulRead += cbRead;
	if (cbRead != size * nmemb)
		return KCERR_CALL_FAILED;

	switch (size) {
	case 1: break;
	case 2:
		for (size_t x = 0; x < nmemb; ++x) {
			uint16_t tmp;
			memcpy(&tmp, static_cast<uint16_t *>(ptr) + x, sizeof(tmp));
			tmp = ntohs(tmp);
			memcpy(static_cast<uint16_t *>(ptr) + x, &tmp, sizeof(tmp));
		}
		break;
	case 4:
		for (size_t x = 0; x < nmemb; ++x) {
			uint32_t tmp;
			memcpy(&tmp, static_cast<uint32_t *>(ptr) + x, sizeof(tmp));
			tmp = ntohl(tmp);
			memcpy(static_cast<uint32_t *>(ptr) + x, &tmp, sizeof(tmp));
		}
		break;
	case 8:
		for (size_t x = 0; x < nmemb; ++x) {
			uint64_t tmp;
			memcpy(&tmp, static_cast<uint64_t *>(ptr) + x, sizeof(tmp));
			tmp = be64_to_cpu(tmp);
			memcpy(static_cast<uint64_t *>(ptr) + x, &tmp, sizeof(tmp));
		}
		break;
	default:
		er = KCERR_INVALID_PARAMETER;
		break;
	}
	return er;
}

ECRESULT ECStreamSerializer::Skip(size_t size, size_t nmemb)
{
	ECRESULT er = erSuccess;
	char buffer[4096];
	ULONG read = 0;

	for (size_t total = 0; total < nmemb * size; total += read) {
		er = m_lpBuffer->Read(buffer, std::min(sizeof(buffer), (size * nmemb) - total), &read);
		if (er != erSuccess)
			return er;
	}
	return er;
}

ECRESULT ECStreamSerializer::Flush()
{
	ECRESULT er;
	ULONG cbRead = 0;
	char buf[16384];

	while (true) {
		er = m_lpBuffer->Read(buf, sizeof(buf), &cbRead);
		if (er != erSuccess)
			return er;
		m_ulRead += cbRead;
		if (cbRead < sizeof(buf))
			break;
	}
	return er;
}

ECRESULT ECStreamSerializer::Stat(ULONG *lpcbRead, ULONG *lpcbWrite)
{
	if (lpcbRead != nullptr)
		*lpcbRead = m_ulRead;
	if (lpcbWrite != nullptr)
		*lpcbWrite = m_ulWritten;
	return erSuccess;
}

NamedPropertyMapper::NamedPropertyMapper(ECDatabase *lpDatabase)
	: m_lpDatabase(lpDatabase) 
{
	assert(lpDatabase != NULL);
}

ECRESULT NamedPropertyMapper::GetId(const GUID &guid, unsigned int ulNameId, unsigned int *lpulId)
{
	ECRESULT er = erSuccess;

	std::string strQuery;
	DB_RESULT lpResult;
	DB_ROW lpRow = NULL;

	nameidkey_t key(guid, ulNameId);
	nameidmap_t::const_iterator i = m_mapNameIds.find(key);
	if (i != m_mapNameIds.cend()) {
		*lpulId = i->second;
		return erSuccess;
	}

	// Check the database
	strQuery = 
		"SELECT id FROM names "
		"WHERE nameid=" + stringify(ulNameId) +
		" AND guid=" + m_lpDatabase->EscapeBinary(&guid, sizeof(guid));

	er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		return er;

	lpRow = lpResult.fetch_row();
	if (lpRow != nullptr) {
		if (lpRow[0] == NULL) {
			ec_log_err("NamedPropertyMapper::GetId(): column null");
			return KCERR_DATABASE_ERROR;
		}

		*lpulId = atoui((char*)lpRow[0]) + 0x8501;
	} else {
		// Create the named property
		strQuery = 
			"INSERT INTO names (nameid, guid) "
			"VALUES(" + stringify(ulNameId) + "," + m_lpDatabase->EscapeBinary(&guid, sizeof(guid)) + ")";
		er = m_lpDatabase->DoInsert(strQuery, lpulId);
		if (er != erSuccess)
			return er;
		*lpulId += 0x8501;
	}

	// *lpulId now contains the local propid, update the cache
	m_mapNameIds.emplace(key, *lpulId);
	return erSuccess;
}

ECRESULT NamedPropertyMapper::GetId(const GUID &guid, const std::string &strNameString, unsigned int *lpulId)
{
	ECRESULT er = erSuccess;

	std::string strQuery;
	DB_RESULT lpResult;
	DB_ROW lpRow = NULL;

	namestringkey_t key(guid, strNameString);
	namestringmap_t::const_iterator i = m_mapNameStrings.find(key);
	if (i != m_mapNameStrings.cend()) {
		*lpulId = i->second;
		return erSuccess;
	}

	// Check the database
	strQuery = 
		"SELECT id FROM names "
		"WHERE namestring='" + m_lpDatabase->Escape(strNameString) + "'"
		" AND guid=" + m_lpDatabase->EscapeBinary(&guid, sizeof(guid));
	er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		return er;

	lpRow = lpResult.fetch_row();
	if (lpRow != nullptr) {
		if (lpRow[0] == NULL) {
			ec_log_err("NamedPropertyMapper::GetId(): column null");
			return KCERR_DATABASE_ERROR;
		}

		*lpulId = atoui((char*)lpRow[0]) + 0x8501;
	} else {
		// Create the named property
		strQuery = 
			"INSERT INTO names (namestring, guid) "
			"VALUES('" + m_lpDatabase->Escape(strNameString) + "'," + m_lpDatabase->EscapeBinary(&guid, sizeof(guid)) + ")";
		er = m_lpDatabase->DoInsert(strQuery, lpulId);
		if (er != erSuccess)
			return er;
		*lpulId += 0x8501;
	}

	// *lpulId now contains the local propid, update the cache
	m_mapNameStrings.emplace(key, *lpulId);
	return erSuccess;
}

// Utility Functions
static ECRESULT GetValidatedPropType(DB_ROW, unsigned int *type);

static ECRESULT SerializeDatabasePropVal(const StreamCaps *lpStreamCaps,
    DB_ROW lpRow, DB_LENGTHS lpLen, ECSerializer *lpSink)
{
	ECRESULT er = erSuccess;
	unsigned int type = 0;
	unsigned int ulPropTag = 0;
	unsigned int ulCount;
	unsigned int ulLen;
	unsigned int ulLastPos;
	unsigned int ulLastPos2;
	std::string	strData;

	unsigned int ulKind;
	unsigned int ulNameId;
	std::string strNameString;
	locale_t loc = createlocale(LC_NUMERIC, "C");
	convert_context converter;

	short i;
	unsigned int ul;
	float flt;
	unsigned char b;
	double dbl;
	hiloLong hilo;
	long long li;

	er = GetValidatedPropType(lpRow, &type);
	if (er == KCERR_DATABASE_ERROR) {
		er = erSuccess;
		goto exit;
	}
	else if (er != erSuccess) {
		goto exit;
	}

	ulPropTag = PROP_TAG(type, atoi(lpRow[FIELD_NR_TAG]));
	er = lpSink->Write(&ulPropTag, sizeof(ulPropTag), 1);
	if (er != erSuccess)
		goto exit;

	switch (type) {
	case PT_I2:
		if (lpRow[FIELD_NR_ULONG] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		i = (short)atoi(lpRow[FIELD_NR_ULONG]);
		er = lpSink->Write(&i, sizeof(i), 1);
		break;
	case PT_LONG:
		if (lpRow[FIELD_NR_ULONG] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		ul = atoui(lpRow[FIELD_NR_ULONG]);
		er = lpSink->Write(&ul, sizeof(ul), 1);
		break;
	case PT_R4:
		if (lpRow[FIELD_NR_DOUBLE] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		flt = (float)strtod_l(lpRow[FIELD_NR_DOUBLE], NULL, loc);
		er = lpSink->Write(&flt, sizeof(flt), 1);
		break;
	case PT_BOOLEAN:
		if (lpRow[FIELD_NR_ULONG] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		b = (atoi(lpRow[FIELD_NR_ULONG]) ? 1 : 0);
		er = lpSink->Write(&b, sizeof(b), 1);
		break;
	case PT_DOUBLE:
	case PT_APPTIME:
		if (lpRow[FIELD_NR_DOUBLE] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		dbl = strtod_l(lpRow[FIELD_NR_DOUBLE], NULL, loc);
		er = lpSink->Write(&dbl, sizeof(dbl), 1);
		break;
	case PT_CURRENCY:
	case PT_SYSTIME:
		if (lpRow[FIELD_NR_HI] == NULL || lpRow[FIELD_NR_LO] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		hilo.hi = atoi(lpRow[FIELD_NR_HI]);
		hilo.lo = atoui(lpRow[FIELD_NR_LO]);
		er = lpSink->Write(&hilo.hi, sizeof(hilo.hi), 1);
		if (er == erSuccess)
			er = lpSink->Write(&hilo.lo, sizeof(hilo.lo), 1);
		break;
	case PT_I8:
		if (lpRow[FIELD_NR_LONGINT] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		li = atoll(lpRow[FIELD_NR_LONGINT]);
		er = lpSink->Write(&li, sizeof(li), 1);
		break;
	case PT_STRING8:
	case PT_UNICODE:
		if (lpRow[FIELD_NR_STRING] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		if (lpStreamCaps->bSupportUnicode) {
			ulLen = lpLen[FIELD_NR_STRING];
			er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
			if (er == erSuccess)
				er = lpSink->Write(lpRow[FIELD_NR_STRING], 1, lpLen[FIELD_NR_STRING]);
		} else {
			const std::string strEncoded = converter.convert_to<std::string>(CHARSET_WIN1252, lpRow[FIELD_NR_STRING], lpLen[FIELD_NR_STRING], "UTF-8");
			ulLen = strEncoded.length();
			er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
			if (er == erSuccess)
				er = lpSink->Write(strEncoded.data(), 1, ulLen);
		}
		break;
	case PT_CLSID:
	case PT_BINARY:
		if (lpRow[FIELD_NR_BINARY] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		ulLen = lpLen[FIELD_NR_BINARY];
		er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
		if (er == erSuccess)
			er = lpSink->Write(lpRow[FIELD_NR_BINARY], 1, lpLen[FIELD_NR_BINARY]);
		break;
	case PT_MV_I2:
		if (lpRow[FIELD_NR_ULONG] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}

		ulCount = atoi(lpRow[FIELD_NR_ID]);
		er = lpSink->Write(&ulCount, sizeof(ulCount), 1);
		ulLastPos = 0;
		for (unsigned int x = 0; er == erSuccess && x < ulCount; ++x) {
			ParseMVProp(lpRow[FIELD_NR_ULONG], lpLen[FIELD_NR_ULONG], &ulLastPos, &strData);
			i = (short)atoi(strData.c_str());
			er = lpSink->Write(&i, sizeof(i), 1);
		}
		break;
	case PT_MV_LONG:
		if (lpRow[FIELD_NR_ULONG] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}

		ulCount = atoi(lpRow[FIELD_NR_ID]);
		er = lpSink->Write(&ulCount, sizeof(ulCount), 1);
		ulLastPos = 0;
		for (unsigned int x = 0; er == erSuccess && x < ulCount; ++x) {
			ParseMVProp(lpRow[FIELD_NR_ULONG], lpLen[FIELD_NR_ULONG], &ulLastPos, &strData);
			ul = atoui((char*)strData.c_str());
			er = lpSink->Write(&ul, sizeof(ul), 1);
		}
		break;
	case PT_MV_R4:
		if (lpRow[FIELD_NR_DOUBLE] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}

		ulCount = atoi(lpRow[FIELD_NR_ID]);
		er = lpSink->Write(&ulCount, sizeof(ulCount), 1);
		ulLastPos = 0;
		for (unsigned int x = 0; er == erSuccess && x < ulCount; ++x) {
			ParseMVProp(lpRow[FIELD_NR_DOUBLE], lpLen[FIELD_NR_DOUBLE], &ulLastPos, &strData);
			flt = (float)strtod_l(strData.c_str(), NULL, loc);
			er = lpSink->Write(&flt, sizeof(flt), 1);
		}
		break;
	case PT_MV_DOUBLE:
	case PT_MV_APPTIME:
		if (lpRow[FIELD_NR_DOUBLE] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}

		ulCount = atoi(lpRow[FIELD_NR_ID]);
		er = lpSink->Write(&ulCount, sizeof(ulCount), 1);
		ulLastPos = 0;
		for (unsigned int x = 0; er == erSuccess && x < ulCount; ++x) {
			ParseMVProp(lpRow[FIELD_NR_DOUBLE], lpLen[FIELD_NR_DOUBLE], &ulLastPos, &strData);
			dbl = strtod_l(strData.c_str(), NULL, loc);
			er = lpSink->Write(&dbl, sizeof(dbl), 1);
		}
		break;
	case PT_MV_CURRENCY:
	case PT_MV_SYSTIME:
		if (lpRow[FIELD_NR_HI] == NULL || lpRow[FIELD_NR_LO] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		ulCount = atoi(lpRow[FIELD_NR_ID]);
		er = lpSink->Write(&ulCount, sizeof(ulCount), 1);
		ulLastPos = 0;
		ulLastPos2 = 0;
		for (unsigned int x = 0; er == erSuccess && x < ulCount; ++x) {
			ParseMVProp(lpRow[FIELD_NR_LO], lpLen[FIELD_NR_LO], &ulLastPos, &strData);
			hilo.lo = atoui((char*)strData.c_str());
			ParseMVProp(lpRow[FIELD_NR_HI], lpLen[FIELD_NR_HI], &ulLastPos2, &strData);
			hilo.hi = atoi((char*)strData.c_str());
			er = lpSink->Write(&hilo.hi, sizeof(hilo.hi), 1);
			if (er == erSuccess)
				er = lpSink->Write(&hilo.lo, sizeof(hilo.lo), 1);
		}
		break;
	case PT_MV_BINARY:
	case PT_MV_CLSID:
		if (lpRow[FIELD_NR_BINARY] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		ulCount = atoi(lpRow[FIELD_NR_ID]);
		er = lpSink->Write(&ulCount, sizeof(ulCount), 1);
		ulLastPos = 0;
		for (unsigned int x = 0; er == erSuccess && x < ulCount; ++x) {
			ParseMVProp(lpRow[FIELD_NR_BINARY], lpLen[FIELD_NR_BINARY], &ulLastPos, &strData);
			ulLen = (unsigned int)strData.size();
			er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
			if (er == erSuccess)
				er = lpSink->Write(strData.c_str(), 1, ulLen);
		}
		break;
	case PT_MV_STRING8:
	case PT_MV_UNICODE:
		if (lpRow[FIELD_NR_STRING] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		ulCount = atoi(lpRow[FIELD_NR_ID]);
		er = lpSink->Write(&ulCount, sizeof(ulCount), 1);
		ulLastPos = 0;
		for (unsigned int x = 0; er == erSuccess && x < ulCount; ++x) {
			ParseMVProp(lpRow[FIELD_NR_STRING], lpLen[FIELD_NR_STRING], &ulLastPos, &strData);
			if (lpStreamCaps->bSupportUnicode) {
				ulLen = (unsigned int)strData.size();
				er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
				if (er == erSuccess)
					er = lpSink->Write(strData.c_str(), 1, ulLen);
			} else {
				const std::string strEncoded = converter.convert_to<std::string>(CHARSET_WIN1252, strData, rawsize(strData), "UTF-8");
				ulLen = (unsigned int)strEncoded.size();
				er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
				if (er == erSuccess)
					er = lpSink->Write(strEncoded.c_str(), 1, ulLen);
			}
		}
		break;
	case PT_MV_I8:
		if (lpRow[FIELD_NR_LONGINT] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}

		ulCount = atoi(lpRow[FIELD_NR_ID]);
		er = lpSink->Write(&ulCount, sizeof(ulCount), 1);
		ulLastPos = 0;
		for (unsigned int x = 0; er == erSuccess && x < ulCount; ++x) {
			ParseMVProp(lpRow[FIELD_NR_LONGINT], lpLen[FIELD_NR_LONGINT], &ulLastPos, &strData);
			li = atoll(strData.c_str());
			er = lpSink->Write(&li, sizeof(li), 1);
		}
		break;
	default:
		er = KCERR_INVALID_TYPE;
		goto exit;
	}

	// If property is named property in the dynamic range we need to add some extra info
	if (PROP_ID(ulPropTag) > 0x8500) {
		// Send out the GUID.
		er = lpSink->Write(lpRow[FIELD_NR_NAMEGUID], 1, lpLen[FIELD_NR_NAMEGUID]);

		if (er == erSuccess && lpRow[FIELD_NR_NAMEID] != NULL) {
			ulKind = MNID_ID;
			ulNameId = atoui((char*)lpRow[FIELD_NR_NAMEID]);

			er = lpSink->Write(&ulKind, sizeof(ulKind), 1);
			if (er == erSuccess)
				er = lpSink->Write(&ulNameId, sizeof(ulNameId), 1);

		} else if (er == erSuccess && lpRow[FIELD_NR_NAMESTR] != NULL) {
			ulKind = MNID_STRING;
			ulLen = lpLen[FIELD_NR_NAMESTR];

			er = lpSink->Write(&ulKind, sizeof(ulKind), 1);
			if (er == erSuccess)
				er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
			if (er == erSuccess)
				er = lpSink->Write(lpRow[FIELD_NR_NAMESTR], 1, ulLen);

		} else if (er == erSuccess)
			er = KCERR_INVALID_TYPE;
	}

exit:
	freelocale(loc);
	return er;
}

static ECRESULT SerializePropVal(const StreamCaps *lpStreamCaps,
    const struct propVal &sPropVal, ECSerializer *lpSink,
    const NamedPropDefMap *lpNamedPropDefs)
{
	ECRESULT er;
	unsigned int type = PROP_TYPE(sPropVal.ulPropTag);
	unsigned int ulLen;
	unsigned char b;
	unsigned int ulPropTag = sPropVal.ulPropTag;
	convert_context converter;
	NamedPropDefMap::const_iterator iNamedPropDef;

	// We always stream PT_STRING8
	if(PROP_TYPE(ulPropTag) == PT_UNICODE)
		ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_STRING8);
	else if(PROP_TYPE(ulPropTag) == PT_MV_UNICODE)
		ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_MV_STRING8);

	if (PROP_ID(ulPropTag) > 0x8500) {
		assert(lpNamedPropDefs != NULL);
		if (!lpNamedPropDefs)
			return KCERR_INVALID_TYPE;
		iNamedPropDef = lpNamedPropDefs->find(ulPropTag);
		assert(iNamedPropDef != lpNamedPropDefs->cend());
		if (iNamedPropDef == lpNamedPropDefs->cend())
			return KCERR_NOT_FOUND;
	}
	
	er = lpSink->Write((unsigned char *)&ulPropTag, sizeof(ulPropTag), 1);
	if (er != erSuccess)
		return er;

	switch (type) {
	case PT_I2:
		er = lpSink->Write(&sPropVal.Value.i, sizeof(sPropVal.Value.i), 1);
		break;
	case PT_LONG:
		er = lpSink->Write(&sPropVal.Value.ul, sizeof(sPropVal.Value.ul), 1);
		break;
	case PT_R4:
		er = lpSink->Write(&sPropVal.Value.flt, sizeof(sPropVal.Value.flt), 1);
		break;
	case PT_BOOLEAN:
		b = (sPropVal.Value.b ? 1 : 0);
		er = lpSink->Write(&b, sizeof(b), 1);
		break;
	case PT_DOUBLE:
	case PT_APPTIME:
		er = lpSink->Write(&sPropVal.Value.dbl, sizeof(sPropVal.Value.dbl), 1);
		break;
	case PT_CURRENCY:
	case PT_SYSTIME:
		er = lpSink->Write(&sPropVal.Value.hilo->hi, sizeof(sPropVal.Value.hilo->hi), 1);
		if (er == erSuccess)
			er = lpSink->Write(&sPropVal.Value.hilo->lo, sizeof(sPropVal.Value.hilo->lo), 1);
		break;
	case PT_I8:
		er = lpSink->Write(&sPropVal.Value.li, sizeof(sPropVal.Value.li), 1);
		break;
	case PT_STRING8:
	case PT_UNICODE:
		if (lpStreamCaps->bSupportUnicode) {
			ulLen = (unsigned)strlen(sPropVal.Value.lpszA);
			er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
			if (er == erSuccess)
				er = lpSink->Write(sPropVal.Value.lpszA, 1, ulLen);
		} else {
			const std::string strEncoded = converter.convert_to<std::string>(CHARSET_WIN1252, sPropVal.Value.lpszA, rawsize(sPropVal.Value.lpszA), "UTF-8");
			ulLen = strEncoded.length();
			er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
			if (er == erSuccess)
				er = lpSink->Write(strEncoded.data(), 1, ulLen);
		}
		break;
	case PT_CLSID:
	case PT_BINARY:
		er = lpSink->Write(&sPropVal.Value.bin->__size, sizeof(sPropVal.Value.bin->__size), 1);
		if (er == erSuccess)
			er = lpSink->Write(sPropVal.Value.bin->__ptr, 1, sPropVal.Value.bin->__size);
		break;
	case PT_MV_I2:
		er = lpSink->Write(&sPropVal.Value.mvi.__size, sizeof(sPropVal.Value.mvi.__size), 1);
		for (gsoap_size_t x = 0; er == erSuccess && x < sPropVal.Value.mvi.__size; ++x)
			er = lpSink->Write(&sPropVal.Value.mvi.__ptr[x], sizeof(sPropVal.Value.mvi.__ptr[x]), 1);
		break;
	case PT_MV_LONG:
		er = lpSink->Write(&sPropVal.Value.mvl.__size, sizeof(sPropVal.Value.mvl.__size), 1);
		for (gsoap_size_t x = 0; er == erSuccess && x < sPropVal.Value.mvl.__size; ++x)
			er = lpSink->Write(&sPropVal.Value.mvl.__ptr[x], sizeof(sPropVal.Value.mvl.__ptr[x]), 1);
		break;
	case PT_MV_R4:
		er = lpSink->Write(&sPropVal.Value.mvflt.__size, sizeof(sPropVal.Value.mvflt.__size), 1);
		for (gsoap_size_t x = 0; er == erSuccess && x < sPropVal.Value.mvflt.__size; ++x)
			er = lpSink->Write(&sPropVal.Value.mvflt.__ptr[x], sizeof(sPropVal.Value.mvflt.__ptr[x]), 1);
		break;
	case PT_MV_DOUBLE:
	case PT_MV_APPTIME:
		er = lpSink->Write(&sPropVal.Value.mvdbl.__size, sizeof(sPropVal.Value.mvdbl.__size), 1);
		for (gsoap_size_t x = 0; er == erSuccess && x < sPropVal.Value.mvdbl.__size; ++x)
			er = lpSink->Write(&sPropVal.Value.mvdbl.__ptr[x], sizeof(sPropVal.Value.mvdbl.__ptr[x]), 1);
		break;
	case PT_MV_CURRENCY:
	case PT_MV_SYSTIME:
		er = lpSink->Write(&sPropVal.Value.mvhilo.__size, sizeof(sPropVal.Value.mvhilo.__size), 1);
		for (gsoap_size_t x = 0; er == erSuccess && x < sPropVal.Value.mvhilo.__size; ++x) {
			er = lpSink->Write(&sPropVal.Value.mvhilo.__ptr[x].hi, sizeof(sPropVal.Value.mvhilo.__ptr[x].hi), 1);
			if (er == erSuccess)
				er = lpSink->Write(&sPropVal.Value.mvhilo.__ptr[x].lo, sizeof(sPropVal.Value.mvhilo.__ptr[x].lo), 1);
		}
		break;
	case PT_MV_BINARY:
	case PT_MV_CLSID:
		er = lpSink->Write(&sPropVal.Value.mvbin.__size, sizeof(sPropVal.Value.mvbin.__size), 1);
		for (gsoap_size_t x = 0; er == erSuccess && x < sPropVal.Value.mvbin.__size; ++x) {
			er = lpSink->Write(&sPropVal.Value.mvbin.__ptr[x].__size, sizeof(sPropVal.Value.mvbin.__ptr[x].__size), 1);
			if (er == erSuccess)
				er = lpSink->Write(sPropVal.Value.mvbin.__ptr[x].__ptr, 1, sPropVal.Value.mvbin.__ptr[x].__size);
		}
		break;
	case PT_MV_STRING8:
	case PT_MV_UNICODE:
		er = lpSink->Write(&sPropVal.Value.mvszA.__size, sizeof(sPropVal.Value.mvszA.__size), 1);
		for (gsoap_size_t x = 0; er == erSuccess && x < sPropVal.Value.mvszA.__size; ++x) {
			if (lpStreamCaps->bSupportUnicode) {
				ulLen = (unsigned)strlen(sPropVal.Value.mvszA.__ptr[x]);
				er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
				if (er == erSuccess)
					er = lpSink->Write(sPropVal.Value.mvszA.__ptr[x], 1, ulLen);
				continue;
			}
			const std::string strEncoded = converter.convert_to<std::string>(CHARSET_WIN1252, sPropVal.Value.mvszA.__ptr[x], rawsize(sPropVal.Value.mvszA.__ptr[x]), "UTF-8");
			ulLen = strEncoded.length();
			er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
			if (er == erSuccess)
				er = lpSink->Write(strEncoded.data(), 1, ulLen);
		}
		break;
	case PT_MV_I8:
		er = lpSink->Write(&sPropVal.Value.mvli.__size, sizeof(sPropVal.Value.mvli.__size), 1);
		for (gsoap_size_t x = 0; er == erSuccess && x < sPropVal.Value.mvli.__size; ++x)
			er = lpSink->Write(&sPropVal.Value.mvli.__ptr[x], sizeof(sPropVal.Value.mvli.__ptr[x]), 1);
		break;

	default:
		er = KCERR_INVALID_TYPE;
	}
	
	if (PROP_ID(sPropVal.ulPropTag) <= 0x8500)
		return er;

	// If property is named property in the dynamic range we need to add some extra info
	assert(lpNamedPropDefs != NULL && iNamedPropDef != lpNamedPropDefs->cend());
	// Send out the GUID.
	er = lpSink->Write(&iNamedPropDef->second.guid, 1, sizeof(iNamedPropDef->second.guid));
	if (er == erSuccess)
		er = lpSink->Write(&iNamedPropDef->second.ulKind, sizeof(iNamedPropDef->second.ulKind), 1);
	if (er != erSuccess)
		return er;
	if (iNamedPropDef->second.ulKind == MNID_ID) {
		er = lpSink->Write(&iNamedPropDef->second.ulId, sizeof(iNamedPropDef->second.ulId), 1);
	} else if (iNamedPropDef->second.ulKind == MNID_STRING) {
		unsigned int ulLen = iNamedPropDef->second.strName.size();
		er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
		if (er == erSuccess)
			er = lpSink->Write(iNamedPropDef->second.strName.data(), 1, ulLen);
	} else {
		er = KCERR_INVALID_TYPE;
	}
	return er;
}

static ECRESULT SerializeProps(struct propValArray *lpPropVals,
    LPCSTREAMCAPS lpStreamCaps, ECSerializer *lpSink,
    const NamedPropDefMap *lpNamedPropDefs)
{
	ECRESULT er;
	unsigned int	ulCount = 0;

	ulCount = lpPropVals->__size;
	
    er = lpSink->Write(&ulCount, sizeof(ulCount), 1);
	if (er != erSuccess)
		return er;
    	
	for (unsigned int i = 0; i < ulCount; ++i) {
		er = SerializePropVal(lpStreamCaps, lpPropVals->__ptr[i], lpSink, lpNamedPropDefs);
	        if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

static ECRESULT GetBestBody(ECDatabase *lpDatabase, unsigned int ulObjId,
    std::string *lpstrBestBody)
{
	ECRESULT er = erSuccess;
	DB_ROW 			lpDBRow = NULL;
	DB_RESULT lpDBResult;
	auto strQuery = "SELECT tag FROM properties WHERE hierarchyid=" + stringify(ulObjId) + " AND tag IN (4105, 4115) ORDER BY tag LIMIT 1";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;
	lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow && lpDBRow[0])
		*lpstrBestBody = lpDBRow[0];
	else
		*lpstrBestBody = "0";
	return erSuccess;
}

static ECRESULT SerializeProps(ECSession *lpecSession, ECDatabase *lpDatabase,
    ECAttachmentStorage *lpAttachmentStorage, LPCSTREAMCAPS lpStreamCaps,
    unsigned int ulObjId, unsigned int ulObjType, unsigned int ulStoreId,
    GUID *lpsGuid, ULONG ulFlags, ECSerializer *lpSink, bool bTop)
{
	ECRESULT		er = erSuccess;
	unsigned int	ulCount = 0;

	struct soap		*soap = NULL;
	struct propVal sPropVal{__gszeroinit};

	DB_ROW 			lpDBRow = NULL;
	DB_LENGTHS		lpDBLen = NULL;
	DB_RESULT lpDBResult;
	std::string		strQuery;
	object_ptr<ECMemStream> lpStream;
	object_ptr<IStream> lpIStream;
	ECStreamSerializer *	lpTempSink = NULL;
	bool			bUseSQLMulti = parseBool(g_lpSessionManager->GetConfig()->GetSetting("enable_sql_procedures"));

	std::list<struct propVal> sPropValList;

	assert(lpStreamCaps != NULL);
	er = ECMemStream::Create(nullptr, 0, STGM_SHARE_EXCLUSIVE | STGM_WRITE, nullptr, nullptr, nullptr, &~lpStream);
	if (er != erSuccess)
		goto exit;
	er = lpStream->QueryInterface(IID_IStream, &~lpIStream);
	if (er != erSuccess)
		goto exit;
	if (!lpAttachmentStorage) {
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}
	lpTempSink = new(std::nothrow) ECStreamSerializer(lpIStream);
	if (lpTempSink == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;

	// We'll (ab)use a soap structure as a memory pool.
	soap = soap_new();
	
	// PR_SOURCE_KEY
	if (bTop && ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_SOURCE_KEY, ulObjId, 0, ulStoreId, 0, ulObjType, &sPropVal) == erSuccess)
		sPropValList.emplace_back(sPropVal);

	if (bUseSQLMulti) {
		er = lpDatabase->GetNextResult(&lpDBResult);
	} else {
		// szGetProps
		std::string strMode = "0", strBestBody = "0";
		if(ulFlags & SYNC_BEST_BODY) {
			strMode = "1";
			er = GetBestBody(lpDatabase, ulObjId, &strBestBody);
			if (er != erSuccess)
				goto exit;
		} else if(ulFlags & SYNC_LIMITED_IMESSAGE)
			strMode = "2";
		
		strQuery = "SELECT " PROPCOLORDER ", 0, names.nameid, names.namestring, names.guid FROM properties "
			"LEFT JOIN names ON properties.tag-34049=names.id WHERE hierarchyid=" + stringify(ulObjId) + " AND (tag <= 34048 OR names.id IS NOT NULL) "
			"AND (tag NOT IN (4105, 4115) OR " + strMode + " = 0 OR (" + strMode + " = 1 AND tag = " + strBestBody + ")) "
			"UNION "
			"SELECT " MVPROPCOLORDER ", 0, names.nameid, names.namestring, names.guid FROM mvproperties "
			"LEFT JOIN names ON mvproperties.tag-34049=names.id WHERE hierarchyid=" + stringify(ulObjId) + " AND (tag <= 34048 OR names.id IS NOT NULL) "
			"GROUP BY tag, mvproperties.type"
			;
		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	}
	if (er != erSuccess)
		goto exit;

	// Properties
	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		lpDBLen = lpDBResult.fetch_row_lengths();
		if (lpDBRow == NULL || lpDBLen == NULL) {
			er = KCERR_DATABASE_ERROR;
			ec_log_err("SerializeProps(): fetchrow/fetchrowlengths failed");
			goto exit;
		}

		er = SerializeDatabasePropVal(lpStreamCaps, lpDBRow, lpDBLen, lpTempSink);
		if (er != erSuccess)
			goto exit;
		++ulCount;
	}

	for (const auto &pv : sPropValList) {
		/* No NamedPropDefMap needed for computed properties */
		er = SerializePropVal(lpStreamCaps, pv, lpTempSink, NULL);
		if (er != erSuccess)
			goto exit;
		++ulCount;
	}

	er = lpSink->Write(&ulCount, sizeof(ulCount), 1);
	if (er != erSuccess)
		goto exit;

	er = lpSink->Write(lpStream->GetBuffer(), 1, lpStream->GetSize());
	if (er != erSuccess)
		goto exit;

exit:
	delete lpTempSink;
	if (soap) {
		soap_destroy(soap);
		soap_end(soap);
		soap_free(soap);
	}

	return er;
}

/**
 * Serialize a Message directly from the database.
 * This method handles direct subobjects and recurses whenever an embedded
 * message is encountered.
 * 
 * @param[in] lpecSession			Pointer to the current session.
 * @param[in] lpStreamDatabase		Pointer to the database.
 * @param[in] lpAttachmentStorage	Pointer to the attachmentstore.
 * @param[in] lpStreamCaps			Pointer to a stream capability structore. Must be NULL except
 * 									when called by SerializeMessage itself.
 * @param[in] ulObjId				The hierarchyid of the object to serialize.
 * @param[in] ulObjType				The type of the object to serialize. Must be MAPI_MESSAGE.
 * @param[in] ulStoreId				The id of the store containing the message.
 * @param[in] lpsGuid				Seems to be unused.
 * @param[in] ulFlags				Flags (SYNC_BEST_BODY to stream the best body for the message,
 * 									SYNC_LIMITED_IMESSAGE to only stream the plain text body).
 * @param[in] lpSink				Pointer to an ECSerializer instance to which the serialized
 * 									data should be written.
 * @param[in] bTop					Specifies that this is a toplevel message. Must be true excep
 * 									when called by SerializeMessage itself.
 */
ECRESULT SerializeMessage(ECSession *lpecSession, ECDatabase *lpStreamDatabase, ECAttachmentStorage *lpAttachmentStorage, LPCSTREAMCAPS lpStreamCaps, unsigned int ulObjId, unsigned int ulObjType, unsigned int ulStoreId, GUID *lpsGuid, ULONG ulFlags, ECSerializer *lpSink, bool bTop)
{
	ECRESULT		er = erSuccess;
	unsigned int	ulStreamVersion = STREAM_VERSION;
	unsigned int	ulSubObjId = 0;
	unsigned int	ulSubObjType = 0;
	unsigned int	ulCount = 0;
	ChildPropsMap	mapChildProps;
	NamedPropDefMap	mapNamedPropDefs;

	DB_ROW 			lpDBRow = NULL;
	DB_LENGTHS		lpDBLen = NULL;
	DB_RESULT lpDBResult, lpDBResultAttachment;
	std::string		strQuery;
	bool			bUseSQLMulti = parseBool(g_lpSessionManager->GetConfig()->GetSetting("enable_sql_procedures"));

	if (ulObjType != MAPI_MESSAGE) {
		er = KCERR_NO_SUPPORT;
		goto exit;
	}
	
	if (lpStreamCaps == NULL) {
		lpStreamCaps = STREAM_CAPS_CURRENT;	// Set to current stream capabilities.

		if ((lpecSession->GetCapabilities() & KOPANO_CAP_UNICODE) == 0) {
			ulStreamVersion = 0;
			lpStreamCaps = &g_StreamCaps[0];
		}

		er = lpSink->Write(&ulStreamVersion, sizeof(ulStreamVersion), 1);
		if (er != erSuccess)
			goto exit;
	}

	// szGetProps
	er = SerializeProps(lpecSession, lpStreamDatabase, lpAttachmentStorage, lpStreamCaps, ulObjId, ulObjType, ulStoreId, lpsGuid, ulFlags, lpSink, bTop);
	if (er != erSuccess)
		goto exit;

	// szPrepareGetProps
	er = PrepareReadProps(NULL, lpStreamDatabase, !bUseSQLMulti, true, 0, ulObjId, 0, &mapChildProps, &mapNamedPropDefs);
	if (er != erSuccess)
		goto exit;

	if (bUseSQLMulti) {
		// Serialize sub objects
		er = lpStreamDatabase->GetNextResult(&lpDBResult);
	} else {
		// begin of loop part
		strQuery = "SELECT id,hierarchy.type FROM hierarchy WHERE parent=" + stringify(ulObjId);
		er = lpStreamDatabase->DoSelect(strQuery, &lpDBResult);
	}
	if (er != erSuccess)
		goto exit;
	ulCount = lpDBResult.get_num_rows();
	er = lpSink->Write(&ulCount, sizeof(ulCount), 1);
	if (er != erSuccess)
		goto exit;

	for (unsigned i = 0; i < ulCount; ++i) {
		lpDBRow = lpDBResult.fetch_row();
		lpDBLen = lpDBResult.fetch_row_lengths();
		if (lpDBRow == NULL || lpDBLen == NULL) {
			er = KCERR_DATABASE_ERROR;
			ec_log_err("SerializeMessage(): fetchrow/fetchrowlengths failed");
			goto exit;
		}

		// ulSubObjType should be MAPI_MAILUSER, MAPI_DISTLIST or MAPI_ATTACH. But in reality
		// it can be anything. We'll send the 'wrong' type so the receiver can also return
		// the wrong type that might be expected by a client.
		ulSubObjType = atoi(lpDBRow[1]);
		er = lpSink->Write(&ulSubObjType, sizeof(ulSubObjType), 1);
		if (er != erSuccess)
			goto exit;

		ulSubObjId = atoi(lpDBRow[0]);
		er = lpSink->Write(&ulSubObjId, sizeof(ulSubObjId), 1);
		if (er != erSuccess)
			goto exit;
			
		// Output properties for this object
		auto iterChild = mapChildProps.find(ulSubObjId);
		if (iterChild != mapChildProps.cend()) {
			struct propValArray props;
			
			iterChild->second.lpPropVals->GetPropValArray(&props);
			
			er = SerializeProps(&props, lpStreamCaps, lpSink, &mapNamedPropDefs);

			FreePropValArray(&props, false);

			if(er != erSuccess)
				goto exit;
		}
		
		if (ulSubObjType != MAPI_ATTACH)
			continue;

		unsigned int ulLen = 0;
		if (lpAttachmentStorage->ExistAttachment(ulSubObjId, PROP_ID(PR_ATTACH_DATA_BIN))) {
			unsigned char *data = NULL;
			size_t temp = 0;
			er = lpAttachmentStorage->LoadAttachment(NULL, ulSubObjId, PROP_ID(PR_ATTACH_DATA_BIN), &temp, &data);
			if (er != erSuccess)
				goto exit;
			ulLen = (unsigned int)temp;
			er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
			if (er != erSuccess) {
				s_free(NULL, data);
				goto exit;
			}
			er = lpSink->Write(data, 1, ulLen);
			s_free(NULL, data);
			if (er != erSuccess)
				goto exit;
		} else {
			er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
			if (er != erSuccess)
				goto exit;
		}

		// start sub objects, can only be 0 or 1
		if (bUseSQLMulti) {
			er = lpStreamDatabase->GetNextResult(&lpDBResultAttachment);
		} else {
			strQuery = "SELECT id, hierarchy.type FROM hierarchy WHERE parent = " + stringify(ulSubObjId) + " LIMIT 1";
			er = lpStreamDatabase->DoSelect(strQuery, &lpDBResultAttachment);
		}
		if (er != erSuccess)
			goto exit;
		/* Force value to 0 or 1, we cannot output more than one submessage. */
		ulLen = lpDBResultAttachment.get_num_rows() >= 1 ? 1 : 0;
		er = lpSink->Write(&ulLen, sizeof(ulLen), 1);
		if (er != erSuccess)
			goto exit;
		lpDBRow = lpDBResultAttachment.fetch_row();
		if (lpDBRow == nullptr)
			continue;
		if (lpDBRow[0] == NULL) {
			er = KCERR_DATABASE_ERROR;
			ec_log_err("SerializeMessage(): column null");
			goto exit;
		}
		ulSubObjType = atoi(lpDBRow[1]);
		er = lpSink->Write(&ulSubObjType, sizeof(ulSubObjType), 1);
		if (er != erSuccess)
			goto exit;
		ulSubObjId = atoi(lpDBRow[0]);
		er = lpSink->Write(&ulSubObjId, sizeof(ulSubObjId), 1);
		if (er != erSuccess)
			goto exit;
		// Recurse into subobject, depth is ignored when not using sql procedures
		er = SerializeMessage(lpecSession, lpStreamDatabase, lpAttachmentStorage, lpStreamCaps, ulSubObjId, ulSubObjType, ulStoreId, lpsGuid, ulFlags, lpSink, false);
		if (er != erSuccess)
			goto exit;
	}

	if(bTop && bUseSQLMulti)
		lpStreamDatabase->FinalizeMulti();

exit:
	if (er != erSuccess)
		ec_log_err("SerializeObject failed with error code 0x%08x for object %d", er, ulObjId );
	FreeChildProps(&mapChildProps);
		
	return er;
}

static ECRESULT DeserializePropVal(struct soap *soap,
    LPCSTREAMCAPS lpStreamCaps, NamedPropertyMapper &namedPropertyMapper,
    propVal **lppsPropval, ECSerializer *lpSource)
{
	ECRESULT er;
	gsoap_size_t ulCount;
	unsigned int	ulLen;
	propVal			*lpsPropval = NULL;
	unsigned char	b;

	GUID			guid = {0};
	unsigned int	ulKind = 0;
	unsigned int	ulNameId = 0;
	std::string		strNameString;
	unsigned int	ulLocalId = 0;

	convert_context	converter;

	lpsPropval = s_alloc<propVal>(soap);
	er = lpSource->Read(&lpsPropval->ulPropTag, sizeof(lpsPropval->ulPropTag), 1);
	if (er != erSuccess)
		return er;

	switch (PROP_TYPE(lpsPropval->ulPropTag)) {
	case PT_I2:
		lpsPropval->__union = SOAP_UNION_propValData_i;
		er = lpSource->Read(&lpsPropval->Value.i, sizeof(lpsPropval->Value.i), 1);
		break;
	case PT_LONG:
		lpsPropval->__union = SOAP_UNION_propValData_ul;
		er = lpSource->Read(&lpsPropval->Value.ul, sizeof(lpsPropval->Value.ul), 1);
		break;
	case PT_R4:
		lpsPropval->__union = SOAP_UNION_propValData_flt;
		er = lpSource->Read(&lpsPropval->Value.flt, sizeof(lpsPropval->Value.flt), 1);
		break;
	case PT_BOOLEAN:
		lpsPropval->__union = SOAP_UNION_propValData_b;
		er = lpSource->Read(&b, sizeof(b), 1);
		lpsPropval->Value.b = (b != 0);
		break;
	case PT_DOUBLE:
	case PT_APPTIME:
		lpsPropval->__union = SOAP_UNION_propValData_dbl;
		er = lpSource->Read(&lpsPropval->Value.dbl, sizeof(lpsPropval->Value.dbl), 1);
		break;
	case PT_CURRENCY:
	case PT_SYSTIME:
		lpsPropval->__union = SOAP_UNION_propValData_hilo;
		lpsPropval->Value.hilo = s_alloc<hiloLong>(soap);
		er = lpSource->Read(&lpsPropval->Value.hilo->hi, sizeof(lpsPropval->Value.hilo->hi), 1);
		if (er == erSuccess)
			er = lpSource->Read(&lpsPropval->Value.hilo->lo, sizeof(lpsPropval->Value.hilo->lo), 1);
		break;
	case PT_I8:
		lpsPropval->__union = SOAP_UNION_propValData_li;
		er = lpSource->Read(&lpsPropval->Value.li, sizeof(lpsPropval->Value.li), 1);
		break;
	case PT_STRING8:
	case PT_UNICODE: {
		lpsPropval->__union = SOAP_UNION_propValData_lpszA;
		er = lpSource->Read(&ulLen, sizeof(ulLen), 1);
		if (er != erSuccess)
			break;
		if (lpStreamCaps->bSupportUnicode) {
			lpsPropval->Value.lpszA = s_alloc<char>(soap, ulLen + 1);
			memset(lpsPropval->Value.lpszA, 0, ulLen + 1);
			er = lpSource->Read(lpsPropval->Value.lpszA, 1, ulLen);
			break;
		}
		std::string strData(ulLen, '\0');
		er = lpSource->Read((void *)strData.data(), 1, ulLen);
		if (er != erSuccess)
			break;
		const utf8string strConverted = converter.convert_to<utf8string>(strData, rawsize(strData), "WINDOWS-1252");
		lpsPropval->Value.lpszA = s_strcpy(soap, strConverted.c_str());
		break;
	}
	case PT_CLSID:
	case PT_BINARY:
		lpsPropval->__union = SOAP_UNION_propValData_bin;
		er = lpSource->Read(&ulLen, sizeof(ulLen), 1);
		if (er != erSuccess)
			break;
		lpsPropval->Value.bin = s_alloc<xsd__base64Binary>(soap);
		lpsPropval->Value.bin->__size = ulLen;
		lpsPropval->Value.bin->__ptr = s_alloc<unsigned char>(soap, ulLen);
		er = lpSource->Read(lpsPropval->Value.bin->__ptr, 1, ulLen);
		break;
	case PT_MV_I2:
		lpsPropval->__union = SOAP_UNION_propValData_mvi;
		er = lpSource->Read(&ulCount, sizeof(ulCount), 1);
		if (er != erSuccess)
			break;
		lpsPropval->Value.mvi.__size = ulCount;
		lpsPropval->Value.mvi.__ptr = s_alloc<short>(soap, ulCount);
		er = lpSource->Read(lpsPropval->Value.mvi.__ptr, sizeof *lpsPropval->Value.mvi.__ptr, ulCount);
		break;
	case PT_MV_LONG:
		lpsPropval->__union = SOAP_UNION_propValData_mvl;
		er = lpSource->Read(&ulCount, sizeof(ulCount), 1);
		if (er != erSuccess)
			break;
		lpsPropval->Value.mvl.__size = ulCount;
		lpsPropval->Value.mvl.__ptr = s_alloc<unsigned int>(soap, ulCount);
		er = lpSource->Read(lpsPropval->Value.mvl.__ptr, sizeof *lpsPropval->Value.mvl.__ptr, ulCount);
		break;
	case PT_MV_R4:
		lpsPropval->__union = SOAP_UNION_propValData_mvflt;
		er = lpSource->Read(&ulCount, sizeof(ulCount), 1);
		if (er != erSuccess)
			break;
		lpsPropval->Value.mvflt.__size = ulCount;
		lpsPropval->Value.mvflt.__ptr = s_alloc<float>(soap, ulCount);
		er = lpSource->Read(lpsPropval->Value.mvflt.__ptr, sizeof *lpsPropval->Value.mvflt.__ptr, ulCount);
		break;
	case PT_MV_DOUBLE:
	case PT_MV_APPTIME:
		lpsPropval->__union = SOAP_UNION_propValData_mvdbl;
		er = lpSource->Read(&ulCount, sizeof(ulCount), 1);
		if (er != erSuccess)
			break;
		lpsPropval->Value.mvdbl.__size = ulCount;
		lpsPropval->Value.mvdbl.__ptr = s_alloc<double>(soap, ulCount);
		er = lpSource->Read(lpsPropval->Value.mvdbl.__ptr, sizeof *lpsPropval->Value.mvdbl.__ptr, ulCount);
		break;
	case PT_MV_CURRENCY:
	case PT_MV_SYSTIME:
		lpsPropval->__union = SOAP_UNION_propValData_mvhilo;
		er = lpSource->Read(&ulCount, sizeof(ulCount), 1);
		if (er != erSuccess)
			break;
		lpsPropval->Value.mvhilo.__size = ulCount;
		lpsPropval->Value.mvhilo.__ptr = s_alloc<hiloLong>(soap, ulCount);
		for (gsoap_size_t x = 0; er == erSuccess && x < ulCount; ++x) {
			er = lpSource->Read(&lpsPropval->Value.mvhilo.__ptr[x].hi, sizeof(lpsPropval->Value.mvhilo.__ptr[x].hi), ulCount);
			if (er != erSuccess)
				continue;
			er = lpSource->Read(&lpsPropval->Value.mvhilo.__ptr[x].lo, sizeof(lpsPropval->Value.mvhilo.__ptr[x].lo), ulCount);
		}
		break;
	case PT_MV_BINARY:
	case PT_MV_CLSID:
		lpsPropval->__union = SOAP_UNION_propValData_mvbin;
		er = lpSource->Read(&ulCount, sizeof(ulCount), 1);
		if (er != erSuccess)
			break;
		lpsPropval->Value.mvbin.__size = ulCount;
		lpsPropval->Value.mvbin.__ptr = s_alloc<xsd__base64Binary>(soap, ulCount);
		for (gsoap_size_t x = 0; er == erSuccess && x < ulCount; ++x) {
			er = lpSource->Read(&ulLen, sizeof(ulLen), 1);
			if (er != erSuccess)
				continue;
			lpsPropval->Value.mvbin.__ptr[x].__size = ulLen;
			lpsPropval->Value.mvbin.__ptr[x].__ptr = s_alloc<unsigned char>(soap, ulLen);
			er = lpSource->Read(lpsPropval->Value.mvbin.__ptr[x].__ptr, 1, ulLen);
		}
		break;
	case PT_MV_STRING8:
	case PT_MV_UNICODE:
		lpsPropval->__union = SOAP_UNION_propValData_mvszA;
		er = lpSource->Read(&ulCount, sizeof(ulCount), 1);
		if (er != erSuccess)
			break;
		lpsPropval->Value.mvszA.__size = ulCount;
		lpsPropval->Value.mvszA.__ptr = s_alloc<char*>(soap, ulCount);
		for (gsoap_size_t x = 0; er == erSuccess && x < ulCount; ++x) {
			er = lpSource->Read(&ulLen, sizeof(ulLen), 1);
			if (er != erSuccess)
				continue;
			if (lpStreamCaps->bSupportUnicode) {
				lpsPropval->Value.mvszA.__ptr[x] = s_alloc<char>(soap, ulLen + 1);
				memset(lpsPropval->Value.mvszA.__ptr[x], 0, ulLen + 1);
				er = lpSource->Read(lpsPropval->Value.mvszA.__ptr[x], 1, ulLen);
				continue;
			}
			std::string strData(ulLen, '\0');
			er = lpSource->Read((void*)strData.data(), 1, ulLen);
			if (er != erSuccess)
				continue;
			const utf8string strConverted = converter.convert_to<utf8string>(strData, rawsize(strData), "WINDOWS-1252");
			lpsPropval->Value.mvszA.__ptr[x] = s_strcpy(soap, strConverted.c_str());
		}
		break;
	case PT_MV_I8:
		lpsPropval->__union = SOAP_UNION_propValData_mvli;
		er = lpSource->Read(&ulCount, sizeof(ulCount), 1);
		if (er != erSuccess)
			break;
		lpsPropval->Value.mvli.__size = ulCount;
		lpsPropval->Value.mvli.__ptr = s_alloc<LONG64>(soap, ulCount);
		er = lpSource->Read(lpsPropval->Value.mvli.__ptr, sizeof *lpsPropval->Value.mvli.__ptr, ulCount);
		break;
	default:
		return KCERR_INVALID_TYPE;
	}

	// If the proptag is in the dynamic named property range, we need to get the correct local proptag
	if (PROP_ID(lpsPropval->ulPropTag) > 0x8500) {
		er = lpSource->Read(&guid, 1, sizeof(guid));
		if (er == erSuccess)
			er = lpSource->Read(&ulKind, sizeof(ulKind), 1);
		if (er == erSuccess && ulKind == MNID_ID) {
			er = lpSource->Read(&ulNameId, sizeof(ulNameId), 1);
			if (er == erSuccess)
				er = namedPropertyMapper.GetId(guid, ulNameId, &ulLocalId);
		} else if (er == erSuccess && ulKind == MNID_STRING) {
			er = lpSource->Read(&ulLen, sizeof(ulLen), 1);
			if (er == erSuccess) {
				strNameString.resize(ulLen, 0);
				er = lpSource->Read((void*)strNameString.data(), 1, ulLen);
			}
			if (er == erSuccess)
				er = namedPropertyMapper.GetId(guid, strNameString, &ulLocalId);
		}
		if (er == erSuccess)
			lpsPropval->ulPropTag = PROP_TAG(PROP_TYPE(lpsPropval->ulPropTag), ulLocalId);
	}

	*lppsPropval = lpsPropval;
	return er;
}

static ECRESULT DeserializeProps(ECSession *lpecSession, ECDatabase *lpDatabase,
    ECAttachmentStorage *lpAttachmentStorage, const StreamCaps *lpStreamCaps,
    unsigned int ulObjId, unsigned int ulObjType, unsigned int ulStoreId,
    GUID *lpsGuid, bool bNewItem, ECSerializer *lpSource,
    struct propValArray **lppPropValArray)
{
	ECRESULT		er = erSuccess;
	unsigned int	ulCount = 0;
	unsigned int	ulFlags = 0;
	unsigned int	ulParentId = 0;
	unsigned int	ulOwner = 0;
	unsigned int	ulParentType = 0;
	gsoap_size_t nMVItems = 0;
	unsigned int	ulAffected = 0;
	unsigned int	ulLen = 0;
	propVal			*lpsPropval = NULL;
	struct soap		*soap = NULL;
	struct propValArray *lpPropValArray = NULL;
	NamedPropertyMapper namedPropertyMapper(lpDatabase);

	std::string		strQuery;
	std::string		strInsertQuery;
	std::string		strInsertTProp;
	std::string		strColData;
	std::string		strColName;

	SOURCEKEY		sSourceKey;
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow = NULL;
	auto gcache = g_lpSessionManager->GetCacheManager();

	std::set<unsigned int>				setInserted;

	if (!lpDatabase) {
		er = KCERR_DATABASE_ERROR;
		goto exit;
	}

	if (!lpAttachmentStorage) {
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	er = gcache->GetObject(ulObjId, &ulParentId, &ulOwner, &ulFlags, nullptr);
	if (er != erSuccess)
		goto exit;
	er = gcache->GetObject(ulParentId, nullptr, nullptr, nullptr, &ulParentType);
	if (er != erSuccess)
		goto exit;

	// Get the number of properties
	er = lpSource->Read(&ulCount, sizeof(ulCount), 1);
	if (er != erSuccess)
		goto exit;
		
	if (lppPropValArray) {
		// If requested we can store upto ulCount properties. Currently we won't store them all though.
		// Note that we test on lppPropValArray but allocate lpPropValArray. We'll assign that to
		// *lppPropValArray later if all went well.
		lpPropValArray = s_alloc<struct propValArray>(NULL);
		
		lpPropValArray->__ptr = s_alloc<struct propVal>(NULL, ulCount);
		memset(lpPropValArray->__ptr, 0, sizeof(struct propVal) * ulCount);
		
		lpPropValArray->__size = 0;
	}

	for (unsigned i = 0; i < ulCount; ++i) {
		// We'll (ab)use a soap structure as a memory pool.
		assert(soap == NULL);
		soap = soap_new();

		er = DeserializePropVal(soap, lpStreamCaps, namedPropertyMapper, &lpsPropval, lpSource);
		if (er != erSuccess)
			goto exit;

		auto iterInserted = setInserted.find(lpsPropval->ulPropTag);
		if (iterInserted != setInserted.cend())
			goto next_property;

		if (ECGenProps::IsPropRedundant(lpsPropval->ulPropTag, ulObjType) == erSuccess)
			goto next_property;

		// Same goes for flags in PR_MESSAGE_FLAGS
		if (lpsPropval->ulPropTag == PR_MESSAGE_FLAGS) {
		    // ulFlags is obtained from the hierarchy table, which should only contain
		    // 'unsettable' flags
			assert((ulFlags & ~MSGFLAG_UNSETTABLE) == 0);

			// Normalize PR_MESSAGE_FLAGS so that the user cannot change flags that are also
			// stored in the hierarchy table.
		    lpsPropval->Value.ul = (lpsPropval->Value.ul & ~MSGFLAG_UNSETTABLE) | ulFlags;

			if (lpPropValArray)
				CopyPropVal(lpsPropval, lpPropValArray->__ptr + lpPropValArray->__size++);
		}

		// Make sure we dont have a colliding PR_SOURCE_KEY. This can happen if a user imports an exported message for example.
		if (lpsPropval->ulPropTag == PR_SOURCE_KEY) {
			// don't use the sourcekey if found.
			// Don't query the cache as that can be out of sync with the db in rare occasions.
			strQuery = 
				"SELECT hierarchyid FROM indexedproperties "
					"WHERE tag=" + stringify(PROP_ID(PR_SOURCE_KEY)) + 
					" AND val_binary=" + lpDatabase->EscapeBinary(lpsPropval->Value.bin->__ptr, lpsPropval->Value.bin->__size);
			
			er = lpDatabase->DoSelect(strQuery, &lpDBResult);
			if(er != erSuccess)
				goto exit;

			lpDBRow = lpDBResult.fetch_row();
			// We can't use lpDBRow here except for checking if it was NULL.
			if (lpDBRow != NULL)
				continue;

			strQuery = "REPLACE INTO indexedproperties(hierarchyid,tag,val_binary) VALUES (" + stringify(ulObjId) + "," + stringify(PROP_ID(PR_SOURCE_KEY)) + "," + lpDatabase->EscapeBinary(lpsPropval->Value.bin->__ptr, lpsPropval->Value.bin->__size) + ")";
			er = lpDatabase->DoInsert(strQuery);
			if (er != erSuccess)
				goto exit;
			setInserted.emplace(lpsPropval->ulPropTag);
			gcache->SetObjectProp(PROP_ID(PR_SOURCE_KEY), lpsPropval->Value.bin->__size, lpsPropval->Value.bin->__ptr, ulObjId);
			goto next_property;
		}

		if (PROP_TYPE(lpsPropval->ulPropTag) & MV_FLAG) {
			nMVItems = GetMVItemCount(lpsPropval);
			for (gsoap_size_t j = 0; j < nMVItems; ++j) {
				er = CopySOAPPropValToDatabaseMVPropVal(lpsPropval, j, strColName, strColData, lpDatabase);
				if (er != erSuccess) {
					er = erSuccess;
					goto next_property;
				}

				strQuery = "REPLACE INTO mvproperties(hierarchyid,orderid,tag,type," + strColName + ") VALUES(" + stringify(ulObjId) + "," + stringify(j) + "," + stringify(PROP_ID(lpsPropval->ulPropTag)) + "," + stringify(PROP_TYPE(lpsPropval->ulPropTag)) + "," + strColData + ")";
				er = lpDatabase->DoInsert(strQuery, NULL, &ulAffected);
				if (er != erSuccess)
					goto exit;
				if (ulAffected != 1) {
					er = KCERR_DATABASE_ERROR;
					ec_log_err("DeserializeProps(): Unexpected affected row count");
					goto exit;
				}
			}
			
		} else {
			// Write the property to the database
			er = WriteSingleProp(lpDatabase, ulObjId, ulParentId, lpsPropval, false, lpDatabase->GetMaxAllowedPacket(), strInsertQuery);
			if (er == KCERR_TOO_BIG) {
				er = lpDatabase->DoInsert(strInsertQuery);
				if (er == erSuccess) {
					strInsertQuery.clear();
					er = WriteSingleProp(lpDatabase, ulObjId, ulParentId, lpsPropval, false, lpDatabase->GetMaxAllowedPacket(), strInsertQuery);
				}
			}
			if (er != erSuccess)
				goto exit;
			
			// Write the property to the table properties if needed (only on objects in folders (folders, messages), and if the property is being tracked here.
			if (ulParentType == MAPI_FOLDER) {
				// Cache the written value
				sObjectTableKey key(ulObjId, 0);
				gcache->SetCell(&key, lpsPropval->ulPropTag, lpsPropval);
			}
		}

		setInserted.emplace(lpsPropval->ulPropTag);
next_property:
		soap_destroy(soap);
		soap_end(soap);
		soap_free(soap);
		soap = NULL;
	}

	if (!strInsertQuery.empty()) {
		er = lpDatabase->DoInsert(strInsertQuery);
		if (er != erSuccess)
			goto exit;
	}

	if(ulParentType == MAPI_FOLDER && ulParentId != CACHE_NO_PARENT) {
		// Instead of writing directly to tproperties, save a delayed write request (flushed on table open).
		er = ECTPropsPurge::AddDeferredUpdateNoPurge(lpDatabase, ulParentId, 0, ulObjId);
		if(er != erSuccess)
			goto exit;
	}

	if (bNewItem && ulParentType == MAPI_FOLDER && RealObjType(ulObjType, ulParentType) == MAPI_MESSAGE) {
		auto iterInserted = setInserted.find(PR_SOURCE_KEY);
		if (iterInserted == setInserted.cend()) {
			er = lpecSession->GetNewSourceKey(&sSourceKey);
			if (er != erSuccess)
				goto exit;

			strQuery = "INSERT INTO indexedproperties(hierarchyid,tag,val_binary) VALUES(" + stringify(ulObjId) + "," + stringify(PROP_ID(PR_SOURCE_KEY)) + "," + lpDatabase->EscapeBinary(sSourceKey, sSourceKey.size()) + ")";
			er = lpDatabase->DoInsert(strQuery);
			if (er != erSuccess)
				goto exit;
			gcache->SetObjectProp(PROP_ID(PR_SOURCE_KEY), sSourceKey.size(), sSourceKey, ulObjId);
		}
	}

	if (RealObjType(ulObjType, ulParentType) == MAPI_ATTACH) {

		er = lpSource->Read(&ulLen, sizeof(ulLen), 1);
		if (er != erSuccess)
			goto exit;

		// We don't require the instance id, since we have no way of returning the instance id of this new object to the client.
		er = lpAttachmentStorage->SaveAttachment(ulObjId, PROP_ID(PR_ATTACH_DATA_BIN), true, ulLen, lpSource, NULL);
		if (er != erSuccess)
			goto exit;
	}

	if (!bNewItem)
		gcache->Update(fnevObjectModified, ulObjId);
	gcache->SetObject(ulObjId, ulParentId, ulOwner, ulFlags, ulObjType);
	if (lpPropValArray) {
		assert(lppPropValArray != NULL);
		*lppPropValArray = lpPropValArray;
		lpPropValArray = NULL;
	}

exit:
	if (lpPropValArray)
		FreePropValArray(lpPropValArray, true);

	if (soap) {
		soap_destroy(soap);
		soap_end(soap);
		soap_free(soap);
	}

	return er;
}

ECRESULT DeserializeObject(ECSession *lpecSession, ECDatabase *lpDatabase, ECAttachmentStorage *lpAttachmentStorage, LPCSTREAMCAPS lpStreamCaps, unsigned int ulObjId, unsigned int ulStoreId, GUID *lpsGuid, bool bNewItem, unsigned long long ullIMAP, ECSerializer *lpSource, struct propValArray **lppPropValArray)
{
	ECRESULT		er = erSuccess;
	unsigned int	ulStreamVersion = 0;
	unsigned int	ulObjType = 0;
	unsigned int	ulRealObjType = 0;
	unsigned int	ulParentId = 0;
	unsigned int	ulParentType = 0;
	unsigned int	ulSize =0 ;
	struct propValArray *lpPropValArray = NULL;
	std::string		strQuery;
	auto gcache = g_lpSessionManager->GetCacheManager();

	if (!lpDatabase) {
		er = KCERR_DATABASE_ERROR;
		goto exit;
	}
	er = gcache->GetObject(ulObjId, &ulParentId, nullptr, nullptr, &ulObjType);
	if (er != erSuccess)
		goto exit;
	er = gcache->GetObject(ulParentId, nullptr, nullptr, nullptr, &ulParentType);
	if (er != erSuccess)
		goto exit;
		
	// Normalize the object type, but keep the original for storing in the db
	ulRealObjType = RealObjType(ulObjType, ulParentType);

	if (ulRealObjType != MAPI_MESSAGE && ulRealObjType != MAPI_ATTACH && ulRealObjType != MAPI_MAILUSER && ulRealObjType != MAPI_DISTLIST) {
		er = KCERR_NO_SUPPORT;
		goto exit;
	}

	if (lpStreamCaps == NULL) {
		er = lpSource->Read(&ulStreamVersion, sizeof(ulStreamVersion), 1);
		if (er != erSuccess)
			goto exit;
		if (ulStreamVersion >= ARRAY_SIZE(g_StreamCaps)) {
			er = KCERR_NO_SUPPORT;
			goto exit;
		}

		lpStreamCaps = &g_StreamCaps[ulStreamVersion];
	}

	er = DeserializeProps(lpecSession, lpDatabase, lpAttachmentStorage, lpStreamCaps, ulObjId, ulObjType, ulStoreId, lpsGuid, bNewItem, lpSource, lppPropValArray ? &lpPropValArray : NULL);
	if (er != erSuccess)
		goto exit;

	if (ulParentType == MAPI_FOLDER) {
		sObjectTableKey key(ulObjId, 0);
		propVal sProp;

		if (bNewItem) {
			er = g_lpSessionManager->GetNewSequence(ECSessionManager::SEQ_IMAP, &ullIMAP);
			if (er != erSuccess)
				goto exit;
		}

		strQuery = "INSERT INTO properties(hierarchyid, tag, type, val_ulong) VALUES(" +
						stringify(ulObjId) + "," +
						stringify(PROP_ID(PR_EC_IMAP_ID)) + "," +
						stringify(PROP_TYPE(PR_EC_IMAP_ID)) + "," +
						stringify_int64(ullIMAP) + ")";
		er = lpDatabase->DoInsert(strQuery);
		if (er != erSuccess)
			goto exit;

		sProp.ulPropTag = PR_EC_IMAP_ID;
		sProp.Value.ul = (unsigned int)ullIMAP;
		sProp.__union = SOAP_UNION_propValData_ul;
		er = gcache->SetCell(&key, PR_EC_IMAP_ID, &sProp);
		if (er != erSuccess)
			goto exit;
	}

	if (ulRealObjType == MAPI_MESSAGE || ulRealObjType == MAPI_ATTACH) {
		unsigned int	ulSubObjCount = 0;
		unsigned int	ulSubObjId = 0;
		unsigned int	ulSubObjType = 0;
		BOOL			fHasAttach = FALSE;
		
		er = lpSource->Read(&ulSubObjCount, sizeof(ulSubObjCount), 1);
		if (er != erSuccess)
			goto exit;

		for (unsigned i = 0; i < ulSubObjCount; ++i) {
			er = lpSource->Read(&ulSubObjType, sizeof(ulSubObjType), 1);
			if (er != erSuccess)
				goto exit;

			if (RealObjType(ulSubObjType, ulRealObjType) == MAPI_ATTACH)
				fHasAttach = TRUE;

			er = lpSource->Read(&ulSubObjId, sizeof(ulSubObjId), 1);
			if (er != erSuccess)
				goto exit;

			/**
			 * For new items we're not interested in the ulSubObjId from the stream, we do need
			 * to create the object with the current object as its parent
			 **/
			er = CreateObject(lpecSession, lpDatabase, ulObjId, ulObjType, ulSubObjType, 0, &ulSubObjId);
			if (er != erSuccess)
				goto exit;

			er = DeserializeObject(lpecSession, lpDatabase, lpAttachmentStorage, lpStreamCaps, ulSubObjId, ulStoreId, lpsGuid, bNewItem, 0, lpSource, NULL);
			if (er != erSuccess)
				goto exit;
		}

		if (ulRealObjType == MAPI_MESSAGE) {
			// We have to generate/update PR_HASATTACH
			
			sObjectTableKey key(ulObjId, 0);
			std::string strQuery;
			struct propVal sPropHasAttach;
			sPropHasAttach.ulPropTag = PR_HASATTACH;
			sPropHasAttach.Value.b = (fHasAttach != FALSE);
			sPropHasAttach.__union = SOAP_UNION_propValData_b;
	
			// Write in properties		
			strQuery.clear();
			WriteSingleProp(lpDatabase, ulObjId, ulParentId, &sPropHasAttach, false, 0, strQuery);
			er = lpDatabase->DoInsert(strQuery);
			if(er != erSuccess)
				goto exit;
				
			// Write in tproperties
			strQuery.clear();
			WriteSingleProp(lpDatabase, ulObjId, ulParentId, &sPropHasAttach, true, 0, strQuery);
			er = lpDatabase->DoInsert(strQuery);
			if(er != erSuccess)
				goto exit;
			
			// Update cache, since it may have been written before by WriteProps with a possibly wrong value
			gcache->SetCell(&key, PR_HASATTACH, &sPropHasAttach);
			
			// Update MSGFLAG_HASATTACH in the same way. We can assume PR_MESSAGE_FLAGS is already available, so we
			// just do an update (instead of REPLACE INTO)
			strQuery = std::string("UPDATE properties SET val_ulong = val_ulong ") + (fHasAttach ? " | 16 " : " & ~16") + " WHERE hierarchyid = " + stringify(ulObjId) + " AND tag = " + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " AND type = " + stringify(PROP_TYPE(PR_MESSAGE_FLAGS));
			er = lpDatabase->DoUpdate(strQuery);
			if(er != erSuccess)
				goto exit;
				
			// Update cache if it's actually in the cache
			if (gcache->GetCell(&key, PR_MESSAGE_FLAGS, &sPropHasAttach, nullptr, false) == erSuccess) {
				sPropHasAttach.Value.ul &= ~MSGFLAG_HASATTACH;
				sPropHasAttach.Value.ul |= fHasAttach ? MSGFLAG_HASATTACH : 0;
				gcache->SetCell(&key, PR_MESSAGE_FLAGS, &sPropHasAttach);
			}
		}

		// Calc size of object, now that all children are saved.
		// Add new size
		if (CalculateObjectSize(lpDatabase, ulObjId, ulObjType, &ulSize) == erSuccess) {
			er = UpdateObjectSize(lpDatabase, ulObjId, ulObjType, UPDATE_SET, ulSize);
			if (er != erSuccess)
				goto exit;

			if (ulRealObjType == MAPI_MESSAGE && ulParentType == MAPI_FOLDER) {
				er = UpdateObjectSize(lpDatabase, ulStoreId, MAPI_STORE, UPDATE_ADD, ulSize);
				if (er != erSuccess)
					goto exit;
			}

		} else {
			assert(false);
		}
	}
	
	if (lpPropValArray) {
		assert(lppPropValArray != NULL);
		*lppPropValArray = lpPropValArray;
		lpPropValArray = NULL;
	}

exit:
	if (er != erSuccess) {
		lpSource->Flush(); // Flush the whole stream
		ec_log_err("DeserializeObject failed with error code 0x%08x %s", er, GetMAPIErrorMessage(kcerr_to_mapierr(er, ~0U /* anything that yields UNKNOWN */)));
	}	

	if (lpPropValArray)
		FreePropValArray(lpPropValArray, true);

	return er;
}

static ECRESULT GetValidatedPropType(DB_ROW lpRow, unsigned int *lpulType)
{
	ECRESULT er = KCERR_DATABASE_ERROR;
	unsigned int ulType = 0;

	if (lpRow == NULL || lpulType == NULL)
		return KCERR_INVALID_PARAMETER;

	ulType = atoi(lpRow[FIELD_NR_TYPE]);
	switch (ulType) {
	case PT_I2:
		if (lpRow[FIELD_NR_ULONG] == NULL)
			return er;
		break;
	case PT_LONG:
		if (lpRow[FIELD_NR_ULONG] == NULL)
			return er;
		break;
	case PT_R4:
		if (lpRow[FIELD_NR_DOUBLE] == NULL)
			return er;
		break;
	case PT_BOOLEAN:
		if (lpRow[FIELD_NR_ULONG] == NULL)
			return er;
		break;
	case PT_DOUBLE:
	case PT_APPTIME:
		if (lpRow[FIELD_NR_DOUBLE] == NULL)
			return er;
		break;
	case PT_CURRENCY:
	case PT_SYSTIME:
		if (lpRow[FIELD_NR_HI] == NULL || lpRow[FIELD_NR_LO] == NULL) 
			return er;
		break;
	case PT_I8:
		if (lpRow[FIELD_NR_LONGINT] == NULL)
			return er;
		break;
	case PT_STRING8:
	case PT_UNICODE:
		if (lpRow[FIELD_NR_STRING] == NULL) 
			return er;
		break;
	case PT_CLSID:
	case PT_BINARY:
		if (lpRow[FIELD_NR_BINARY] == NULL) 
			return er;
		break;
	case PT_MV_I2:
		if (lpRow[FIELD_NR_ULONG] == NULL) 
			return er;
		break;
	case PT_MV_LONG:
		if (lpRow[FIELD_NR_ULONG] == NULL) 
			return er;
		break;
	case PT_MV_R4:
		if (lpRow[FIELD_NR_DOUBLE] == NULL) 
			return er;
		break;
	case PT_MV_DOUBLE:
	case PT_MV_APPTIME:
		if (lpRow[FIELD_NR_DOUBLE] == NULL) 
			return er;
		break;
	case PT_MV_CURRENCY:
	case PT_MV_SYSTIME:
		if (lpRow[FIELD_NR_HI] == NULL || lpRow[FIELD_NR_LO] == NULL) 
			return er;
		break;
	case PT_MV_BINARY:
	case PT_MV_CLSID:
		if (lpRow[FIELD_NR_BINARY] == NULL) 
			return er;
		break;
	case PT_MV_STRING8:
	case PT_MV_UNICODE:
		if (lpRow[FIELD_NR_STRING] == NULL) 
			return er;
		break;
	case PT_MV_I8:
		if (lpRow[FIELD_NR_LONGINT] == NULL) 
			return er;
		break;
	default:
		return KCERR_INVALID_TYPE;
	}
	*lpulType = ulType;
	return erSuccess;
}

} /* namespace */
