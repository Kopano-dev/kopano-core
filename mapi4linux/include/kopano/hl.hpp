#ifndef _KC_HL_HPP
#define _KC_HL_HPP 1

#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include <kopano/ECDebug.h>
#include <stdexcept>
#include <string>

namespace KC {

class _kc_export_throw KMAPIError _kc_final : public std::runtime_error {
	public:
	KMAPIError(HRESULT c = hrSuccess) : std::runtime_error(GetMAPIErrorDescription(c)), m_code(c) {}
	HRESULT code(void) const noexcept { return m_code; }

	private:
	HRESULT m_code;
};

} /* namespace */

#endif /* _KC_HL_HPP */
