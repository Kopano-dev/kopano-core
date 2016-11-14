/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 * Copyright 2016 Kopano and its licensors
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
 */
#ifndef _KCHL_MEMORY_HPP
#define _KCHL_MEMORY_HPP 1

#include <kopano/zcdefs.h>
#include <utility> /* std::swap */
#include <mapiutil.h> /* MAPIFreeBuffer */

namespace KCHL {

template<typename _T> class memory_proxy _kc_final {
	public:
	memory_proxy(_T **__p) noexcept : _m_ptr(__p) {}
	operator _T **(void) noexcept { return _m_ptr; }
	template<typename _U> _U **as(void) const noexcept
	{
		static_assert(sizeof(_U *) == sizeof(_T *), "This hack won't work");
		return reinterpret_cast<_U **>(_m_ptr);
	}
	operator void **(void) noexcept { return as<void>(); }

	private:
	_T **_m_ptr;
};

template<typename _T> class memory_proxy2 _kc_final {
	public:
	memory_proxy2(_T **__p) noexcept : _m_ptr(__p) {}
	memory_proxy<_T> operator&(void)
	{
		return memory_proxy<_T>(_m_ptr);
	}

	private:
	_T **_m_ptr;
};

/**
 * The KCHL memory_ptr works a lot like std::unique_ptr, with the
 * additional differences:
 *  - operator& is defined (this is why we cannot use/derive from unique_ptr)
 *  - the deleter is fixed (for now…)
 *  - conversion to base class is not permitted (not very relevant to KC)
 *  - operator bool not present (not really desired for KC)
 * Differences to the prior implementation (ZCP's mapi_memory_ptr):
 *  - "constexpr", "noexcept" and "explicit" keywords added
 *  - move constructor and move assignment is implemented
 *  - methods "is_null", "free" and "as" are gone
 *  - operator void** and operator! is gone
 */
template<typename _T> class memory_ptr {
	public:
	typedef _T value_type;
	typedef _T *pointer;
	constexpr memory_ptr(void) noexcept {}
	constexpr memory_ptr(nullptr_t) noexcept {}
	explicit memory_ptr(_T *__p) noexcept : _m_ptr(__p) {}
	~memory_ptr(void)
	{
		/* Also see reset() for another instance of MAPIFreeBuffer. */
		if (_m_ptr != nullptr)
			MAPIFreeBuffer(_m_ptr);
		/*
		 * We normally don't need the following. Or maybe don't even
		 * want. But g++'s stdlib has it, probably for robustness
		 * reasons when placement delete is involved.
		 */
		_m_ptr = pointer();
	}
	memory_ptr(const memory_ptr &) = delete;
	memory_ptr(memory_ptr &&__o) : _m_ptr(__o.release()) {}
	/* Observers */
	_T &operator*(void) const { return *_m_ptr; }
	_T *operator->(void) const noexcept { return _m_ptr; }
	_T *get(void) const noexcept { return _m_ptr; }
	operator _T *(void) const noexcept { return _m_ptr; }
	_T &operator[](size_t __n) const noexcept { return _m_ptr[__n]; }
	_T *operator+(size_t __n) const noexcept { return _m_ptr + __n; }
	public:
	/* Modifiers */
	_T *release(void) noexcept
	{
		_T *__p = get();
		_m_ptr = pointer();
		return __p;
	}
	void reset(_T *__p = pointer()) noexcept
	{
		std::swap(_m_ptr, __p);
		if (__p != pointer())
			MAPIFreeBuffer(__p);
	}
	void swap(memory_ptr &__o)
	{
		std::swap(_m_ptr, __o._m_ptr);
	}
	memory_proxy2<_T> operator~(void)
	{
		reset();
		return memory_proxy2<_T>(&_m_ptr);
	}
	memory_ptr &operator=(const memory_ptr &) = delete;
	memory_ptr &operator=(memory_ptr &&__o) noexcept
	{
		reset(__o.release());
		return *this;
	}
	memory_ptr &operator=(nullptr_t) noexcept
	{
		reset();
		return *this;
	}

	private:
	void operator&(void) const noexcept {} /* flag everyone */

	_T *_m_ptr = nullptr;
};

template<typename _T> inline void
swap(memory_ptr<_T> &__x, memory_ptr<_T> &__y) noexcept
{
	__x.swap(__y);
}

} /* namespace KCHL */

#endif /* _KCHL_MEMORY_HPP */
