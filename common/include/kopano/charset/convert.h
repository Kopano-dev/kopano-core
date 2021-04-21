/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <map>
#include <set>
#include <list>
#include <string>
#include <stdexcept>
#include <iconv.h>
#include <kopano/charset/traits.h>
#include <kopano/charset/utf8string.h>
#include <mapidefs.h>

namespace KC {

/**
 * @brief	Exception class
 */
class KC_EXPORT_THROW convert_exception : public std::runtime_error {
public:
	enum exception_type {
		eUnknownCharset,
		eIllegalSequence
	};
	convert_exception(enum exception_type type, const std::string &message);

	enum exception_type type() const {
		return m_type;
	}

private:
	enum exception_type m_type;
};

/**
 * @brief	Unknown charset
 */
class KC_EXPORT_THROW unknown_charset_exception KC_FINAL :
    public convert_exception {
	public:
	unknown_charset_exception(const std::string &message);
};

/**
 * @brief	Illegal sequence
 */
class KC_EXPORT_THROW illegal_sequence_exception KC_FINAL :
    public convert_exception {
	public:
	illegal_sequence_exception(const std::string &message);
};

/**
 * @brief	Performs the generic iconv processing.
 */
class KC_EXPORT iconv_context_base {
	public:
	virtual ~iconv_context_base();

	protected:
	/**
	 * @param[in]  tocode		The destination charset.
	 * @param[out] fromcode		The source charset.
	 */
	iconv_context_base(const char* tocode, const char* fromcode);

	/**
	 * @brief Performs the actual conversion.
	 *
	 * Performs the conversion and stores the result in the output string
	 * by calling append, which must be overridden by a derived class.
	 * @param[in] lpFrom	Pointer to the source data.
	 * @param[in] cbFrom	Size of the source data in bytes.
	 */
	void doconvert(const char *lpFrom, size_t cbFrom);

	private:
	/**
	 * @brief Appends converted data to the result.
	 *
	 * @param[in] lpBuf		Pointer to the data to be appended.
	 * @param[in] cbBuf		Size of the data to be appended in bytes.
	 */
	KC_HIDDEN virtual void append(const char *buf, size_t bufsize) = 0;

	iconv_t	m_cd = reinterpret_cast<iconv_t>(-1);
	bool m_bForce = true; /* Ignore illegal sequences by default. */
	bool m_bHTML = false, m_translit_run = false;
	unsigned int m_translit_adv = 1;

