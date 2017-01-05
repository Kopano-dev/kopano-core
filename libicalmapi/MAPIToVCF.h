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

#ifndef MAPITOVCF_H
#define MAPITOVCF_H

#include <string>
#include <mapidefs.h>

namespace KC {

class MapiToVCF {
public:
	virtual ~MapiToVCF(void) = default;
	virtual HRESULT AddMessage(LPMESSAGE lpMessage) = 0;
	virtual HRESULT Finalize(std::string *strVCF) = 0;
	virtual HRESULT ResetObject() = 0;
};

extern _kc_export HRESULT CreateMapiToVCF(MapiToVCF **ret);

} /* namespace */

#endif
