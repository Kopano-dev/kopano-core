/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <memory>

namespace KC { namespace operations {

/**
 * This interface defines an object that performs arbitrary operations
 * once a particular object has been saved.
 */
class IPostSaveAction {
public:
	virtual ~IPostSaveAction() = default;
	virtual HRESULT Execute() = 0;
};

}} /* namespace */
