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
#ifndef _KC_MEMORY_HPP
#define _KC_MEMORY_HPP 1

#include <kopano/zcdefs.h>
#include <type_traits> /* std::is_base_of */
#include <utility> /* std::swap */
#include <cstdlib>
#include <mapiutil.h> /* MAPIFreeBuffer */
#include <edkmdb.h> /* ROWLIST */
#include <kopano/ECGuid.h>
#include <kopano/ECTags.h>

namespace KC {

template<typename T> class memory_proxy _kc_final {
	public:
	memory_proxy(T **p) noexcept : m_ptr(p) {}
	operator T **(void) noexcept { return m_ptr; }
	template<typename U> U **as(void) const noexcept
	{
		static_assert(sizeof(U *) == sizeof(T *), "This hack won't work");
		return reinterpret_cast<U **>(m_ptr);
	}
	operator void **(void) noexcept { return as<void>(); }

	private:
	T **m_ptr;
};

template<typename T> class memory_proxy2 _kc_final {
	public:
	memory_proxy2(T **p) noexcept : m_ptr(p) {}
	memory_proxy<T> operator&(void)
	{
		return memory_proxy<T>(m_ptr);
	}

	private:
	T **m_ptr;
};

template<typename T> class object_proxy _kc_final {
	public:
	object_proxy(T **p) noexcept : m_ptr(p) {}
	operator T **(void) noexcept { return m_ptr; }
	template<typename U> U **as(void) const noexcept
	{
		static_assert(sizeof(U *) == sizeof(T *), "This hack won't work");
		return reinterpret_cast<U **>(m_ptr);
	}
	operator void **(void) noexcept { return as<void>(); }
	operator IUnknown **(void) noexcept { return as<IUnknown>(); }

	private:
	T **m_ptr;
};

template<> class object_proxy<IUnknown> _kc_final {
	public:
	object_proxy(IUnknown **p) noexcept : m_ptr(p) {}
	operator IUnknown **(void) noexcept { return m_ptr; }
	template<typename U> U **as(void) const noexcept
	{
		static_assert(sizeof(U *) == sizeof(IUnknown *), "This hack won't work");
		return reinterpret_cast<U **>(m_ptr);
	}
	operator void **(void) noexcept { return as<void>(); }

	private:
	IUnknown **m_ptr;
};

template<typename T> class object_proxy2 _kc_final {
	public:
	object_proxy2(T **p) noexcept : m_ptr(p) {}
	object_proxy<T> operator&(void)
	{
		return object_proxy<T>(m_ptr);
	}

	private:
	T **m_ptr;
};

class default_delete {
	public:
	void operator()(void *p) const { MAPIFreeBuffer(p); }
};

/**
 * memory_ptr works a lot like std::unique_ptr, with the
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
template<typename T, typename Deleter = default_delete> class memory_ptr {
	public:
	typedef T *pointer;
	constexpr memory_ptr(void) noexcept {}
	constexpr memory_ptr(std::nullptr_t) noexcept {}
	explicit memory_ptr(T *p) noexcept : m_ptr(p) {}
	~memory_ptr(void)
	{
		if (m_ptr != nullptr)
			Deleter()(m_ptr);
		/*
		 * We normally don't need the following. Or maybe don't even
		 * want. But g++'s stdlib has it, probably for robustness
		 * reasons when placement delete is involved.
		 */
		m_ptr = pointer();
	}
	memory_ptr(const memory_ptr &) = delete;
	memory_ptr(memory_ptr &&o) : m_ptr(o.release()) {}
	/* Observers */
	T &operator*(void) const { return *m_ptr; }
	T *operator->(void) const noexcept { return m_ptr; }
	T *get(void) const noexcept { return m_ptr; }
	operator T *(void) const noexcept { return m_ptr; }
	T *operator+(size_t n) const noexcept { return m_ptr + n; }
	/* Modifiers */
	T *release(void) noexcept
	{
		T *p = get();
		m_ptr = pointer();
		return p;
	}
	void reset(T *p = pointer()) noexcept
	{
		std::swap(m_ptr, p);
		if (p != pointer())
			Deleter()(p);
	}
	void swap(memory_ptr &o) noexcept
	{
		std::swap(m_ptr, o.m_ptr);
	}
	memory_proxy2<T> operator~(void)
	{
		reset();
		return memory_proxy2<T>(&m_ptr);
	}
	memory_proxy2<T> operator+(void)
	{
		return memory_proxy2<T>(&m_ptr);
	}
	memory_ptr &operator=(const memory_ptr &) = delete;
	memory_ptr &operator=(memory_ptr &&o) noexcept
	{
		reset(o.release());
		return *this;
	}
	memory_ptr &operator=(std::nullptr_t) noexcept
	{
		reset();
		return *this;
	}

	private:
	void operator&(void) const noexcept {} /* flag everyone */

	T *m_ptr = nullptr;
};

/**
 * Works a bit like shared_ptr, except that the refcounting is in the
 * underlying object (T) rather than this class.
 */
