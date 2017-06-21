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

#ifndef EC_FEATURES_H
#define EC_FEATURES_H

#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <mapidefs.h>
#include <mapix.h>
#include <kopano/IECInterfaces.hpp>
#include <string>
#include <set>

namespace KC {

extern _kc_export bool isFeature(const char *);
extern HRESULT hasFeature(const char *feature, const SPropValue *lpProps);
extern _kc_export HRESULT hasFeature(const wchar_t *feature, const SPropValue *props);
std::set<std::string> getFeatures();
extern _kc_export bool isFeatureDisabled(const char *, IAddrBook *, IMsgStore *user);

} /* namespace */

#endif
