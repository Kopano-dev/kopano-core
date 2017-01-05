/*
 * Copyright 2017 - Kopano and its licensors
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

#ifndef VCFTOMAPI_H
#define VCFTOMAPI_H

#include <mapidefs.h>
#include <string>
#include <list>

#include "icalitem.h"

namespace KC {

class VCFToMapi {
public:
	VCFToMapi(IMAPIProp *lpPropObj) : m_lpPropObj(lpPropObj) {};
	virtual ~VCFToMapi(void) = default;
	virtual HRESULT ParseVCF(const std::string& strVCF) = 0;
	virtual HRESULT GetItem(LPMESSAGE lpMessage) = 0;
protected:
	LPMAPIPROP m_lpPropObj;
	std::list<SPropValue> props;
};

extern _kc_export HRESULT CreateVCFToMapi(IMAPIProp *propobj, VCFToMapi **ret);

} /* namespace */

#endif
