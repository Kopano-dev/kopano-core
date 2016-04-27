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

#include <kopano/platform.h>
#include <mapidefs.h>
#include <mapix.h>
#include <kopano/IECServiceAdmin.h>
#include <string>
#include <set>

bool isFeature(const char* feature);
HRESULT hasFeature(const char* feature, LPSPropValue lpProps);
HRESULT hasFeature(const WCHAR* feature, LPSPropValue lpProps);
std::set<std::string> getFeatures();

bool isFeatureEnabled(const char* feature, IAddrBook *lpAddrBook, IMsgStore *lpUser);
bool isFeatureDisabled(const char* feature, IAddrBook *lpAddrBook, IMsgStore *lpUser);

#endif
