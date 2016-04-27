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

#ifndef utf16string_INCLUDED
#define utf16string_INCLUDED

#include <kopano/zcdefs.h>
#include <string>
#include <stdexcept>

#include <kopano/charset/traits.h>

#ifdef LINUX
typedef unsigned short UTF16_CHAR;
typedef std::basic_string<unsigned short> utf16string;

namespace std {

template<>
    struct char_traits<unsigned short>
    {
      typedef unsigned short    char_type;
      typedef wint_t            int_type;
      typedef streamoff         off_type;
      typedef wstreampos        pos_type;
      typedef mbstate_t         state_type;

      static void
      assign(char_type& __c1, const char_type& __c2)
      { __c1 = __c2; }

      static bool
      eq(const char_type& __c1, const char_type& __c2)
      { return __c1 == __c2; }

      static bool
      lt(const char_type& __c1, const char_type& __c2)
      { return __c1 < __c2; }

      static int
      compare(const char_type* __s1, const char_type* __s2, size_t __n)
      { while(*__s1 != 0 && *__s2 != 0 && __n != 0) { 
		if(*__s1 != *__s2) return *__s1 - *__s2;
		++__s1; ++__s2; --__n;
	}
	if((*__s1 == 0 && *__s2 == 0) || __n == 0) return 0;
	if(*__s1 != 0) return -1;
	return 1;
      }

      static size_t
      length(const char_type* __s)
      { int l = 0;
	while (*__s != 0) { ++l; ++__s; }
	return l; 
      }

      static const char_type* 
      find(const char_type* __s, size_t __n, const char_type& __a)
      { while(*__s != 0 && __n > 0) { 
	  if(*__s == __a) return __s; 
	  ++__s; --__n;
	}
	return NULL;
      }

      static char_type* 
      move(char_type* __s1, const char_type* __s2, int_type __n)
      { return (unsigned short *)memmove((void *)__s1, (void *)__s2, __n*sizeof(unsigned short)); }

      static char_type* 
      copy(char_type* __s1, const char_type* __s2, size_t __n)
      { return (unsigned short *)memcpy((void *)__s1, (void *)__s2, __n*sizeof(unsigned short)); }

      static char_type* 
      assign(char_type* __s, size_t __n, char_type __a)
      { char_type *__o = __s;
	while (__n > 0) { *__s = __a; --__n; ++__s; }
	return  __o;
      }

      static char_type 
      to_char_type(const int_type& __c) { return char_type(__c); }

      static int_type 
      to_int_type(const char_type& __c) { return int_type(__c); }

      static bool 
      eq_int_type(const int_type& __c1, const int_type& __c2)
      { return __c1 == __c2; }

      static int_type 
      eof() { return static_cast<int_type>(-1); }

      static int_type 
      not_eof(const int_type& __c)
      { return eq_int_type(__c, eof()) ? 0 : __c; }
  };
}

// 16-bit character specializations
template <>
class iconv_charset<utf16string> _zcp_final {
public:
	static const char *name() {
		return "UTF-16LE";
	}
	static const char *rawptr(const utf16string &from) {
		return reinterpret_cast<const char*>(from.c_str());
	}
	static size_t rawsize(const utf16string &from) {
		return from.size() * sizeof(utf16string::value_type);
	}
};

template <>
class iconv_charset<unsigned short *> _zcp_final {
public:
	static const char *name() {
		return "UTF-16LE";
	}
	static const char *rawptr(const unsigned short *from) {
		return reinterpret_cast<const char*>(from);
	}
	static size_t rawsize(const unsigned short *from);
};

template <>
class iconv_charset<const unsigned short *> _zcp_final {
public:
	static const char *name() {
		return "UTF-16LE";
	}
	static const char *rawptr(const unsigned short *from) {
		return reinterpret_cast<const char*>(from);
	}
	static size_t rawsize(const unsigned short *from);
};

#else
typedef wchar_t UTF16_CHAR;

// The utf16string type
typedef std::wstring utf16string;

#endif

#endif //ndef utf16string_INCLUDED
