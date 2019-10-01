/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef EC_FEATURES_H
#define EC_FEATURES_H

#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <mapidefs.h>
#include <mapix.h>
#include <kopano/IECInterfaces.hpp>

namespace KC {

extern KC_EXPORT bool isFeature(const char *);
extern KC_EXPORT HRESULT hasFeature(const char *feature, const SPropValue *props);
extern KC_EXPORT bool checkFeature(const char *feature, IAddrBook *, IMsgStore *, unsigned int tag);


} /* namespace */

#endif
