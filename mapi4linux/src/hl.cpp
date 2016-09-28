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
#include <mapiutil.h>

namespace KCHL {

KAttach::KAttach(IAttach *attach, unsigned int num) :
	m_attach(attach), m_num(num)
{
}

KAttach::KAttach(KAttach &&other) :
	m_attach(other.m_attach)
{
	other.m_attach = NULL;
}

KAttach::~KAttach(void)
{
	if (m_attach != NULL)
		m_attach->Release();
}

KAttach &KAttach::operator=(KAttach &&other)
{
	std::swap(m_attach, other.m_attach);
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

void KDeleter::operator()(SPropValue *p)
{
	MAPIFreeBuffer(p);
}

void KDeleter::operator()(SRowSet *p)
{
	FreeProws(p);
}

KEntryId::KEntryId(void) :
	m_eid(NULL), m_size(0)
{
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

const char *KMAPIError::what(void) const throw()
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

KPropertyRestriction::KPropertyRestriction(ULONG op, SPropValue *prop)
{
	relop = op;
	if (prop == NULL)
		throw KMAPIError(MAPI_E_INVALID_TYPE);
	ulPropTag = prop->ulPropTag;
	lpProp = prop;
}

KPropertyRestriction::operator SRestriction(void) const
{
	SRestriction r;
	r.rt = RES_PROPERTY;
	r.res.resProperty = *this;
	return r;
}

KSession::KSession(void)
{
	m_session = NULL;
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
	m_store(store)
{
}

KStore::KStore(KStore &&other) :
	m_store(other.m_store)
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
	return *this;
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
