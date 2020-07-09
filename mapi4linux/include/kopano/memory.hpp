/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 * Copyright 2016 Kopano and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <memory>
#include <type_traits> /* std::is_base_of */
#include <new>
#include <utility> /* std::swap */
#include <cstdlib>
#include <mapiutil.h> /* MAPIFreeBuffer */
#include <edkmdb.h> /* ROWLIST */
#include <kopano/ECGuid.h>
#include <kopano/ECTags.h>

namespace KC {

template<typename T> class memory_proxy KC_FINAL {
	public:
	memory_proxy(T **p) noexcept : m_ptr(p) {}
	operator T **(void) noexcept { return m_ptr; }
	operator void **() noexcept {
		static_assert(sizeof(void *) == sizeof(T *), "This hack won't work");
		return reinterpret_cast<void **>(m_ptr);
	}

	private:
	T **m_ptr;
};

template<typename T> class memory_proxy2 KC_FINAL {
	public:
	memory_proxy2(T **p) noexcept : m_ptr(p) {}
	memory_proxy<T> operator&(void)
	{
		return memory_proxy<T>(m_ptr);
	}

	private:
	T **m_ptr;
};

template<typename T> class object_proxy KC_FINAL {
	public:
	object_proxy(T **p) noexcept : m_ptr(p) {}
	operator T **(void) noexcept { return m_ptr; }
	operator void **() noexcept { return as<void>(); }
	operator IUnknown **() noexcept { return as<IUnknown>(); }

	private:
	template<typename U> U **as(void) const noexcept
	{
		static_assert(sizeof(U *) == sizeof(T *), "This hack won't work");
		return reinterpret_cast<U **>(m_ptr);
	}

	T **m_ptr;
};

template<> class object_proxy<IUnknown> KC_FINAL {
	public:
	object_proxy(IUnknown **p) noexcept : m_ptr(p) {}
	operator IUnknown **(void) noexcept { return m_ptr; }
	operator void **() noexcept {
		static_assert(sizeof(void *) == sizeof(IUnknown *), "This hack won't work");
		return reinterpret_cast<void **>(m_ptr);
	}

	private:
	IUnknown **m_ptr;
};

template<typename T> class object_proxy2 KC_FINAL {
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
	void reset(const memory_ptr &) noexcept = delete;
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

/* Extra proxy class to forbid AddRef/Release */
template<typename T> class object_rcguard final : public T {
	private:
	virtual unsigned int AddRef() { return T::AddRef(); }
	virtual unsigned int Release() { return T::Release(); }
};

/**
 * Works a bit like shared_ptr, differences being:
 *
 * 1. object_ptr requires that T provides IUnknown functions AddRef and
 *    Release.
 * 2. Due to IUnknown, the control block with the refcounts is always part of
 *    the object (by way of inheritance) rather than being "bolted-on" through
 *    the template.
 * 3. As a result of (2), enable_shared_from_this-like functionality
 *    ("object_ptr<T>(this);") is provided at no extra cost.
 */
template<typename T> class object_ptr {
	public:
	typedef T *pointer;
	constexpr object_ptr(void) noexcept {}
	constexpr object_ptr(std::nullptr_t) noexcept {}
	explicit object_ptr(T *p) : m_ptr(p)
	{
		if (m_ptr != pointer())
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
		reset(o.m_ptr);
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
#ifdef KC_DISALLOW_OBJECTPTR_REFMOD
	object_rcguard<T> *operator->() const noexcept { return reinterpret_cast<object_rcguard<T> *>(m_ptr); }
#else
	T *operator->() const noexcept { return m_ptr; }
#endif
	T *get(void) const noexcept { return m_ptr; }
	operator T *(void) const noexcept { return m_ptr; }

	/* Modifiers */
	T *release(void) noexcept
	{
		T *p = get();
		m_ptr = pointer();
		return p;
	}
	void reset(T *p = pointer()) noexcept
	{
		if (p != pointer())
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
		reset(o.m_ptr);
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

template<typename T> struct mkuniq_helper {
	typedef std::unique_ptr<T> single_object;
};

template<typename T> struct mkuniq_helper<T[]> {
	typedef std::unique_ptr<T[]> array;
};

template<typename T, size_t Z> struct mkuniq_helper<T[Z]> {
	struct invalid_type {};
};

template<typename T, typename... Args> inline typename mkuniq_helper<T>::single_object
make_unique_nt(Args &&...args)
{
	return std::unique_ptr<T>(new(std::nothrow) T(std::forward<Args>(args)...));
}

template<typename T> inline typename mkuniq_helper<T>::array make_unique_nt(size_t z)
{
	return std::unique_ptr<T>(new(std::nothrow) typename std::remove_extent<T>::type[z]);
}

template<typename T, typename... Args> inline typename mkuniq_helper<T>::invalid_type
make_unique_nt(Args &&...) = delete;

} /* namespace */
