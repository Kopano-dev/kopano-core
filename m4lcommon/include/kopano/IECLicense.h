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

#ifndef IECLICENSE_H
#define IECLICENSE_H

class IECLicense : public IUnknown {
public:
	virtual HRESULT __stdcall LicenseAuth(unsigned char *lpData, unsigned int ulSize, unsigned char **lpAuthResponse, unsigned int *lpulResponseSize) = 0;
	virtual HRESULT __stdcall LicenseCapa(unsigned int ulServiceType, char ***lppszCapabilities, unsigned int *lpulCapabilities) = 0;
	virtual HRESULT __stdcall LicenseUsers(unsigned int ulServiceType, unsigned int *ulUsers) = 0;
};

#endif
