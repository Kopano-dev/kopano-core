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

#ifndef ECCLIENTUPDATE_H
#define ECCLIENTUPDATE_H

struct ClientVersion {
	unsigned int nMajorVersion;
	unsigned int nMinorVersion;
	unsigned int nUpdateNumber;
	unsigned int nBuildNumber;
};


/* entry point */
int HandleClientUpdate(struct soap *soap);

bool ConvertAndValidatePath(const char *lpszClientUpdatePath, const std::string &strMSIName, std::string *lpstrDownloadFile);
bool GetVersionFromString(char *szVersion, ClientVersion *lpClientVersion);
bool GetVersionFromMSIName(const char *szVersion, ClientVersion *lpClientVersion);
int  CompareVersions(ClientVersion Version1, ClientVersion Version2);
bool GetClientMSINameFromVersion(const ClientVersion &clientVersion, std::string *lpstrMSIName);

#endif
