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

#ifndef EC_STATS_COLLECTOR_H
#define EC_STATS_COLLECTOR_H

#include <kopano/zcdefs.h>
#include <string>
#include <map>
#include <pthread.h>

#include <kopano/IECStatsCollector.h>

typedef union _statdata {
	float f;
	LONGLONG ll;
	time_t ts;
} SCData;

enum SCType { SCDT_FLOAT, SCDT_LONGLONG, SCDT_TIMESTAMP };

typedef struct _ECStat {
	SCData data;
	LONGLONG avginc;
	SCType type;
	const char *name;
	const char *description;
	pthread_mutex_t lock;
} ECStat;

typedef std::map<SCName, ECStat> SCMap;

typedef struct _ECStrings {
	std::string description;
	std::string value;
} ECStrings;

class ECStatsCollector _zcp_final : public IECStatsCollector {
public:
	ECStatsCollector();
	~ECStatsCollector();

	void Increment(SCName name, float inc) _zcp_override;
	void Increment(SCName name, int inc = 1) _zcp_override;
	void Increment(SCName name, LONGLONG inc) _zcp_override;

	void Set(SCName name, float set) _zcp_override;
	void Set(SCName name, LONGLONG set) _zcp_override;
	void SetTime(SCName name, time_t set) _zcp_override;

	void Min(SCName name, float min) _zcp_override;
	void Min(SCName name, LONGLONG min) _zcp_override;
	void MinTime(SCName name, time_t min) _zcp_override;

	void Max(SCName name, float max) _zcp_override;
	void Max(SCName name, LONGLONG max) _zcp_override;
	void MaxTime(SCName name, time_t max) _zcp_override;

	void Avg(SCName name, float add) _zcp_override;
	void Avg(SCName name, LONGLONG add) _zcp_override;
	void AvgTime(SCName name, time_t add) _zcp_override;

	/* strings are separate, used by ECSerial */
	void Set(const std::string &name, const std::string &description, const std::string &value) _zcp_override;
	void Remove(const std::string &name) _zcp_override;

	std::string GetValue(SCMap::const_iterator iSD);
	std::string GetValue(SCName name) _zcp_override;

	void ForEachStat(void(callback)(const std::string &, const std::string &, const std::string &, void*), void *obj);
	void ForEachString(void(callback)(const std::string &, const std::string &, const std::string &, void*), void *obj);

	void Reset(void) _zcp_override;
	void Reset(SCName name) _zcp_override;

private:
	void AddStat(SCName index, SCType type, const char *name, const char *description);

	SCMap m_StatData;
	pthread_mutex_t m_StringsLock;
	std::map<std::string, ECStrings> m_StatStrings;
};

/* actual variable is in ECServerEntryPoint.cpp */
extern ECStatsCollector* g_lpStatsCollector;

#endif
