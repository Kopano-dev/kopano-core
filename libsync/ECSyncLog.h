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

#ifndef ECSYNCLOG_INCLUDED
#define ECSYNCLOG_INCLUDED

#include <mutex>
#include <kopano/zcdefs.h>
#include "ECLibSync.h"
#include <pthread.h>

class ECLogger;

class ECSyncLog _kc_final {
public:
	static HRESULT ECLIBSYNC_API GetLogger(ECLogger **lppLogger);
	static HRESULT ECLIBSYNC_API SetLogger(ECLogger *lpLogger);

private:
	static std::mutex s_hMutex;
	static ECLogger			*s_lpLogger;

	struct __initializer {
		~__initializer();
	};
	static __initializer __i;
};

#endif // ndef ECSYNCLOG_INCLUDED