	iconv_context_base(const iconv_context_base &) = delete;
	iconv_context_base &operator=(const iconv_context_base &) = delete;
};

/**
 * @brief	Default converter from one charset to another with string types.
 */
template<typename To_Type, typename From_Type>
class KC_EXPORT_DYCAST iconv_context KC_FINAL :
    public iconv_context_base {
	public:
	/**
	 * Constructs a iconv_context_base with the right tocode and fromcode based
	 * on the To_Type and From_Type template parameters.
	 */
	iconv_context() :
		iconv_context_base(iconv_charset<To_Type>::name(), iconv_charset<From_Type>::name())
	{}

	/**
	 * Constructs a iconv_context_base with the tocode based on the To_Type
	 * and the passed fromcode.
	 */
	iconv_context(const char *fromcode) :
		iconv_context_base(iconv_charset<To_Type>::name(), fromcode)
	{}

	/**
	 * Constructs a iconv_context_base with the tocode based on the To_Type
	 * and the passed fromcode.
	 */
	iconv_context(const char *tocode, const char *fromcode)
		: iconv_context_base(tocode, fromcode)
	{}

	/**
	 * @brief Performs the conversion.
	 *
	 * The actual conversion in delegated to iconv_context_base.
	 * @param[in] lpRaw		Raw pointer to the data to be converted.
	 * @param[in] cbRaw		The size in bytes of the data to be converted.
	 * @return				The converted string.
	 */
	To_Type convert(const char *lpRaw, size_t cbRaw)
	{
		m_to.clear();
		doconvert(lpRaw, cbRaw);
		return m_to;
	}

	/**
	 * @brief Performs the conversion.
	 *
	 * The actual conversion in delegated to iconv_context_base.
	 * @param[in] _from		The string to be converted.
	 * @return				The converted string.
	 */
	To_Type convert(const From_Type &from)
	{
		return convert(iconv_charset<From_Type>::rawptr(from),
		       iconv_charset<From_Type>::rawsize(from));
	}

	private:
	KC_HIDDEN void append(const char *lpBuf, size_t cbBuf) KC_OVERRIDE
	{
		m_to.append(reinterpret_cast<typename To_Type::const_pointer>(lpBuf),
			cbBuf / sizeof(typename To_Type::value_type));
	}

	To_Type	m_to;
};

/**
 * @brief	Converts a string to a string with a different charset.
 *
 * convert_to comes in three forms:
 *
 * 1. convert_to<dsttype>(dstcharset, srcstring, srcsize, srccharset)
 * 2. convert_to<dsttype>(srcstring, srcsize, srccharset)
 *    autoderived: dstcharset (from dsttype)
 *    see iconv_charset<dsttype>::name() for the charset that will be assumed
 * 3. convert_to<dsttype>(srcstring)
 *    autoderived: dstcharset, srcsize, srccharset
 *
 * Derivation happens with iconv_charset<> where the defaults are set.
 *
 * This is the function to call when a one of conversion from one charset to
 * another is required.
 * @tparam	  To_Type		The type of the destination string.
 * @param[in] _from			The string that is to be converted to another charset.
 * @return					The converted string.
 *
 * @note	Since this method needs to create an iconv object internally
 *			it is better to use a convert_context when multiple conversions
 *			need to be performed.
 */
template<typename To_Type, typename From_Type>
inline To_Type convert_to(const From_Type &from)
{
	static_assert(!std::is_same<To_Type, From_Type>::value, "pointless conversion");
	iconv_context<To_Type, From_Type> context;
	return context.convert(from);
}

template<typename To_Type, typename From_Type> inline To_Type
convert_to(const From_Type &from, size_t cbBytes, const char *fromcode)
{
	iconv_context<To_Type, From_Type> context(fromcode);
	return context.convert(iconv_charset<From_Type>::rawptr(from), cbBytes);
}

template<typename To_Type, typename From_Type>
inline To_Type convert_to(const char *tocode, const From_Type &from,
    size_t cbBytes, const char *fromcode)
{
	iconv_context<To_Type, From_Type> context(tocode, fromcode);
	return context.convert(iconv_charset<From_Type>::rawptr(from), cbBytes);
}


/**
 * @brief	Allows multiple conversions within the same context.
 *
 * The convert_context class is used to perform multiple conversions within the
 * same context. This basically means that the iconv_context classes can
 * be reused, removing the need to recreate them for each conversion.
 */
class KC_EXPORT convert_context KC_FINAL {
public:
	convert_context() = default;

	/**
	 * @brief	Converts a string to a string with a different charset.
	 *
	 * The to- and from charsets are implicitly determined by on one side the
	 * passed To_Type and on the other side the _from argument.
	 * @tparam	  To_Type		The type of the destination string.
	 * @param[in] _from			The string that is to be converted to another charset.
	 * @return					The converted string.
	 */
	template<typename To_Type, typename From_Type>
	KC_HIDDEN To_Type convert_to(const From_Type &from)
	{
		static_assert(!std::is_same<To_Type, From_Type>::value, "pointless conversion");
		return get_context<To_Type, From_Type>().convert(from);
	}

	/**
	 * @brief	Converts a string to a string with a different charset.
	 *
	 * The to charset is implicitly determined by the passed To_Type.
	 * The from charset is passed in fromcode.
	 * @tparam	  To_Type		The type of the destination string.
	 * @param[in] _from			The string that is to be converted to another charset.
	 * @param[in] cbBytes		The size in bytes of the string to convert.
	 * @param[in] fromcode		The source charset.
	 * @return					The converted string.
	 */
	template<typename To_Type, typename From_Type>
	KC_HIDDEN To_Type convert_to(const From_Type &from, size_t cbBytes,
	    const char *fromcode)
	{
		return get_context<To_Type, From_Type>(fromcode).convert(
			iconv_charset<From_Type>::rawptr(from), cbBytes);
	}

