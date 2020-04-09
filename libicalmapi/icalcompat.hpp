#pragma once
#include <libical/ical.h>

namespace KC {

template<typename T> static inline void kc_ical_utc(T &r, bool utc)
{
#if defined(ICAL_MAJOR_VERSION) && ICAL_MAJOR_VERSION < 3
	r.is_utc = utc;
#else
	r.zone = utc ? icaltimezone_get_utc_timezone() : nullptr;
#endif
}

} /* namespace */
