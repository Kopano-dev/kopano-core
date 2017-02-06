#ifndef ICALMEM_HPP
#define ICALMEM_HPP 1

#include <memory>
#include <libical/ical.h>

class icalmapi_delete {
	public:
	void operator()(icalcomponent *p) { icalcomponent_free(p); }
	void operator()(icalproperty *p) { icalproperty_free(p); }
	void operator()(icaltimezone *p) { icaltimezone_free(p, true); }
	void operator()(char *p) { icalmemory_free_buffer(p); }
};

typedef std::unique_ptr<icalcomponent, icalmapi_delete> icalcomp_ptr;
typedef std::unique_ptr<char[], icalmapi_delete> icalmem_ptr;

class icalcomp_ptr_autoconv :
    public std::unique_ptr<icalcomponent, icalmapi_delete> {
	public:
	using std::unique_ptr<icalcomponent, icalmapi_delete>::unique_ptr;
	operator icalcomponent *(void) const { return get(); }
};

#endif /* ICALMEM_HPP */
