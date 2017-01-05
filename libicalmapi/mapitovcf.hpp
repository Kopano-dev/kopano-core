#ifndef MAPITOVCF_H
#define MAPITOVCF_H

#include <string>
#include <mapidefs.h>

namespace KC {

class mapitovcf {
	public:
	virtual ~mapitovcf(void) = default;
	virtual HRESULT add_message(IMessage *) = 0;
	virtual HRESULT finalize(std::string *) = 0;
};

extern _kc_export HRESULT create_mapitovcf(mapitovcf **);

} /* namespace */

#endif
