/*
 *	A RAII-ish interface to libmapi
 */
#include <kopano/platform.h>
#include <string>
#include <kopano/ECDebug.h>
#include <kopano/hl.hpp>

using namespace KC;

namespace KCHL {

KMAPIError::KMAPIError(HRESULT code) :
	m_code(code), m_message(GetMAPIErrorDescription(m_code))
{
}

const char *KMAPIError::what(void) const noexcept
{
	return m_message.c_str();
}

} /* namespace KCHL */
