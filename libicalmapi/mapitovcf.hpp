#pragma once
#include <string>
#include <mapidefs.h>

namespace KC {

class mapitovcf {
	public:
	virtual ~mapitovcf() = default;
	virtual HRESULT add_message(IMessage *) = 0;
	virtual HRESULT finalize(std::string *) = 0;
};

extern KC_EXPORT HRESULT create_mapitovcf(mapitovcf **);

} /* namespace */
