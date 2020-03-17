/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/**
 * MAPIErrors.h
 * Declarations of GetMAPIErrorMessage() and supporting data structures and
 * constants
 */

#ifndef MAPIERRORS_H_INCLUDED
#define MAPIERRORS_H_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/platform.h>	// for declaration of HRESULT
#include <string>

namespace KC {

extern KC_EXPORT const char *GetMAPIErrorMessage(HRESULT);
extern KC_EXPORT std::string getMapiCodeString(HRESULT, const char *object = "object");

} /* namespace */

#endif // !defined MAPIERRORS_H_INCLUDED

