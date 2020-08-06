#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <kopano/kcodes.h>

enum eServiceType {
	SERVICE_TYPE_EC = 0, /* ZC6+ */
	SERVICE_TYPE_ARCHIVER, /* ZC6+ */
};

enum {
	/* subtypes for eServiceType */
	EC_SERVICE_DEFAULT = 0, /* ZC6+ */
	EC_SERVICE_OUTLOOK = 2, /* ZC6+ */
	EC_SERVICE_OLENABLED = 3,
	EC_SERVICE_BACKUP = 4,
	EC_SERVICE_BES = 10,
	EC_SERVICE_UPDATER = 12,
	EC_SERVICE_EWS = 13,
};

class IMAPISession;
class IMsgStore;

namespace KC {

struct LICENSEREQUEST {
	uint32_t version = 0, tracking_id = 0, service_id = 0;
	char username[252]{};
	uint32_t service_type = 0;
};

struct LICENSERESPONSE {
	uint32_t version = 0, tracking_id = 0;
	uint64_t flags = 0;
	uint32_t status = 0;
	char pad[4]{};
};

extern KC_EXPORT HRESULT licstream_enc(const void *src, size_t src_size, std::string &dst);
extern KC_EXPORT HRESULT licstream_dec(const void *src, size_t src_size, std::string &dst);

}
