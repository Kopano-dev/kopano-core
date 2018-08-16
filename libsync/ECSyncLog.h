/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
