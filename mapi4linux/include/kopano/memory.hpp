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
#include <type_traits> /* std::is_base_of */
#include <utility> /* std::swap */
#include <cstdlib>
#include <mapiutil.h> /* MAPIFreeBuffer */
#include <edkmdb.h> /* ROWLIST */
#include <kopano/ECGuid.h>
#include <kopano/ECTags.h>

namespace KCHL {
using namespace KC;

template<typename T> class memory_proxy _kc_final {
	public:
	memory_proxy(T **__p) noexcept : _m_ptr(__p) {}
	operator T **(void) noexcept { return _m_ptr; }
	template<typename U> U **as(void) const noexcept
	{
		static_assert(sizeof(U *) == sizeof(T *), "This hack won't work");
		return reinterpret_cast<U **>(_m_ptr);
	}
	operator void **(void) noexcept { return as<void>(); }

	private:
	T **_m_ptr;
};

template<typename T> class memory_proxy2 _kc_final {
	public:
	memory_proxy2(T **__p) noexcept : _m_ptr(__p) {}
	memory_proxy<T> operator&(void)
	{
		return memory_proxy<T>(_m_ptr);
	}

	private:
	T **_m_ptr;
};

template<typename T> class object_proxy _kc_final {
	public:
	object_proxy(T **__p) noexcept : _m_ptr(__p) {}
	operator T **(void) noexcept { return _m_ptr; }
	template<typename U> U **as(void) const noexcept
	{
		static_assert(sizeof(U *) == sizeof(T *), "This hack won't work");
		return reinterpret_cast<U **>(_m_ptr);
	}
	operator void **(void) noexcept { return as<void>(); }
	operator IUnknown **(void) noexcept { return as<IUnknown>(); }

	private:
	T **_m_ptr;
};

template<> class object_proxy<IUnknown> _kc_final {
	public:
	object_proxy(IUnknown **__p) noexcept : _m_ptr(__p) {}
	operator IUnknown **(void) noexcept { return _m_ptr; }
	template<typename U> U **as(void) const noexcept
	{
		static_assert(sizeof(U *) == sizeof(IUnknown *), "This hack won't work");
		return reinterpret_cast<U **>(_m_ptr);
	}
	operator void **(void) noexcept { return as<void>(); }

	private:
	IUnknown **_m_ptr;
};

template<typename T> class object_proxy2 _kc_final {
	public:
	object_proxy2(T **__p) noexcept : _m_ptr(__p) {}
	object_proxy<T> operator&(void)
	{
		return object_proxy<T>(_m_ptr);
	}

	private:
	T **_m_ptr;
};

class default_delete {
	public:
	void operator()(void *p) const { MAPIFreeBuffer(p); }
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
template<typename T, typename _Deleter = default_delete> class memory_ptr {
	public:
	typedef T value_type;
	typedef T *pointer;
	constexpr memory_ptr(void) noexcept {}
	constexpr memory_ptr(std::nullptr_t) noexcept {}
	explicit memory_ptr(T *__p) noexcept : _m_ptr(__p) {}
	~memory_ptr(void)
	{
		if (_m_ptr != nullptr)
			_Deleter()(_m_ptr);
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
	T &operator*(void) const { return *_m_ptr; }
	T *operator->(void) const noexcept { return _m_ptr; }
	T *get(void) const noexcept { return _m_ptr; }
	operator T *(void) const noexcept { return _m_ptr; }
	T *operator+(size_t __n) const noexcept { return _m_ptr + __n; }
	/* Modifiers */
	T *release(void) noexcept
	{
		T *__p = get();
		_m_ptr = pointer();
		return __p;
	}
	void reset(T *__p = pointer()) noexcept
	{
		std::swap(_m_ptr, __p);
		if (__p != pointer())
			_Deleter()(__p);
	}
	void swap(memory_ptr &__o) noexcept
	{
		std::swap(_m_ptr, __o._m_ptr);
	}
	memory_proxy2<T> operator~(void)
	{
		reset();
		return memory_proxy2<T>(&_m_ptr);
	}
	memory_proxy2<T> operator+(void)
	{
		return memory_proxy2<T>(&_m_ptr);
	}
	memory_ptr &operator=(const memory_ptr &) = delete;
	memory_ptr &operator=(memory_ptr &&__o) noexcept
	{
		reset(__o.release());
		return *this;
	}
	memory_ptr &operator=(std::nullptr_t) noexcept
	{
		reset();
		return *this;
	}

	private:
	void operator&(void) const noexcept {} /* flag everyone */

	T *_m_ptr = nullptr;
};

/**
 * Works a bit like shared_ptr, except that the refcounting is in the
 * underlying object (T) rather than this class.
 */
template<typename T> class object_ptr {
	public:
	typedef T value_type;
	typedef T *pointer;
	constexpr object_ptr(void) noexcept {}
	constexpr object_ptr(std::nullptr_t) noexcept {}
	explicit object_ptr(T *__p, bool __addref = true) : _m_ptr(__p)
	{
		if (__addref && _m_ptr != pointer())
			_m_ptr->AddRef();
	}
	~object_ptr(void)
	{
		if (_m_ptr != pointer())
			_m_ptr->Release();
		_m_ptr = pointer();
	}
	object_ptr(const object_ptr &__o)
	{
		reset(__o._m_ptr, true);
	}
	object_ptr(object_ptr &&__o)
	{
		auto __old = get();
		_m_ptr = __o._m_ptr;
		__o._m_ptr = pointer();
		if (__old != pointer())
			__old->Release();
	}
	/* Observers */
	T &operator*(void) const { return *_m_ptr; }
	T *operator->(void) const noexcept { return _m_ptr; }
	T *get(void) const noexcept { return _m_ptr; }
	operator T *(void) const noexcept { return _m_ptr; }
	template<typename U> HRESULT QueryInterface(U &);
	template<typename P> P as();

	/* Modifiers */
	T *release(void) noexcept
	{
		T *__p = get();
		_m_ptr = pointer();
		return __p;
	}
	void reset(T *__p = pointer(), bool __addref = true) noexcept
	{
		if (__addref && __p != pointer())
			__p->AddRef();
		std::swap(_m_ptr, __p);
		if (__p != pointer())
			__p->Release();
	}
	void swap(object_ptr &__o) noexcept
	{
		std::swap(_m_ptr, __o._m_ptr);
	}
	object_proxy2<T> operator~(void)
	{
		reset();
		return object_proxy2<T>(&_m_ptr);
	}
	object_proxy2<T> operator+(void)
	{
		return object_proxy2<T>(&_m_ptr);
	}
	object_ptr &operator=(const object_ptr &__o) noexcept
	{
		reset(__o._m_ptr, true);
		return *this;
	}
	object_ptr &operator=(object_ptr &&__o) noexcept
	{
		auto __old = get();
		_m_ptr = __o._m_ptr;
		__o._m_ptr = pointer();
		if (__old != pointer())
			__old->Release();
		return *this;
	}
	private:
	void operator=(std::nullptr_t) noexcept {}
	void operator&(void) const noexcept {} /* flag everyone */

	T *_m_ptr = nullptr;
};

class cstdlib_deleter {
	public:
	void operator()(void *x) const { free(x); }
};

class rowset_delete {
	public:
	void operator()(ADRLIST *x) const { FreePadrlist(x); }
	void operator()(SRowSet *x) const { FreeProws(x); }
	void operator()(ROWLIST *x) const { FreeProws(reinterpret_cast<SRowSet *>(x)); }
};

typedef memory_ptr<ADRLIST, rowset_delete> adrlist_ptr;
typedef memory_ptr<SRowSet, rowset_delete> rowset_ptr;
typedef memory_ptr<ROWLIST, rowset_delete> rowlist_ptr;

template<typename T> inline void
swap(memory_ptr<T> &__x, memory_ptr<T> &__y) noexcept
{
	__x.swap(__y);
}

template<typename T> inline void
swap(object_ptr<T> &__x, object_ptr<T> &__y) noexcept
{
	__x.swap(__y);
}

} /* namespace KCHL */

namespace KC {

template<typename U> static inline constexpr const IID &
iid_of(const KCHL::object_ptr<U> &)
{
	return iid_of(static_cast<const U *>(nullptr));
}

} /* namespace KC */

namespace KCHL {

template<typename T > template<typename U>
HRESULT object_ptr<T>::QueryInterface(U &result)
{
	if (_m_ptr == nullptr)
		return MAPI_E_NOT_INITIALIZED;
	typename U::pointer newobj = nullptr;
	HRESULT hr = _m_ptr->QueryInterface(iid_of(result), reinterpret_cast<void **>(&newobj));
	if (hr == hrSuccess)
		result.reset(newobj, false);
	/*
	 * Here we check if it makes sense to try to get the requested
	 * interface through the PR_EC_OBJECT object. It only makes
	 * sense to attempt this if the current type (value_type) is
	 * derived from IMAPIProp. If it is higher than IMAPIProp, no
	 * OpenProperty exists. If it is derived from
	 * ECMAPIProp/ECGenericProp, there is no need to try the
	 * workaround, because we can be sure it is not wrapped by
	 * MAPI.
	 *
	 * The Conversion<IMAPIProp,value_type>::exists is some
	 * template magic that checks at compile time. We could check
	 * at run time with a dynamic_cast, but we know the current
	 * type at compile time, so why not check it at compile time?
	 */
	else if (hr == MAPI_E_INTERFACE_NOT_SUPPORTED &&
	    std::is_base_of<IMAPIProp, value_type>::value) {
		KCHL::memory_ptr<SPropValue> pv;
		if (HrGetOneProp(_m_ptr, PR_EC_OBJECT, &~pv) != hrSuccess)
			return hr; // hr is still MAPI_E_INTERFACE_NOT_SUPPORTED
		auto unk = reinterpret_cast<IUnknown *>(pv->Value.lpszA);
		hr = unk->QueryInterface(iid_of(newobj), reinterpret_cast<void **>(&newobj));
		if (hr == hrSuccess)
			result.reset(newobj, false);
	}
	return hr;
}

template<typename T> template<typename P> P object_ptr<T>::as(void)
{
	P tmp = nullptr;
	QueryInterface(tmp);
	return tmp;
}

} /* namespace KCHL */

#endif /* _KCHL_MEMORY_HPP */
