/*
 * Copyright 2005 - 2016 Zarafa and its licensors
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
 *
 */

#ifndef mapi_array_ptr_INCLUDED
#define mapi_array_ptr_INCLUDED

#include <kopano/zcdefs.h>
#include <utility>

template<typename _T> class mapi_array_proxy _kc_final {
public:
	typedef _T		value_type;
	typedef _T**	pointerpointer;

	mapi_array_proxy(pointerpointer lpp) : m_lpp(lpp) {}

	operator pointerpointer() { return m_lpp; }
	operator LPVOID*() { return (LPVOID*)m_lpp; }

private:
	pointerpointer	m_lpp;
};

template <typename _T> class mapi_array_ptr _kc_final {
public:
	typedef _T						value_type;
	typedef _T*						pointer;
	typedef _T&						reference;
	typedef const _T*				const_pointer;
	typedef const _T&				const_reference;
	typedef mapi_array_proxy<_T>	proxy;

	enum { element_size = sizeof(_T) };

	mapi_array_ptr() : m_lpObject(NULL) {}
	
	mapi_array_ptr(pointer lpObject) : m_lpObject(lpObject) {}
	~mapi_array_ptr() {
		MAPIFreeBuffer(m_lpObject);
		m_lpObject = NULL;
	}

	mapi_array_ptr& operator=(pointer lpObject) {
		if (m_lpObject != lpObject) {
			mapi_array_ptr tmp(lpObject);
			swap(tmp);
		}
		return *this;
	}

	reference operator*() { return *m_lpObject; }
	const_reference operator*() const { return *m_lpObject; }

	// Utility
	void swap(mapi_array_ptr &other) {
		std::swap(m_lpObject, other.m_lpObject);
	}

	operator void*() { return m_lpObject; }
	operator const void*() const { return m_lpObject; }

	proxy operator&() {
		MAPIFreeBuffer(m_lpObject);
		m_lpObject = NULL;
		return proxy(&m_lpObject);
	}

	pointer release() {
		pointer lpTmp = m_lpObject;
		m_lpObject = NULL;
		return lpTmp;
	}

	reference operator[](unsigned i) { return m_lpObject[i]; }
	const_reference operator[](unsigned i) const { return m_lpObject[i]; }

	pointer get() { return m_lpObject; }
	const_pointer get() const { return m_lpObject; }

	void** lppVoid() {
		MAPIFreeBuffer(m_lpObject);
		m_lpObject = NULL;
		return (void**)&m_lpObject;
	}

	bool operator!() const {
		return m_lpObject == NULL;
	}

	unsigned elem_size() const {
		return element_size;
	}
	
private:	// inhibit copying untill refcounting is implemented
	mapi_array_ptr(const mapi_array_ptr &) = delete;
	mapi_array_ptr &operator=(const mapi_array_ptr &) = delete;
	pointer	m_lpObject;
};

#endif // ndef mapi_array_ptr_INCLUDED
