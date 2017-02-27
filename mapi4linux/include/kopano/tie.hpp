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
template<typename _T, typename _D> class unique_proxy {
	public:
	unique_proxy(std::unique_ptr<_T, _D> &a) : u(a), p(u.get()) {}
	~unique_proxy(void) { u.reset(p); }
	typename std::unique_ptr<_T, _D>::pointer *operator&(void) { return &p; }
	private:
	std::unique_ptr<_T, _D> &u;
	typename std::unique_ptr<_T, _D>::pointer p;
};

template<typename _T, typename _D> unique_proxy<_T, _D>
unique_tie(std::unique_ptr<_T, _D> &u)
{
	return unique_proxy<_T, _D>(u);
}

} /* namespace KCHL */

#endif /* _KCHL_TIE_HPP */
