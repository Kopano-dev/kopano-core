/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
