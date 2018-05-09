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
#include <kopano/memory.hpp>

namespace KC {

class ECLogger;

class ECSyncLog final {
public:
	static HRESULT GetLogger(ECLogger **);
	static HRESULT SetLogger(ECLogger *annoyingswig);

private:
	static std::mutex s_hMutex;
	static object_ptr<ECLogger> s_lpLogger;

	struct _kc_hidden initializer _kc_final {
		~initializer();
	};
	static initializer xinit;
};

} /* namespace */

#endif // ndef ECSYNCLOG_INCLUDED
