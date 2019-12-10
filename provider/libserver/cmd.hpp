/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef KC_CMD_HPP
#define KC_CMD_HPP 1

#include <memory>
#include <kopano/zcdefs.h>
#include <kopano/ECThreadPool.h>

namespace KC {

extern _kc_export void *SoftDeleteRemover(void *);

class _kc_export ksrv_tpool : public ECThreadPool {
	public:
	using ECThreadPool::ECThreadPool;
	std::unique_ptr<ECThreadWorker> make_worker() override;
};

} /* namespace */

#endif /* KC_CMD_HPP */
