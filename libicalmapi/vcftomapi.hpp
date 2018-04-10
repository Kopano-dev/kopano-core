#ifndef VCFTOMAPI_H
#define VCFTOMAPI_H

#include <list>
#include <string>
#include <mapidefs.h>
#include "icalitem.h"

namespace KC {

class vcftomapi {
	public:
	vcftomapi(IMAPIProp *o) : m_propobj(o) {}
	virtual ~vcftomapi(void) = default;
	virtual HRESULT parse_vcf(const std::string &ical) = 0;
	virtual HRESULT get_item(IMessage *) = 0;

	protected:
	IMAPIProp *m_propobj;
	std::list<SPropValue> props;
	std::string photo;
	enum photo_type_enum { PHOTO_NONE, PHOTO_JPEG, PHOTO_PNG, PHOTO_GIF } ;
	photo_type_enum phototype = PHOTO_NONE;
};

extern _kc_export HRESULT create_vcftomapi(IMAPIProp *, vcftomapi **);

} /* namespace */

#endif
