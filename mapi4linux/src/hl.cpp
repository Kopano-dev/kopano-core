/*
 *	A RAII-ish interface to libmapi
 */
#include <kopano/platform.h>
#include <utility>
#include <cstdio>
#include <kopano/CommonUtil.h>
#include <kopano/ECDebug.h>
#include <kopano/ECGuid.h>
#include <kopano/ECLogger.h>
#include <kopano/hl.hpp>
#include <kopano/memory.hpp>
#include <mapiutil.h>

using namespace KC;

namespace KCHL {

KProp::KProp(SPropValue *s) :
	m_s(s)
{
}

KProp::KProp(KProp &&other) :
	m_s(other.m_s)
{
	other.m_s = NULL;
}

KProp::~KProp() {
	if (m_s != nullptr)
		MAPIFreeBuffer(m_s);
}

KProp &KProp::operator=(KProp &&other)
{
	std::swap(m_s, other.m_s);
	return *this;
}

unsigned int KProp::prop_type() const
{
	return PROP_TYPE(m_s->ulPropTag);
}

const unsigned int & KProp::prop_tag() const
{
	return m_s->ulPropTag;
}

const bool KProp::b() const
{
	if (PROP_TYPE(prop_tag()) != PT_BOOLEAN)
		throw KMAPIError(MAPI_E_INVALID_TYPE);

	return m_s->Value.b;
}

const unsigned int & KProp::ul() const
{
	if (PROP_TYPE(prop_tag()) != PT_LONG)
		throw KMAPIError(MAPI_E_INVALID_TYPE);

	return m_s->Value.ul;
}

const int & KProp::l() const
{
	if (PROP_TYPE(prop_tag()) != PT_LONG)
		throw KMAPIError(MAPI_E_INVALID_TYPE);

	return m_s->Value.l;
}

std::string KProp::str()
{
	if (PROP_TYPE(prop_tag()) != PT_STRING8)
		throw KMAPIError(MAPI_E_INVALID_TYPE);

	return std::string(m_s->Value.lpszA);
}

std::wstring KProp::wstr()
{
	if (PROP_TYPE(prop_tag()) != PT_UNICODE)
		throw KMAPIError(MAPI_E_INVALID_TYPE);

	return std::wstring(m_s->Value.lpszW);
}

KEntryId KProp::entry_id()
{
	if (PROP_TYPE(prop_tag()) != PT_BINARY)
		throw KMAPIError(MAPI_E_INVALID_TYPE);

	memory_ptr<ENTRYID> entry_id;
	HRESULT ret = MAPIAllocateBuffer(m_s->Value.bin.cb, &~entry_id);

	if (ret != hrSuccess)
		throw KMAPIError(ret);

	memcpy(entry_id, m_s->Value.bin.lpb, m_s->Value.bin.cb);

	return KEntryId(entry_id.release(), m_s->Value.bin.cb);
}

KAttach::KAttach(IAttach *attach, unsigned int num) :
	m_attach(attach), m_num(num)
{
}

KAttach::KAttach(KAttach &&other) :
	m_attach(other.m_attach), m_num(other.m_num)
{
	other.m_attach = NULL;
	other.m_num = 0;
}

KAttach::~KAttach(void)
{
	if (m_attach != NULL)
		m_attach->Release();
}

KAttach &KAttach::operator=(KAttach &&other)
{
	std::swap(m_attach, other.m_attach);
	std::swap(m_num, other.m_num);
	return *this;
}

KStream KAttach::open_property_stream(unsigned int tag, unsigned int intopts,
    unsigned int flags)
{
	IStream *stream;
	int ret = m_attach->OpenProperty(tag, &IID_IStream, intopts, flags,
	          reinterpret_cast<LPUNKNOWN *>(&stream));
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KStream(stream);
}

HRESULT KAttach::save_changes(unsigned int flags)
{
	return m_attach->SaveChanges(flags);
}

KEntryId::KEntryId(ENTRYID *eid, size_t size) :
	m_eid(eid), m_size(size)
{
}

KEntryId::KEntryId(KEntryId &&other) :
	m_eid(other.m_eid), m_size(other.m_size)
{
	other.m_eid = NULL;
}

KEntryId::~KEntryId(void)
{
	MAPIFreeBuffer(m_eid);
}

KEntryId &KEntryId::operator=(KEntryId &&other)
{
	std::swap(m_eid, other.m_eid);
	other.m_size = 0;
	return *this;
}

KMAPIError::KMAPIError(HRESULT code) :
	m_code(code), m_message(GetMAPIErrorDescription(m_code))
{
}

const char *KMAPIError::what(void) const noexcept
{
	return m_message.c_str();
}

KFolder::KFolder(IMAPIFolder *folder) :
	m_folder(folder)
{
}

KFolder::KFolder(KFolder &&other) :
	m_folder(other.m_folder)
{
	other.m_folder = NULL;
}

KFolder::~KFolder(void)
{
	if (m_folder != NULL)
		m_folder->Release();
}

KFolder &KFolder::operator=(KFolder &&other)
{
	std::swap(m_folder, other.m_folder);
	return *this;
}

KMessage KFolder::create_message(LPCIID intf, unsigned int flags)
{
	IMessage *message;
	int ret = m_folder->CreateMessage(intf, flags, &message);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KMessage(message);
}

KTable KFolder::get_contents_table(unsigned int flags)
{
	IMAPITable *table;
	int ret = m_folder->GetContentsTable(flags, &table);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KTable(table);
}

KTable KFolder::get_hierarchy_table(unsigned int flags)
{
	IMAPITable *table;
	int ret = m_folder->GetHierarchyTable(flags, &table);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KTable(table);
}

KProp KFolder::get_prop(unsigned int tag)
{
	SPropValue *prop;
	auto ret = HrGetOneProp(m_folder, tag, &prop);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return prop;
}

KMessage::KMessage(IMessage *message) :
	m_message(message)
{
}

KMessage::KMessage(KMessage &&other) :
	m_message(other.m_message)
{
	other.m_message = NULL;
}

KMessage::~KMessage(void)
{
	if (m_message != NULL)
		m_message->Release();
}

KMessage &KMessage::operator=(KMessage &&other)
{
	std::swap(m_message, other.m_message);
	return *this;
}

KAttach KMessage::create_attach(LPCIID intf, unsigned int flags)
{
	IAttach *atm;
	ULONG num;
	int ret = m_message->CreateAttach(intf, flags, &num, &atm);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KAttach(atm, num);
}

KProp KMessage::get_prop(unsigned int tag)
{
	SPropValue *prop;
	int ret = HrGetOneProp(m_message, tag, &prop);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KProp(prop);
}

HRESULT KMessage::save_changes(unsigned int flags)
{
	return m_message->SaveChanges(flags);
}

HRESULT KMessage::set_read_flag(unsigned int f)
{
	return m_message->SetReadFlag(f);
}

KSession::KSession(void)
{
	const char *sock = getenv("KOPANO_SOCKET");
	if (sock == NULL)
		sock = "default:";
	int ret = HrOpenECSession(&m_session, "app_vers", "app_misc",
	          L"SYSTEM", L"", sock, 0, 0, 0);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
}

KSession::KSession(const wchar_t *user, const wchar_t *pass)
{
	m_session = NULL;
	const char *sock = getenv("KOPANO_SOCKET");
	if (sock == NULL)
		sock = "default:";
	int ret = HrOpenECSession(&m_session, "app_vers", "app_misc",
	          user, pass, sock, 0, 0, 0);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
}

KSession::KSession(IMAPISession *session) :
	m_session(session)
{
}

KSession::~KSession(void)
{
	if (m_session != NULL)
		m_session->Release();
}

KStore KSession::open_default_store(void)
{
	IMsgStore *m_store = NULL;
	int ret = HrOpenDefaultStore(m_session, &m_store);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KStore(m_store);
}

KStore::KStore(IMsgStore *store) :
	m_store(store), m_type(0)
{
}

KStore::KStore(KStore &&other) :
	m_store(other.m_store), m_type(other.m_type)
{
	other.m_store = NULL;
}

KStore::~KStore(void)
{
	if (m_store != NULL)
		m_store->Release();
}

KStore &KStore::operator=(KStore &&other)
{
	std::swap(m_store, other.m_store);
	std::swap(m_type, other.m_type);
	return *this;
}

KEntryId KStore::get_receive_folder(const char *cls, char **xcls)
{
	ULONG eid_size = 0;
	ENTRYID *raw_eid = nullptr;
	auto ret = m_store->GetReceiveFolder(reinterpret_cast<TCHAR *>(const_cast<char *>(cls)),
	           0, &eid_size, &raw_eid, reinterpret_cast<TCHAR **>(xcls));
	KEntryId eid(raw_eid, eid_size); /* stuff into RAII object before throw */
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return eid;
}

KProp KStore::get_prop(unsigned int tag)
{
	SPropValue *prop;
	int ret = HrGetOneProp(m_store, tag, &prop);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KProp(prop);
}

KUnknown KStore::open_entry(const KEntryId &eid, LPCIID intf,
    unsigned int flags)
{
	IUnknown *unk;
	auto ret = m_store->OpenEntry(eid.m_size, eid.m_eid,
                   intf, flags, &m_type, &unk);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KUnknown(unk);
}

KUnknown KStore::open_entry(const SPropValue *eid, LPCIID intf,
    unsigned int flags)
{
	IUnknown *unk;
	int ret;
	if (eid == NULL) {
		ret = m_store->OpenEntry(0, NULL, intf, flags, &m_type, &unk);
	} else {
		if (PROP_TYPE(eid->ulPropTag) != PT_BINARY)
			throw KMAPIError(MAPI_E_INVALID_TYPE);
		ret = m_store->OpenEntry(eid->Value.bin.cb,
		      reinterpret_cast<ENTRYID *>(eid->Value.bin.lpb),
	              intf, flags, &m_type, &unk);
	}
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KUnknown(unk);
}

KStream::KStream(IStream *stream) :
	m_stream(stream)
{
}

KStream::KStream(KStream &&other) :
	m_stream(other.m_stream)
{
	other.m_stream = NULL;
}

KStream::~KStream(void)
{
	if (m_stream != NULL)
		m_stream->Release();
}

KStream &KStream::operator=(KStream &&other)
{
	std::swap(m_stream, other.m_stream);
	return *this;
}

HRESULT KStream::write(const std::string &s)
{
	return m_stream->Write(s.c_str(), s.length(), 0);
}

HRESULT KStream::commit(unsigned int flags)
{
	return m_stream->Commit(flags);
}

KRow::KRow(SRow row) :
	m_row(row)
{
}

KProp KRow::operator[](size_t index) const
{
	memory_ptr<SPropValue> prop;

	HRESULT ret = MAPIAllocateBuffer(sizeof(SPropValue), &~prop);
	if (ret != hrSuccess)
		throw KMAPIError(ret);

	memcpy(prop, &m_row.lpProps[index], sizeof(SPropValue));

	return KProp(prop.release());
}

unsigned int KRow::count() const
{
	return m_row.cValues;
}

KRowSet::KRowSet(SRowSet *rowset) :
	m_rowset(rowset)
{
}

KRow KRowSet::operator[](size_t index) const
{
	return KRow(m_rowset->aRow[index]);
}

unsigned int KRowSet::count() const
{
	return m_rowset->cRows;
}

KTable::KTable(IMAPITable *table) :
	m_table(table)
{
}

KTable::KTable(KTable &&other) :
	m_table(other.m_table)
{
	other.m_table = NULL;
}

KTable::~KTable(void)
{
	if (m_table != NULL)
		m_table->Release();
}

KTable &KTable::operator=(KTable &&other)
{
	std::swap(m_table, other.m_table);
	return *this;
}

HRESULT KTable::restrict(const SRestriction &r, unsigned int flags)
{
	return m_table->Restrict(const_cast<SRestriction *>(&r), flags);
}

void KTable::columns(std::initializer_list<unsigned int> props, unsigned int flags)
{
	size_t len = props.size();
	memory_ptr<SPropTagArray> array;

	HRESULT ret = MAPIAllocateBuffer(CbNewSPropTagArray(len), &~array);
	if (ret != hrSuccess)
		throw KMAPIError(ret);

	array->cValues = len;

	int i = 0;
	for (const auto &prop : props) {
		array->aulPropTag[i] = prop;
		i++;
	}

	ret = m_table->SetColumns(array, flags);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
}

void KTable::sort(std::initializer_list<std::pair<unsigned int, KTable::SortOrder> > sort_order, unsigned int flags)
{
	size_t len = sort_order.size();
	memory_ptr<SSortOrderSet> array;

	auto ret = MAPIAllocateBuffer(CbNewSSortOrderSet(len), &~array);
	if (ret != hrSuccess)
		throw KMAPIError(ret);

	array->cCategories = 0;
	array->cExpanded = 0;
	array->cSorts = len;

	size_t i = 0;
	for (const auto &pair : sort_order) {
		array->aSort[i].ulPropTag = pair.first;
		array->aSort[i].ulOrder = pair.second;
		++i;
	}

	ret = m_table->SortTable(array, flags);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
}

KRowSet KTable::rows(unsigned int size, unsigned int offset)
{
	rowset_ptr rowset;

	HRESULT ret = m_table->QueryRows(size, offset, &~rowset);
	if (ret != hrSuccess)
		throw KMAPIError(ret);

	return KRowSet(rowset.release());
}

unsigned int KTable::count(unsigned int flags)
{
	unsigned int result;

	auto ret = m_table->GetRowCount(flags, &result);
	if (ret != hrSuccess)
		throw KMAPIError(ret);

	return result;
}

KUnknown::KUnknown(IUnknown *p) :
	m_ptr(p)
{
}

KUnknown::KUnknown(KUnknown &&other) :
	m_ptr(other.m_ptr)
{
	other.m_ptr = NULL;
}

KUnknown::~KUnknown(void)
{
	if (m_ptr != NULL)
		m_ptr->Release();
}

KUnknown &KUnknown::operator=(KUnknown &&other)
{
	std::swap(m_ptr, other.m_ptr);
	return *this;
}

KUnknown::operator KFolder(void) const
{
	IMAPIFolder *f = NULL;
	if (m_ptr->QueryInterface(IID_IMAPIFolder, reinterpret_cast<void **>(&f)) == hrSuccess)
		/* QI implies AddRef */
		return KFolder(f);
	throw KMAPIError(MAPI_E_INVALID_TYPE);
}

KUnknown::operator KMessage(void) const
{
	IMessage *f = NULL;
	if (m_ptr->QueryInterface(IID_IMessage, reinterpret_cast<void **>(&f)) == hrSuccess)
		/* QI implies AddRef */
		return KMessage(f);
	throw KMAPIError(MAPI_E_INVALID_TYPE);
}

} /* namespace KCHL */
