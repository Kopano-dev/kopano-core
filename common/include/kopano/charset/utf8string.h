/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <string>
#include <kopano/charset/traits.h>

namespace KC {

/**
 * @brief	This class represents an UTF-8 string.
 *
 * This class does not expose the same methods as STL's std::string as most of those don't make
 * much sense.
 */
class utf8string KC_FINAL {
public:
	typedef std::string::value_type		value_type;
	typedef std::string::const_pointer	const_pointer;
	typedef std::string::size_type		size_type;

	utf8string() = default;
	utf8string(std::nullptr_t) : m_bNull(true) {}
	utf8string(const utf8string &) = default;
	utf8string(utf8string &&) = default;
	utf8string(size_t n, char c): m_bNull(false), m_str(n, c) {}
	utf8string &operator=(const utf8string &) = default;
	utf8string &operator=(utf8string &&) = default;
	bool null_or_empty() const { return m_bNull || m_str.empty(); }
	const_pointer c_str() const { return m_str.c_str(); }
	const_pointer z_str() const {
		return m_bNull ? NULL : m_str.c_str();
	}

	const_pointer data() const {
		return m_bNull ? NULL : m_str.data();
	}

	const std::string &str() const {
		return m_str;
	}

	size_type size() const {
		return m_str.size();
	}

	bool empty() const {
		return m_str.empty();
	}

	size_type length() const {
		return m_str.length();
	}

	void append(const_pointer lpData, size_t cbData) {
		m_str.append(lpData, cbData);
		m_bNull = false;
	}

	void append(const utf8string &str) {
		m_str.append(str.m_str);
		m_bNull = false;
	}

	void clear() {
		m_str.clear();
	}

private:
	bool m_bNull = false;
	std::string	m_str;
};

template<> class iconv_charset<utf8string> KC_FINAL {
public:
	static const char *name() {
		return "UTF-8";
	}
	static const char *rawptr(const utf8string &from) { return from.z_str(); }
	static size_t rawsize(const utf8string &from) {
		return from.size();
	}
};

} /* namespace */
