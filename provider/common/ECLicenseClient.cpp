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

#include <kopano/platform.h>

#include <string>
#include <vector>
#include <cstdlib>
#include <sys/un.h>
#include <sys/socket.h>
#include <kopano/ECDefs.h>
#include <kopano/ECChannel.h>
#include <kopano/stringutil.h>

#include "ECLicenseClient.h"

namespace KC {

ECRESULT ECLicenseClient::ServiceTypeToServiceTypeString(unsigned int ulServiceType, std::string &strServiceType)
{
    ECRESULT er = erSuccess;
    switch(ulServiceType)
    {
        case 0 /*SERVICE_TYPE_ZCP*/:
            strServiceType = "ZCP";
            break;
        case 1 /*SERVICE_TYPE_ARCHIVE*/:
            strServiceType = "ARCHIVER";
            break;
        default:
            er = KCERR_INVALID_TYPE;
            break;
    }

    return er;
}
    
ECRESULT ECLicenseClient::GetCapabilities(unsigned int ulServiceType, std::vector<std::string > &lstCapabilities)
{
	ECRESULT er;
	std::string strServiceType;

	er = ServiceTypeToServiceTypeString(ulServiceType, strServiceType);
	if (er != erSuccess)
		return er;

	lstCapabilities.clear();
	if (ulServiceType == 0) {
		lstCapabilities.push_back("DEFAULT");
		lstCapabilities.push_back("OUTLOOK");
		lstCapabilities.push_back("OLENABLED");
		lstCapabilities.push_back("BACKUP");
		lstCapabilities.push_back("GATEWAY");
		lstCapabilities.push_back("ICAL");
		lstCapabilities.push_back("REPORT");
		lstCapabilities.push_back("MIGRATION");
		lstCapabilities.push_back("WA-ADVANCED-CALENDAR");
		lstCapabilities.push_back("BES");
		lstCapabilities.push_back("MULTISERVER");
		lstCapabilities.push_back("UPDATER");
		lstCapabilities.push_back("EWS");
	}
	return erSuccess;
}

ECRESULT ECLicenseClient::GetSerial(unsigned int ulServiceType, std::string &strSerial, std::vector<std::string> &lstCALs)
{
	ECRESULT er;
	std::string strServiceType;

	er = ServiceTypeToServiceTypeString(ulServiceType, strServiceType);
	if (er != erSuccess)
		return er;

	strSerial = "";
	lstCALs.clear();

	return erSuccess;
}

ECRESULT ECLicenseClient::GetInfo(unsigned int ulServiceType, unsigned int *lpulUserCount)
{
	ECRESULT er;
	std::string strServiceType;

	er = ServiceTypeToServiceTypeString(ulServiceType, strServiceType);
	if (er != erSuccess)
		return er;

	*lpulUserCount = 65535;
	return erSuccess;
}

struct LICENSERESPONSE {
	unsigned int ulVersion;			// Current: LICENSERESPONSE_VERSION
	unsigned int ulTrackingId;
	unsigned long long llFlags;
	unsigned int ulStatus;
	char szPadding[4];				// Make sure the struct is padded to a multiple of 8 bytes
};

ECRESULT ECLicenseClient::Auth(const unsigned char *lpData,
    unsigned int ulSize, void **lppResponse, unsigned int *lpulResponseSize)
{
	*lppResponse = calloc(1, sizeof(LICENSERESPONSE));
	*lpulResponseSize = sizeof(LICENSERESPONSE);
	return erSuccess;
}

ECRESULT ECLicenseClient::SetSerial(unsigned int ulServiceType, const std::string &strSerial, const std::vector<std::string> &lstCALs)
{
	ECRESULT er;
	std::string strServiceType;

	er = ServiceTypeToServiceTypeString(ulServiceType, strServiceType);
	if (er != erSuccess)
		return er;

	return erSuccess;
}

} /* namespace */
