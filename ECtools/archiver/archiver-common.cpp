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
#include <kopano/archiver-common.h>
#include <kopano/stringutil.h>

bool entryid_t::operator==(const entryid_t &other) const
{
	return getUnwrapped().m_vEntryId == other.getUnwrapped().m_vEntryId;
}

bool entryid_t::operator<(const entryid_t &other) const
{
	return getUnwrapped().m_vEntryId < other.getUnwrapped().m_vEntryId;
}

bool entryid_t::operator>(const entryid_t &other) const
{
	return getUnwrapped().m_vEntryId > other.getUnwrapped().m_vEntryId;
}

bool entryid_t::wrap(const std::string &strPath)
{
	if (!kc_istarts_with(strPath, "file://") &&
	    !kc_istarts_with(strPath, "http://") &&
	    !kc_istarts_with(strPath, "https://"))
		return false;
	
	m_vEntryId.insert(m_vEntryId.begin(), (LPBYTE)strPath.c_str(), (LPBYTE)strPath.c_str() + strPath.size() + 1);	// Include NULL terminator
	return true;
}

bool entryid_t::unwrap(std::string *lpstrPath)
{
	if (!isWrapped())
		return false;
	
	std::vector<BYTE>::iterator iter = std::find(m_vEntryId.begin(), m_vEntryId.end(), 0);
	if (iter == m_vEntryId.end())
		return false;
		
	if (lpstrPath)
		lpstrPath->assign((char*)&m_vEntryId.front(), iter - m_vEntryId.begin());
	
	m_vEntryId.erase(m_vEntryId.begin(), ++iter);
	return true;
}

bool entryid_t::isWrapped() const
{
	// ba::istarts_with doesn't work well on unsigned char. So we use a temporary instead.
	const std::string strEntryId((char*)&m_vEntryId.front(), m_vEntryId.size());

	return kc_istarts_with(strEntryId, "file://") ||
	       kc_istarts_with(strEntryId, "http://") ||
	       kc_istarts_with(strEntryId, "https://");
}

entryid_t entryid_t::getUnwrapped() const
{
	if (!isWrapped())
		return *this;

	entryid_t tmp(*this);
	tmp.unwrap(NULL);
	return tmp;
}

int abentryid_t::compare(const abentryid_t &other) const
{
	if (size() < other.size())
		return -1;
	if (size() > other.size())
		return 1;

	if (size() <= 32) {
		// Too small, just compare the whole thing
		return memcmp(LPBYTE(*this), LPBYTE(other), size());
	}

	// compare the part before the legacy user id.
	int res = memcmp(LPBYTE(*this), LPBYTE(other), 28);
	if (res != 0)
		return res;

	// compare the part after the legacy user id.
	return memcmp(LPBYTE(*this) + 32, LPBYTE(other) + 32, size() - 32);
}

eResult MAPIErrorToArchiveError(HRESULT hr)
{
	switch (hr) {
		case hrSuccess:					return Success;
		case MAPI_E_NOT_ENOUGH_MEMORY:	return OutOfMemory;
		case MAPI_E_INVALID_PARAMETER:	return InvalidParameter;
		case MAPI_W_PARTIAL_COMPLETION:	return PartialCompletion;
		default: 						return Failure;
	}
}

const char* ArchiveResultString(eResult result)
{
    const char* retval = "Unknown result";
    switch (result)
    {
        case Success:
            retval = "Success";
            break;
        case OutOfMemory:
            retval = "Out of memory";
            break;
        case InvalidParameter:
            retval = "Invalid parameter";
            break;
        case PartialCompletion:
            retval = "Partial completion";
            break;
        case Failure:
            retval = "Failure";
            break;
        default:
            /* do nothing */;
    }
    return retval;
}
