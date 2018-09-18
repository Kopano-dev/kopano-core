/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <kopano/kcodes.h>

struct soap;

namespace KC {

class ECSession;

extern ECRESULT TestPerform(ECSession *lpSession, const char *cmd, unsigned int ulArgs, char **args);
extern ECRESULT TestSet(const char *name, const char *value);
extern ECRESULT TestGet(struct soap *soap, const char *name, char **value);

} /* namespace */

#endif
