#ifndef _KCHL_HPP
#define _KCHL_HPP 1

#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include <kopano/ECDebug.h>
#include <stdexcept>
#include <string>

namespace KCHL {

class _kc_export_throw KMAPIError _kc_final : public std::exception {
	public:
	KMAPIError(HRESULT c = hrSuccess) : m_code(c), m_message(GetMAPIErrorDescription(m_code)) {}
	HRESULT code(void) const noexcept { return m_code; }
	const char *what() const noexcept { return m_message.c_str(); }

	private:
	HRESULT m_code;
	std::string m_message;
};

} /* namespace KCHL */

#endif /* _KCHL_HPP */
