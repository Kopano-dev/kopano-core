#ifndef _KC_SCOPE_HPP
#define _KC_SCOPE_HPP 1

#include <stdexcept>
#include <utility>

namespace KC {

/* P0052r5 (C++2020) */
template<typename F> class scope_success {
	public:
	explicit scope_success(F &&f) : m_func(std::move(f)), m_eod(true) {}
	scope_success(scope_success &&o) : m_func(std::move(o.m_func)), m_eod(o.m_eod) {}
	~scope_success() { if (m_eod && !std::uncaught_exception()) m_func(); }
	void operator=(scope_success &&) = delete;
	private:
	F m_func;
	bool m_eod = false;
};

template<typename F> scope_success<F> make_scope_success(F &&f)
{
	return scope_success<F>(std::move(f));
}

} /* namespace */

#endif /* _KC_SCOPE_HPP */
