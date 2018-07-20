/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef postsaveaction_INCLUDED
#define postsaveaction_INCLUDED

#include <kopano/zcdefs.h>
#include <memory>

namespace KC { namespace operations {

/**
 * This interface defines an object that performs arbitrary operations
 * once a particular object has been saved.
 */
class IPostSaveAction {
public:
	virtual ~IPostSaveAction(void) = default;
	virtual HRESULT Execute() = 0;
};
typedef std::shared_ptr<IPostSaveAction> PostSaveActionPtr;

}} /* namespace */

#endif // ndef postsaveaction_INCLUDED
