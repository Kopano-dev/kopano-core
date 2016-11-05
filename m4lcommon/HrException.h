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

#ifndef HRException_INCLUDED
#define HRException_INCLUDED

#include <kopano/zcdefs.h>
#include <exception>

class _kc_export_throw HrException _kc_final : public std::runtime_error {
public:
	HrException(HRESULT hr, const std::string &message = std::string()): std::runtime_error(message), m_hr(hr) {}
	HRESULT hr() const { return m_hr; }

private:
	HRESULT m_hr;
};

#endif // ndef HRException_INCLUDED
