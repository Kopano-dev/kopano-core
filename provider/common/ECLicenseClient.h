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

#ifndef ECLICENSECLIENT_H
#define ECLICENSECLIENT_H

#include <kopano/zcdefs.h>
#include <vector>
#include <string>

#include "ECChannelClient.h"
#include <kopano/kcodes.h>

namespace KC {

class _kc_export ECLicenseClient _kc_final {
public:
	_kc_hidden ECLicenseClient(const char * = nullptr, unsigned int = 0) {}
    
    ECRESULT GetCapabilities(unsigned int ulServiceType, std::vector<std::string > &lstCapabilities);
    ECRESULT GetInfo(unsigned int ulServiceType, unsigned int *lpulUserCount);

private:
	_kc_hidden ECRESULT ServiceTypeToServiceTypeString(unsigned int type, std::string &tname);
};

} /* namespace */

#endif
