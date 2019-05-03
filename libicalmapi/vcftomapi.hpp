#ifndef VCFTOMAPI_H
#define VCFTOMAPI_H

#include <string>
#include <mapidefs.h>

namespace KC {

class vcftomapi {
	public:
	vcftomapi(IMAPIProp *o) : m_propobj(o) {}
	virtual ~vcftomapi(void) = default;
	virtual HRESULT parse_vcf(const std::string &ical) = 0;
	virtual HRESULT get_item(IMessage *) = 0;

	protected:
	IMAPIProp *m_propobj;
};

extern _kc_export HRESULT create_vcftomapi(IMAPIProp *, vcftomapi **);

} /* namespace */

#endif
