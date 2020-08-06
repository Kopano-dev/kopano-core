/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005–2016 Zarafa and its licensors
 */
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <json/reader.h>
#include <mapicode.h>
#include <mapidefs.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECGuid.h>
#include <kopano/ECLogger.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/license.hpp>
#include <kopano/memory.hpp>
#include <kopano/platform.h>
#include <kopano/userutil.h>

namespace KC {

/* Issuess a standalone license request (i.e. not as part of HrLogon) */
HRESULT lic_validate(IMsgStore *store, enum eServiceType svc_type,
    unsigned int svc_id)
{
	unsigned int tracking_id = rand_mt();
	LICENSEREQUEST req_bin;
	req_bin.version      = cpu_to_be32(0);
	req_bin.tracking_id  = cpu_to_be32(tracking_id);
	req_bin.service_id   = cpu_to_be32(svc_id);
	req_bin.service_type = cpu_to_be32(svc_type);
	memset(&req_bin.username, 0, sizeof(req_bin.username));

	std::string req_enc, rsp_enc, rsp_str;
	auto ret = licstream_enc(&req_bin, sizeof(req_bin), req_enc);
	if (ret != hrSuccess)
		return ret;
	object_ptr<IECLicense> intf;
	ret = store->QueryInterface(IID_IECLicense, &~intf);
	if (ret != hrSuccess)
		return ret;
	ret = intf->license_auth(std::move(req_enc), rsp_enc);
	if (ret != hrSuccess)
		return ret;
	ret = licstream_dec(rsp_enc.data(), rsp_enc.size(), rsp_str);
	if (ret != hrSuccess)
		return ret;

	LICENSERESPONSE rsp_bin;
	if (rsp_str.size() < sizeof(rsp_bin))
		return MAPI_E_INVALID_PARAMETER;
	memcpy(&rsp_bin, rsp_str.data(), std::min(sizeof(rsp_bin), rsp_str.size()));
	if (be32_to_cpu(rsp_bin.tracking_id) != tracking_id)
		return MAPI_E_NO_ACCESS;
	ret = rsp_bin.status = be32_to_cpu(rsp_bin.status);
	Json::Value root;
	std::istringstream sin(rsp_str.substr(sizeof(rsp_bin)));
	auto valid_json = Json::parseFromStream(Json::CharReaderBuilder(), sin, &root, nullptr);
	if (ret != hrSuccess) {
		if (valid_json && root.isMember("ers"))
			return hr_lerrf(ret, "%s", root["ers"].asCString());
		return ret;
	}
	return hrSuccess;
}

}
