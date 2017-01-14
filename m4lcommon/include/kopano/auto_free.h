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

#ifndef AUTO_FREE
#define AUTO_FREE

#include <kopano/zcdefs.h>

namespace KC {

/**
 * Auto Free deallocation template class.
 *
 * Define a deallocation function. The function accept online a function with 
 * one parameter, like void free(void*)
 * 
 * Example usage:
 * @code
 * auto_free<char, auto_free_dealloc<void*, void, free> > auto_free_char;
 *
 * auto_free_char = (char*)malloc(10);
 * @endcode
 */

template<typename _T, typename _R, _R(*_FREE)(_T)>
class auto_free_dealloc _kc_final {
public:
	static inline _R free(_T d) {
		return _FREE(d);
	}
};

/**
 * Auto free template class.
 *
 * This class will delete the object automatically after leaving the scope.
 * You need to specify a deallocation function.
 *
 * @see auto_free_dealloc
 *
 * @todo add copy function and ref counting
 */
template<typename _T, class dealloc = auto_free_dealloc<_T, void, NULL> >
class auto_free _kc_final {
	typedef _T value_type;
	typedef _T* pointer;
	typedef const _T* const_pointer;
public:
	auto_free(void) = default;
	auto_free( pointer p ) : data(p) {}
	~auto_free() {
		free();
	}

	// Assign data
	auto_free& operator=(pointer data) {
		free();
		this->data = data;
		return *this;
	}

	pointer* operator&() {
		free();
		return &data;
	}

	pointer operator->() { return data; }
	const_pointer operator->() const { return data; }
	
	pointer operator*() { return data; }
	const_pointer operator*() const { return data; }

	operator pointer() { return data; }
	operator const_pointer() const { return data; }

	operator void*() { return data; }
	operator const void*() const { return data; }

	bool operator!=(_T* other) const {
		return (data != other);
	}

	bool operator!() const {
		return data == NULL;
	}

	const bool operator()(_T* other) const {
		return data != other;
	}

	/**
	 * Release pointer
	 *
	 * Set the internal pointer to null without destructing the object.
	 * The auto_free is no longer responsible to destruct the object, which means
	 * the object should be destroyed by the caller.
	 *
	 * @return A pointer to the object. After the call, the internal pointer's value is the null pointer (points to no object).
	 */
	pointer release() {
		pointer ret = data;
		data = NULL;
		return ret;
	}

private: // Disable copying, need ref counting
	auto_free(const auto_free &other);
	auto_free& operator=(const auto_free &other);

	void free() {
		if (data == nullptr)
			return;
		dealloc::free(data);
		data = NULL;
	}
	pointer data = nullptr;
};

} /* namespace */

#endif // #ifndef AUTO_FREE

