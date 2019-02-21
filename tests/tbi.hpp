/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright 2017, Kopano and its licensors */
/*
 *	A throw-based interface (TBI) for MAPI
 *
 *	Quite unpopular in KC. For tests, we do not care so much if
 *	anything throws out of the blue, though.
 */
#ifndef TBI_HPP
#define TBI_HPP 1

#include <initializer_list>
#include <memory>
#include <utility>
#include <cstdlib>
#include <mapidefs.h>
#include <kopano/CommonUtil.h>
#include <kopano/hl.hpp>
#include <kopano/memory.hpp>

namespace KC {

class KStream : public object_ptr<IStream> {
	public:
	using object_ptr::object_ptr;
	HRESULT commit(unsigned int flags = 0) { return (*this)->Commit(flags); }
	HRESULT write(const std::string &s) { return (*this)->Write(s.c_str(), s.length(), 0); }
};

class KAttach : public object_ptr<IAttach> {
	public:
	using object_ptr::object_ptr;
	inline KStream open_property_stream(unsigned int tag, unsigned int iopts, unsigned int flags);
	HRESULT save_changes(unsigned int flags = 0) { return (*this)->SaveChanges(flags); }
};

class KProp : public std::unique_ptr<SPropValue, default_delete> {
	public:
	using unique_ptr::unique_ptr;
	inline const SBinary &entry_id() const;
	inline ULONG ul() const;
};

class KMessage : public object_ptr<IMessage> {
	public:
	using object_ptr::object_ptr;
	inline KAttach create_attach(unsigned int flags = 0);
	HRESULT save_changes(unsigned int flags = 0) { return (*this)->SaveChanges(flags); }
	HRESULT set_read_flag(unsigned int flag) { return (*this)->SetReadFlag(flag); }
	inline KProp get_prop(unsigned int tag);
	
};

class KTable : public object_ptr<IMAPITable> {
	public:
	using object_ptr::object_ptr;
	inline KTable &columns(const std::initializer_list<unsigned int> &, unsigned int = 0);
	inline rowset_ptr rows(unsigned int size = 65535, unsigned int offset = 0);
};

class KFolder : public object_ptr<IMAPIFolder> {
	public:
	using object_ptr::object_ptr;
	inline KMessage create_message(unsigned int flags = 0);
	inline KTable get_contents_table(unsigned int flags = 0);
};

class KStore : public object_ptr<IMsgStore> {
	public:
	using object_ptr::object_ptr;
	inline KMessage open_message(const KProp &eid, unsigned int flags = 0);
	inline KFolder open_root(unsigned int flags = 0);
};

class AutoMAPI_raii {
	public:
	AutoMAPI_raii() {
		auto ret = am.Initialize();
		if (ret != hrSuccess)
			throw KMAPIError(ret);
	}
	private:
	AutoMAPI am;
};

class KSession : private AutoMAPI_raii, public object_ptr<IMAPISession> {
	public:
	using object_ptr::object_ptr;
	KSession(const wchar_t *user = L"SYSTEM", const wchar_t *pass = L"")
	{
		const char *sock = getenv("KOPANO_SOCKET");
		if (sock == NULL)
			sock = "default:";
		auto ret = HrOpenECSession(&~*this, "", "", user, pass, sock, 0, 0, 0);
		if (ret != hrSuccess)
			throw KMAPIError(ret);
	}
	inline KStore open_default_store();
};

KStream KAttach::open_property_stream(unsigned int tag, unsigned int opts, unsigned int flags)
{
	object_ptr<IStream> stream;
	auto ret = (*this)->OpenProperty(tag, &IID_IStream, opts, flags, &~stream);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KStream(std::move(stream));
}

KMessage KFolder::create_message(unsigned int flags)
{
	IMessage *msg;
	auto ret = (*this)->CreateMessage(&IID_IMessage, flags, &msg);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KMessage(msg);
}

KTable KFolder::get_contents_table(unsigned int flags)
{
	IMAPITable *tbl;
	auto ret = (*this)->GetContentsTable(flags, &tbl);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KTable(tbl);
}

KAttach KMessage::create_attach(unsigned int flags)
{
	IAttach *atx;
	ULONG num;
	auto ret = (*this)->CreateAttach(&IID_IAttachment, flags, &num, &atx);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KAttach(atx);
}

KProp KMessage::get_prop(unsigned int tag)
{
	memory_ptr<SPropValue> prop;
	auto ret = HrGetOneProp(*this, tag, &~prop);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KProp(prop.release());
}

const SBinary &KProp::entry_id() const
{
	if (PROP_TYPE((*this)->ulPropTag) != PT_BINARY)
		throw KMAPIError(MAPI_E_INVALID_TYPE);
	return (*this)->Value.bin;
}

ULONG KProp::ul() const
{
	if (PROP_TYPE((*this)->ulPropTag) != PT_LONG)
		throw KMAPIError(MAPI_E_INVALID_TYPE);
	return (*this)->Value.ul;
}

KStore KSession::open_default_store()
{
	IMsgStore *store = nullptr;
	auto ret = HrOpenDefaultStore(*this, &store);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KStore(store);
}

KMessage KStore::open_message(const KProp &eidprop, unsigned int flags)
{
	object_ptr<IMessage> msg;
	ULONG type;
	auto eid = eidprop.entry_id();
	auto ret = (*this)->OpenEntry(eid.cb, reinterpret_cast<const ENTRYID *>(eid.lpb),
	           &IID_IMessage, flags, &type, &~msg);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KMessage(std::move(msg));
}

KFolder KStore::open_root(unsigned int flags)
{
	object_ptr<IMAPIFolder> folder;
	ULONG type;
	auto ret = (*this)->OpenEntry(0, nullptr, &IID_IMAPIFolder, flags, &type, &~folder);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return KFolder(std::move(folder));
}

KTable &KTable::columns(const std::initializer_list<unsigned int> &props, unsigned int flags)
{
	memory_ptr<SPropTagArray> array;
	auto ret = MAPIAllocateBuffer(CbNewSPropTagArray(props.size()), &~array);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	array->cValues = props.size();
	int i = 0;
	for (const auto &prop : props)
		array->aulPropTag[i++] = prop;
	ret = (*this)->SetColumns(array, flags);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return *this;
}

rowset_ptr KTable::rows(unsigned int size, unsigned int offset)
{
	rowset_ptr r;
	auto ret = (*this)->QueryRows(size, offset, &~r);
	if (ret != hrSuccess)
		throw KMAPIError(ret);
	return r;
}

} /* namespace */

#endif /* TBI_HPP */
