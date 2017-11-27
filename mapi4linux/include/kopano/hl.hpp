#ifndef _KCHL_HPP
#define _KCHL_HPP 1

#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include <stdexcept>
#include <string>

namespace KCHL {

class _kc_export_throw KMAPIError _kc_final : public std::exception {
	public:
	KMAPIError(HRESULT = hrSuccess);
	virtual ~KMAPIError(void) noexcept = default;
	HRESULT code(void) const noexcept { return m_code; }
	virtual const char *what(void) const noexcept;

	private:
	HRESULT m_code;
	std::string m_message;
};

} /* namespace KCHL */

#endif /* _KCHL_HPP */