	/**
	 * @brief	Converts a string to a string with a different charset.
	 *
	 * The to charset is passed in tocode.
	 * The from charset is passed in fromcode.
	 * @param[in] tocode		the destination charset.
	 * @param[in] _from			The string that is to be converted to another charset.
	 * @param[in] cbBytes		The size in bytes of the string to convert.
	 * @param[in] fromcode		The source charset.
	 * @return					The converted string.
	 */
	template<typename To_Type, typename From_Type>
	KC_HIDDEN To_Type convert_to(const char *tocode,
	    const From_Type &from, size_t cbBytes, const char *fromcode)
	{
		return get_context<To_Type, From_Type>(tocode, fromcode).convert(
			iconv_charset<From_Type>::rawptr(from), cbBytes);
	}

private:
	/**
	 * @brief Key for the context_map;
	 */
	struct KC_EXPORT context_key {
		std::string toType;
		std::string toCode;
		std::string fromType;
		std::string fromCode;

		bool operator<(const context_key &lhs) const
		{
			return std::tie(fromType, toType, fromCode, toCode) <
				std::tie(lhs.fromType, lhs.toType, lhs.fromCode, lhs.toCode);
		}
	};

	/** Create a context_key based on the to- and from types and optionally the to- and from codes.
	 *
	 * @tparam	To_Type
	 *			The destination type.
	 * @tparam	From_Type
	 *			The source type.
	 * @param[in]	tocode
	 *			The destination encoding. NULL for autodetect (based on To_Type).
	 * @param[in]	fromcode
	 *			The source encoding. NULL for autodetect (based onFrom_Type).
	 *
	 * @return	The new context_key
	 */
	template<typename To_Type, typename From_Type>
	KC_HIDDEN context_key create_key(const char *tocode,
	    const char *fromcode)
	{
		context_key key = {
			typeid(To_Type).name(),
			(tocode ? tocode : iconv_charset<To_Type>::name()),
			typeid(From_Type).name(),
			(fromcode ? fromcode : iconv_charset<From_Type>::name())
		};
		return key;
	}

	/**
	 * @brief Obtains an iconv_context object.
	 *
	 * The correct iconv_context is based on To_Type and From_Type and is
	 * obtained from the context_map. If the correct iconv_context is not found, a new
	 * one is created and stored in the context_map;
	 * @tparam	To_Type	The type of the destination string.
	 * @tparam	From_Type	The type of the source string.
	 * @return				A pointer to a iconv_context.
	 */
	template<typename To_Type, typename From_Type>
	KC_HIDDEN iconv_context<To_Type, From_Type>& get_context()
	{
		context_key key(create_key<To_Type, From_Type>(NULL, NULL));
		auto iContext = m_contexts.find(key);
		if (iContext == m_contexts.cend()) {
			iContext = m_contexts.emplace(
				key, std::make_unique<iconv_context<To_Type, From_Type>>()).first;
		}
		return dynamic_cast<iconv_context<To_Type, From_Type> &>(*iContext->second.get());
	}

	/**
	 * @brief Obtains an iconv_context object.
	 *
	 * The correct iconv_context is based on To_Type and fromcode and is
	 * obtained from the context_map. If the correct iconv_context is not found, a new
	 * one is created and stored in the context_map;
	 * @tparam		To_Type	The type of the destination string.
	 * @param[in]	fromcode	The source charset.
	 * @return					A pointer to a iconv_context.
	 */
	template<typename To_Type, typename From_Type>
	KC_HIDDEN iconv_context<To_Type, From_Type>&
	get_context(const char *fromcode)
	{
		context_key key(create_key<To_Type, From_Type>(NULL, fromcode));
		auto iContext = m_contexts.find(key);
		if (iContext == m_contexts.cend()) {
			auto lpContext = new iconv_context<To_Type, From_Type>(fromcode);
			iContext = m_contexts.emplace(key, lpContext).first;
		}

		return dynamic_cast<iconv_context<To_Type, From_Type> &>(*iContext->second.get());
	}

