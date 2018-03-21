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

template<size_t N> class KPropbuffer {
	protected:
	SPropValue m_prop[N];
	std::string m_str[N];
	std::wstring m_wstr[N];

	public:
	constexpr size_t size() const { return N; }
	SPropValue *get() { return m_prop; }
	SPropValue &operator[](size_t i) { return m_prop[i]; }
	void set(size_t i, unsigned int tag, const std::string &s)
	{
		m_str[i] = s;
		m_prop[i].ulPropTag = CHANGE_PROP_TYPE(tag, PT_STRING8);
		m_prop[i].Value.lpszA = const_cast<char *>(m_str[i].c_str());
	}
	void set(size_t i, unsigned int tag, std::string &&s)
	{
		m_str[i] = std::move(s);
		m_prop[i].ulPropTag = CHANGE_PROP_TYPE(tag, PT_STRING8);
		m_prop[i].Value.lpszA = const_cast<char *>(m_str[i].c_str());
	}
	void set(size_t i, unsigned int tag, const std::wstring &s)
	{
		m_wstr[i] = s;
		m_prop[i].ulPropTag = CHANGE_PROP_TYPE(tag, PT_UNICODE);
		m_prop[i].Value.lpszW = const_cast<wchar_t *>(m_wstr[i].c_str());
	}
	void set(size_t i, unsigned int tag, std::wstring &&s)
	{
		m_wstr[i] = std::move(s);
		m_prop[i].ulPropTag = CHANGE_PROP_TYPE(tag, PT_UNICODE);
		m_prop[i].Value.lpszW = const_cast<wchar_t *>(m_wstr[i].c_str());
	}
};

} /* namespace */

#endif /* _KC_HL_HPP */
