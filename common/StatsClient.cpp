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
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>

#include "StatsClient.h"
#include "TmpPath.h"

static void submitThreadDo(void *p)
{
	StatsClient *const psc = (StatsClient *)p;

	psc -> getLogger() -> Log(EC_LOGLEVEL_DEBUG, "Push data");

	time_t now = time(NULL);

		pthread_mutex_lock(&psc -> mapsLock);

	std::map<std::string, double>::iterator doubleIterator = psc -> countsMapDouble.begin();
		for (; doubleIterator != psc->countsMapDouble.end(); ++doubleIterator) {
		std::string key = doubleIterator -> first;
			double v = doubleIterator -> second;

		psc -> submit(key, now, v);
		}

		psc -> countsMapDouble.clear();

	std::map<std::string, int64_t>::iterator int64Iterator = psc -> countsMapInt64.begin();
		for (; int64Iterator != psc->countsMapInt64.end(); ++int64Iterator) {
		std::string key = int64Iterator -> first;
			int64_t v = int64Iterator -> second;

		psc -> submit(key, now, v);
		}

		psc -> countsMapInt64.clear();

		pthread_mutex_unlock(&psc -> mapsLock);
	}

static void *submitThread(void *p)
{
	StatsClient *const psc = (StatsClient *)p;

	psc -> getLogger() -> Log(EC_LOGLEVEL_DEBUG, "Submit thread started");

	pthread_cleanup_push(submitThreadDo, p);

	while(!psc -> terminate) {
		sleep(300);

		submitThreadDo(p);
	}

	pthread_cleanup_pop(1);

	psc -> getLogger() -> Log(EC_LOGLEVEL_DEBUG, "Submit thread stopping");

	return NULL;
}

StatsClient::StatsClient(ECLogger *l) :
	fd(-1), thread_running(false), logger(l), terminate(false)
{
	pthread_mutex_init(&mapsLock, NULL);
}

int StatsClient::startup(const std::string &collectorSocket)
{
	int ret = -1;

	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd == -1) {
		logger -> Log(EC_LOGLEVEL_ERROR, "StatsClient cannot create socket: %s", strerror(errno));
		return -errno; /* maybe log a bit */
	}

	srand(time(NULL));

	logger -> Log(EC_LOGLEVEL_DEBUG, "StatsClient binding socket");

	for (unsigned int retry = 0; retry < 3; ++retry) {
		struct sockaddr_un laddr;
		memset(&laddr, 0, sizeof(laddr));
		laddr.sun_family = AF_UNIX;
		sprintf(laddr.sun_path, "%s/.%x%x.sock", TmpPath::getInstance() -> getTempPath().c_str(), rand(), rand());

		int ret = bind(fd, reinterpret_cast<const struct sockaddr *>(&laddr), sizeof(sa_family_t) + strlen(laddr.sun_path) + 1);
		if (ret == 0) {
			logger -> Log(EC_LOGLEVEL_DEBUG, "StatsClient bound socket to %s", laddr.sun_path);

			unlink(laddr.sun_path);
			break;
		}
		ret = -errno;
		ec_log_err("StatsClient bind %s: %s", laddr.sun_path, strerror(errno));
		if (ret == -EADDRINUSE)
			return ret;
	}
	if (ret != 0)
		return ret;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, collectorSocket.c_str());

	addr_len = sizeof(sa_family_t) + collectorSocket.size() + 1;

	if (pthread_create(&countsSubmitThread, NULL, submitThread, this) == 0)
		thread_running = true;

	logger -> Log(EC_LOGLEVEL_DEBUG, "StatsClient thread started");
	return 0;
}

StatsClient::~StatsClient() {
	logger -> Log(EC_LOGLEVEL_DEBUG, "StatsClient terminating");

	terminate = true;
	if (thread_running) {
		// interrupt sleep()
		pthread_cancel(countsSubmitThread);
		void *dummy = NULL;
		pthread_join(countsSubmitThread, &dummy);
	}
	pthread_mutex_destroy(&mapsLock);

	closesocket(fd);

	logger -> Log(EC_LOGLEVEL_DEBUG, "StatsClient terminated");
}

void StatsClient::submit(const std::string & key, const time_t ts, const double value) {
	if (fd == -1)
		return;

	char msg[4096];
	int len = snprintf(msg, sizeof msg, "ADD float %s %ld %f", key.c_str(), ts, value); 

	// in theory snprintf can return -1
	if (len > 0) {
		int rc = sendto(fd, msg, len, 0, (struct sockaddr *)&addr, addr_len);

		if (rc == -1)
			logger -> Log(EC_LOGLEVEL_DEBUG, "StatsClient submit float failed: %s", strerror(errno));
	}
}

void StatsClient::submit(const std::string & key, const time_t ts, const int64_t value) {
	if (fd == -1)
		return;

	char msg[4096];
	int len = snprintf(msg, sizeof msg, "ADD int %s %ld %ld", key.c_str(), ts, value); 

	// in theory snprintf can return -1
	if (len > 0) {
		int rc = sendto(fd, msg, len, 0, (struct sockaddr *)&addr, addr_len);

		if (rc == -1)
			logger -> Log(EC_LOGLEVEL_DEBUG, "StatsClient submit int failed: %s", strerror(errno));
	}
}

void StatsClient::countInc(const std::string & key, const std::string & key_sub) {
	countAdd(key, key_sub, int64_t(1));
}

void StatsClient::countAdd(const std::string & key, const std::string & key_sub, const double n) {
	std::string kp = key + " " + key_sub;

	pthread_mutex_lock(&mapsLock);

	std::map<std::string, double>::iterator doubleIterator = countsMapDouble.find(kp);

	if (doubleIterator == countsMapDouble.end())
		countsMapDouble.insert(std::pair<std::string, double>(kp, n));
	else
		doubleIterator -> second += n;

	pthread_mutex_unlock(&mapsLock);
}

void StatsClient::countAdd(const std::string & key, const std::string & key_sub, const int64_t n) {
	std::string kp = key + " " + key_sub;

	pthread_mutex_lock(&mapsLock);

	std::map<std::string, int64_t>::iterator int64Iterator = countsMapInt64.find(kp);

	if (int64Iterator == countsMapInt64.end())
		countsMapInt64.insert(std::pair<std::string, int64_t>(kp, n));
	else
		int64Iterator -> second += n;

	pthread_mutex_unlock(&mapsLock);
}
