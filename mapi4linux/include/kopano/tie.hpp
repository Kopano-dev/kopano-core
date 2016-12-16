#ifndef _KCHL_TIE_HPP
#define _KCHL_TIE_HPP 1

#include <memory>

namespace KCHL {

/*
 * For functions which return their result through an arugment pointer,
 * a temporary variable may be necessary when one wishes to use unique_ptr:
 * 	unique_ptr<char> u; char *x; bla_alloc(&x); u.reset(x);
 * With unique_tie, this gets shorter:
 * 	unique_ptr<char> u; bla_alloc(&unique_tie(u));
 */
template<typename _T> class unique_proxy {
	public:
	unique_proxy(std::unique_ptr<_T> &a) : u(a), p(u.get()) {}
	~unique_proxy(void) { u.reset(p); }
	_T **operator&(void) { return &p; }
	private:
	std::unique_ptr<_T> &u;
	_T *p;
};

template<typename _T> unique_proxy<_T> unique_tie(std::unique_ptr<_T> &u)
{
	return unique_proxy<_T>(u);
}

} /* namespace KCHL */

#endif /* _KCHL_TIE_HPP */
