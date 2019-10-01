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
	 * Parses the contents of a .vcf file and adds recognized VCARDs to the
	 * internal buffer. Returns %MAPI_E_CORRUPT_DATA if no VCARDs were found.
	 */
	virtual HRESULT parse_vcf(const std::string &ical) = 0;

	/**
	 * Pops the next VCARD (contact) available in the internal buffer and
	 * sets the given MAPI message's properties with the data.
	 * Returns %MAPI_E_NOT_FOUND once no more VCARDs are available.
	 */
	virtual HRESULT get_item(IMessage *) = 0;

	protected:
	IMAPIProp *m_propobj;
};

extern KC_EXPORT HRESULT create_vcftomapi(IMAPIProp *, vcftomapi **);

} /* namespace */

#endif
