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

#ifndef __STATSCLIENT_H__
#define __STATSCLIENT_H__

#include <kopano/zcdefs.h>
#include <map>
#include <string>
#include <ctime>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <kopano/ECLogger.h>

class StatsClient _zcp_final {
private:
	int fd;
	struct sockaddr_un addr;
	int addr_len;
	ECLogger *const logger;

	pthread_t countsSubmitThread;
public:
	volatile bool terminate; // older compilers don't do atomic_bool
	pthread_mutex_t mapsLock;
	std::map<std::string, double> countsMapDouble;
	std::map<std::string, int64_t> countsMapInt64;

public:
	StatsClient(const std::string & collectorSocket, ECLogger *const logger);
	~StatsClient();

	inline ECLogger *const getLogger() { return logger; }

	void countInc(const std::string & key, const std::string & key_sub);
	void countAdd(const std::string & key, const std::string & key_sub, const double n);
	void countAdd(const std::string & key, const std::string & key_sub, const int64_t n);

	void submit(const std::string & key, const time_t ts, const double value);
	void submit(const std::string & key, const time_t ts, const int64_t value);
};

#endif
