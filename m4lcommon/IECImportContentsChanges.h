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

#ifndef IECIMPORTCONTENTSCHANGES_H
#define IECIMPORTCONTENTSCHANGES_H

namespace KC {

class IECImportContentsChanges : public IExchangeImportContentsChanges {
public:
	virtual HRESULT __stdcall ConfigForConversionStream(LPSTREAM lpStream, ULONG ulFlags, ULONG cValuesConversion, LPSPropValue lpPropArrayConversion) = 0;
	virtual HRESULT __stdcall ImportMessageChangeAsAStream(ULONG cpvalChanges, LPSPropValue ppvalChanges, ULONG ulFlags, LPSTREAM *lppstream) = 0;
	virtual HRESULT __stdcall SetMessageInterface(REFIID refiid) = 0;
};

} /* namespace */

#endif
