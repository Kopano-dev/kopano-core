#ifndef VCFTOMAPI_H
#define VCFTOMAPI_H

#include <string>
#include <mapidefs.h>

namespace KC {

class vcftomapi {
	public:
	/**
	 * @o: a MAPI object to use for resolving named properties
	 */
	vcftomapi(IMAPIProp *o) : m_propobj(o) {}
	virtual ~vcftomapi(void) = default;

	/**
	 * Parses the contents of a .vcf file and adds recognized vCards to the
	 * internal buffer. Returns %MAPI_E_CORRUPT_DATA if no vCards were found.
	 */
	virtual HRESULT parse_vcf(const std::string &ical) = 0;
#ifndef SWIG
	virtual HRESULT parse_vcf(std::string &&ical) = 0;
#endif

	virtual size_t get_item_count() = 0;

	/**
	 * Retrieve the selected vCard (contact) available in the internal
	 * buffers and sets the given MAPI message's properties with the data.
	 */
	virtual HRESULT get_item(IMessage *, unsigned int section = 0) = 0;

	protected:
	IMAPIProp *m_propobj;
};

extern KC_EXPORT HRESULT create_vcftomapi(IMAPIProp *, vcftomapi **);

} /* namespace */

#endif
