#include <string>
#include <climits>
#include <cstdlib>
#include <mapidefs.h>
#include <edkmdb.h>
#include <kopano/ECLogger.h>
#include <kopano/hl.hpp>
#include "ECMemStream.h"
#include "idx_util.hpp"

namespace KC {

mapitable_iterator::mapitable_iterator(IMAPITable *t, size_t pos) :
	m_table(t), m_pos(pos)
{
	load();
}

void mapitable_iterator::load()
{
	auto ret = m_table->QueryRows(65536, 0, &~m_rows);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	m_pos = (m_rows.size() == 0) ? SIZE_MAX : 0;
}

const SRow &mapitable_iterator::operator*()
{
	return m_rows[m_pos];
}

mapitable_iterator &mapitable_iterator::operator++()
{
	++m_pos;
	if (m_pos >= m_rows.size()) {
		m_rows.reset();
		load();
	}
	return *this;
}

bool mapitable_iterator::operator==(const mapitable_iterator &o) const
{
	return m_table == o.m_table && m_pos == o.m_pos;
}

mapitable_iterator mapitable_range::end() { return mapitable_iterator(m_table, SIZE_MAX); }

HRESULT server_guid(IMsgStore *store, GUID &out)
{
	memory_ptr<SPropValue> pv;
	auto ret = HrGetOneProp(store, PR_MAPPING_SIGNATURE, &~pv);
	if (ret != hrSuccess)
		return ret;
	if (pv->Value.bin.cb != sizeof(GUID))
		return MAPI_E_NOT_FOUND;
	memcpy(&out, pv->Value.bin.lpb, sizeof(out));
	return hrSuccess;
}

HRESULT ics_state(IMAPIProp *p, bool assoc, std::string &out)
{
	object_ptr<IExchangeExportChanges> exp;
	auto ret = p->OpenProperty(PR_CONTENTS_SYNCHRONIZER, &IID_IExchangeExportChanges, 0, 0, &~exp);
	if (ret != hrSuccess)
		return kc_perror("OpenProperty", ret);
	ret = exp->Config(nullptr, SYNC_NORMAL | SYNC_CATCHUP | (assoc ? SYNC_ASSOCIATED : 0),
	                  nullptr, nullptr, nullptr, nullptr, 0);
	if (ret != hrSuccess)
		return kc_perror("Exporter::Config", ret);
	unsigned int steps = 0, progress = 0;
	do {
		ret = exp->Synchronize(&steps, &progress);
		if (ret != hrSuccess)
			return ret;
	} while (ret == SYNC_W_PROGRESS);

	object_ptr<IStream> stream;
	ret = CreateStreamOnHGlobal(nullptr, true, &~stream);
	if (ret != hrSuccess)
		return kc_perror("CreateStream", ret);
	ret = exp->UpdateState(stream);
	if (ret != hrSuccess)
		return kc_perror("UpdateState", ret);
	ret = stream->Seek(LARGE_INTEGER{}, STREAM_SEEK_SET, nullptr);
	if (ret != hrSuccess)
		return kc_perror("Seek", ret);
	STATSTG st{};
	ret = stream->Stat(&st, STATFLAG_NONAME);
	if (ret != hrSuccess)
		return kc_perror("Stat", ret);
	if (st.cbSize.QuadPart > 0xFFFFF)
		ec_log_info("K-1701: state larger than 1 MB");
	auto dy = dynamic_cast<ECMemStream *>(stream.get());
	if (dy != nullptr) {
		out.assign(dy->GetBuffer(), dy->GetSize());
		return hrSuccess;
	}
	ec_log_crit("idx_util-1 NOTREACHED");
	abort();
}

} /* namesapce */