	/**
	 * @brief Obtains an iconv_context object.
	 *
	 * The correct iconv_context is based on tocode and fromcode and is
	 * obtained from the context_map. If the correct iconv_context is not found, a new
	 * one is created and stored in the context_map;
	 * @param[in]	tocode		The destination charset.
	 * @param[in]	fromcode	The source charset.
	 * @return					A pointer to a iconv_context.
	 */
	template<typename To_Type, typename From_Type>
	KC_HIDDEN iconv_context<To_Type, From_Type>&
	get_context(const char *tocode, const char *fromcode)
	{
		context_key key(create_key<To_Type, From_Type>(tocode, fromcode));
		auto iContext = m_contexts.find(key);
		if (iContext == m_contexts.cend()) {
			auto lpContext = new iconv_context<To_Type, From_Type>(tocode, fromcode);
			iContext = m_contexts.emplace(key, lpContext).first;
		}
		return dynamic_cast<iconv_context<To_Type, From_Type> &>(*iContext->second.get());
	}

	/**
	 * @brief Flags that determine which code of a context_key is persisted
	 */
	enum {
		pfToCode = 1,
		pfFromCode = 2
	};

	std::map<context_key, std::unique_ptr<iconv_context_base>> m_contexts;
	std::list<std::string>	m_lstStrings;
	std::list<std::wstring>	m_lstWstrings;

// a convert_context is not supposed to be copyable.
	convert_context(const convert_context &) = delete;
	convert_context &operator=(const convert_context &) = delete;
};

extern KC_EXPORT HRESULT HrFromException(const convert_exception &);

/*
 * Even if the definition of TCHAR is different between ASCII and Unicode
 * builds, some of the oldest functions in the MAPI spec have the Unicodeness
 * of arguments conveyed by a flags argument, and the char type means nothing.
 */
inline utf8string tfstring_to_utf8(const TCHAR *s, unsigned int fl)
{
	if (s == nullptr)
		return utf8string(nullptr);
	return (fl & MAPI_UNICODE) ? convert_to<utf8string>(reinterpret_cast<const wchar_t *>(s)) :
	       convert_to<utf8string>(reinterpret_cast<const char *>(s));
}

inline std::string tfstring_to_lcl(const TCHAR *s, unsigned int fl)
{
	if (s == nullptr)
		return {};
	return (fl & MAPI_UNICODE) ? convert_to<std::string>(reinterpret_cast<const wchar_t *>(s)) :
	       convert_to<std::string>(reinterpret_cast<const char *>(s));
}

#ifdef MAPIDEFS_H

/**
 * @brief	Converts a string from one charset to another. Failure is indicated
 *			through the return code instead of an exception.
 *
 * @param[in]  _from	The string to be converted.
 * @param[out] _to		The converted string.
 * @return				HRESULT.
 */
template<typename To_Type, typename From_Type>
HRESULT TryConvert(const From_Type &from, To_Type &to)
{
	try {
		to = convert_to<To_Type>(from);
		return hrSuccess;
	} catch (const convert_exception &ce) {
		return HrFromException(ce);
	}
}


/**
 * @brief	Converts a string from one charset to another. Failure is indicated
 *			through the return code instead of an exception.
 *
 * @param[in]  _from	The string to be converted.
 * @param[in] cbBytes	The size in bytes of the string to convert.
 * @param[in]  fromcode The source charset.
 * @param[out] _to		The converted string.
 * @return				HRESULT.
 */
template<typename To_Type, typename From_Type>
HRESULT TryConvert(const From_Type &from, size_t cbBytes,
    const char *fromcode, To_Type &to)
{
	try {
		to = convert_to<To_Type>(from, cbBytes, fromcode);
		return hrSuccess;
	} catch (const convert_exception &ce) {
		return HrFromException(ce);
	}
}

#endif // MAPIDEFS_H

} /* namespace */
