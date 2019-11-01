/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <mapicode.h>
#include <kopano/platform.h>
#include <kopano/archiver-common.h>
#include <kopano/stringutil.h>

namespace KC {

bool entryid_t::operator==(const entryid_t &other) const
{
	return getUnwrapped().m_eid == other.getUnwrapped().m_eid;
}

bool entryid_t::operator<(const entryid_t &other) const
{
	return getUnwrapped().m_eid < other.getUnwrapped().m_eid;
}

bool entryid_t::operator>(const entryid_t &other) const
{
	return getUnwrapped().m_eid > other.getUnwrapped().m_eid;
}

bool entryid_t::wrap(const std::string &strPath)
{
	if (!kc_istarts_with(strPath, "file:") &&
	    !kc_istarts_with(strPath, "http:") &&
	    !kc_istarts_with(strPath, "https:"))
		return false;
	/* The '\0' from strPath is included; used as a separator in the wrapped EID. */
	m_eid.insert(m_eid.begin(), strPath.c_str(), strPath.c_str() + strPath.size() + 1);
	return true;
}

bool entryid_t::unwrap(std::string *lpstrPath)
{
	if (!isWrapped())
		return false;
	auto pos = m_eid.find('\0');
	if (pos == std::string::npos)
		return false;
	/* Extract and save away path; existing EID is unwrapped in place. */
	if (lpstrPath)
		lpstrPath->assign(m_eid, 0, pos);
	m_eid.erase(0, pos + 1);
	return true;
}

bool entryid_t::isWrapped() const
{
	return kc_istarts_with(m_eid, "file:") ||
	       kc_istarts_with(m_eid, "http:") ||
	       kc_istarts_with(m_eid, "https:");
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
	if (size() <= 32)
		// Too small, just compare the whole thing
		return memcmp(LPBYTE(*this), LPBYTE(other), size());

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
	case hrSuccess:			return Success;
	case MAPI_E_NOT_ENOUGH_MEMORY:	return OutOfMemory;
	case MAPI_E_INVALID_PARAMETER:	return InvalidParameter;
	case MAPI_W_PARTIAL_COMPLETION:	return PartialCompletion;
	default:			return Failure;
	}
}

const char* ArchiveResultString(eResult result)
{
    switch (result)
    {
	case Success: return "Success";
	case OutOfMemory: return "Out of memory";
	case InvalidParameter: return "Invalid parameter";
	case PartialCompletion: return "Partial completion";
	case Failure: return "Failure";
	default: return "Unknown result";
    }
}

} /* namespace */