template<typename T> class object_ptr {
	public:
	typedef T *pointer;
	constexpr object_ptr(void) noexcept {}
	constexpr object_ptr(std::nullptr_t) noexcept {}
	explicit object_ptr(T *p, bool addref = true) : m_ptr(p)
	{
		if (addref && m_ptr != pointer())
			m_ptr->AddRef();
	}
	~object_ptr(void)
	{
		if (m_ptr != pointer())
			m_ptr->Release();
		m_ptr = pointer();
	}
	object_ptr(const object_ptr &o)
	{
		reset(o.m_ptr, true);
	}
	object_ptr(object_ptr &&o)
	{
		auto old = get();
		m_ptr = o.m_ptr;
		o.m_ptr = pointer();
		if (old != pointer())
			old->Release();
	}
	/* Observers */
	T &operator*(void) const { return *m_ptr; }
	T *operator->(void) const noexcept { return m_ptr; }
	T *get(void) const noexcept { return m_ptr; }
	operator T *(void) const noexcept { return m_ptr; }
	template<typename U> HRESULT QueryInterface(U &);
	template<typename P> P as();

	/* Modifiers */
	T *release(void) noexcept
	{
		T *p = get();
		m_ptr = pointer();
		return p;
	}
	void reset(T *p = pointer(), bool addref = true) noexcept
	{
		if (addref && p != pointer())
			p->AddRef();
		std::swap(m_ptr, p);
		if (p != pointer())
			p->Release();
	}
	void swap(object_ptr &o) noexcept
	{
		std::swap(m_ptr, o.m_ptr);
	}
	object_proxy2<T> operator~(void)
	{
		reset();
		return object_proxy2<T>(&m_ptr);
	}
	object_proxy2<T> operator+(void)
	{
		return object_proxy2<T>(&m_ptr);
	}
	object_ptr &operator=(const object_ptr &o) noexcept
	{
		reset(o.m_ptr, true);
		return *this;
	}
	object_ptr &operator=(object_ptr &&o) noexcept
	{
		auto old = get();
		m_ptr = o.m_ptr;
		o.m_ptr = pointer();
		if (old != pointer())
			old->Release();
		return *this;
	}
	private:
	void operator=(std::nullptr_t) noexcept {}
	void operator&(void) const noexcept {} /* flag everyone */

	T *m_ptr = nullptr;
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

class rowset_ptr {
	public:
	typedef unsigned int size_type;
	typedef SRowSet *pointer;
	rowset_ptr() = default;
	rowset_ptr(SRowSet *p) : m_rp(p) {}
	void operator&() = delete;
	size_type size() const { return m_rp->cRows; }
	const SRow &operator[](size_t i) const { return m_rp->aRow[i]; }
	bool empty() const { return m_rp == nullptr || m_rp->cRows == 0; }
	/*
	 * rowset_ptr can only be turned back into a memory_ptr
	 * subclass when memory_ptr loses its operator T*().
	 */
	SRowSet *operator->() { return m_rp.get(); }
	memory_proxy2<SRowSet> operator~() { return ~m_rp; }
	bool operator==(std::nullptr_t) const { return m_rp == nullptr; }
	bool operator!=(std::nullptr_t) const { return m_rp != nullptr; }
	SRowSet *get() { return m_rp.get(); }
	SRowSet *release() { return m_rp.release(); }
	void reset() { m_rp.reset(); }

	protected:
	memory_ptr<SRowSet, rowset_delete> m_rp;
};

typedef memory_ptr<ADRLIST, rowset_delete> adrlist_ptr;
typedef memory_ptr<ROWLIST, rowset_delete> rowlist_ptr;

template<typename T> inline void
swap(memory_ptr<T> &x, memory_ptr<T> &y) noexcept
{
	x.swap(y);
}

template<typename T> inline void
swap(object_ptr<T> &x, object_ptr<T> &y) noexcept
{
	x.swap(y);
}

template<typename U> static inline constexpr const IID &
iid_of(const object_ptr<U> &)
{
	return iid_of(static_cast<const U *>(nullptr));
}

template<typename T > template<typename U>
HRESULT object_ptr<T>::QueryInterface(U &result)
{
	if (m_ptr == nullptr)
		return MAPI_E_NOT_INITIALIZED;
	typename U::pointer newobj = nullptr;
	HRESULT hr = m_ptr->QueryInterface(iid_of(result), reinterpret_cast<void **>(&newobj));
	if (hr == hrSuccess)
		result.reset(newobj, false);
	/*
	 * Here we check if it makes sense to try to get the requested
	 * interface through the PR_EC_OBJECT object. It only makes
	 * sense to attempt this if the current type is
	 * derived from IMAPIProp. If it is higher than IMAPIProp, no
	 * OpenProperty exists. If it is derived from
	 * ECMAPIProp/ECGenericProp, there is no need to try the
	 * workaround, because we can be sure it is not wrapped by
	 * MAPI.
	 *
	 * The Conversion<IMAPIProp, T>::exists is some
	 * template magic that checks at compile time. We could check
	 * at run time with a dynamic_cast, but we know the current
	 * type at compile time, so why not check it at compile time?
	 */
	else if (hr == MAPI_E_INTERFACE_NOT_SUPPORTED &&
	    std::is_base_of<IMAPIProp, T>::value) {
		memory_ptr<SPropValue> pv;
		if (HrGetOneProp(m_ptr, PR_EC_OBJECT, &~pv) != hrSuccess)
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

} /* namespace */

#endif /* _KC_MEMORY_HPP */
