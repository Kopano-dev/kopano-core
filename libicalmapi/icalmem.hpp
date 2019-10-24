#ifndef ICALMEM_HPP
#define ICALMEM_HPP 1

#include <memory>
#include <libical/ical.h>

class icalmapi_delete {
	public:
	void operator()(icalcomponent *p) const { icalcomponent_free(p); }
	void operator()(icalparameter *p) const { icalparameter_free(p); }
	void operator()(icalproperty *p) const { icalproperty_free(p); }
	void operator()(icaltimezone *p) const { icaltimezone_free(p, true); }
	void operator()(char *p) const { icalmemory_free_buffer(p); }
};

typedef std::unique_ptr<icalcomponent, icalmapi_delete> icalcomp_ptr;
typedef std::unique_ptr<char[], icalmapi_delete> icalmem_ptr;
using icalparam_ptr = std::unique_ptr<icalparameter, icalmapi_delete>;
using icalprop_ptr = std::unique_ptr<icalproperty, icalmapi_delete>;

#endif /* ICALMEM_HPP */
