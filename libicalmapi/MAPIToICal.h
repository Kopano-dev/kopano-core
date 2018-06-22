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

#ifndef MAPITOICAL_H
#define MAPITOICAL_H

#include <kopano/zcdefs.h>
#include <string>
#include <mapidefs.h>
#include <freebusy.h>

#define M2IC_CENSOR_PRIVATE 0x0001
#define M2IC_NO_VTIMEZONE 0x0002

namespace KC {

class MapiToICal {
public:
	/*
	    - Addressbook (Global AddressBook for looking up users)
	 */
	virtual ~MapiToICal(void) = default;
	virtual HRESULT AddMessage(LPMESSAGE lpMessage, const std::string &strSrvTZ, ULONG ulFlags) = 0;
	virtual HRESULT AddBlocks(FBBlock_1 *lpsFBblk, LONG ulBlocks, time_t tStart, time_t tEnd, const std::string &strOrganiser, const std::string &strUser, const std::string &strUID) = 0;
	virtual HRESULT Finalize(ULONG ulFlags, std::string *strMethod, std::string *strIcal) = 0;
	virtual HRESULT ResetObject() = 0;
};

extern _kc_export HRESULT CreateMapiToICal(LPADRBOOK, const std::string &charset, MapiToICal **ret);

} /* namespace */

#endif
