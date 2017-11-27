#ifndef _KCHL_HPP
#define _KCHL_HPP 1

#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <mapidefs.h>

class IAttach;
class IMAPIFolder;
class IMAPISession;
class IMessage;
class IMsgFolder;
class IMsgStore;
class IStream;
class IUnknown;

namespace KCHL {
using namespace KC;

class KAttach;
class KFolder;
class KMessage;
class KStore;
class KStream;
class KTable;
class KUnknown;
class KEntryId;

class _kc_export KProp _kc_final {
	public:
	KProp(SPropValue *);
	KProp(KProp &&);
	~KProp(void);
	KProp &operator=(KProp &&);
	SPropValue *operator->(void) { return m_s; }
	const SPropValue *operator->(void) const { return m_s; }
	operator SPropValue *(void) { return m_s; }
	operator const SPropValue *(void) const { return m_s; }

	unsigned int prop_type() const;
	const unsigned int &prop_tag() const;
	const bool b() const;
	const unsigned int &ul() const;
	const int &l() const;
	std::string str();
	std::wstring wstr();
	KEntryId entry_id();

	private:
	SPropValue *m_s;
};

class _kc_export KAttach _kc_final {
	public:
	KAttach(IAttach *, unsigned int);
	KAttach(KAttach &&);
	~KAttach(void);
	KAttach &operator=(KAttach &&);
	IAttach *operator->(void) { return m_attach; }
	operator IAttach *(void) { return m_attach; }

	KStream open_property_stream(unsigned int = 0, unsigned int = 0, unsigned int = 0);
	HRESULT save_changes(unsigned int = 0);

	protected:
	IAttach *m_attach;
	unsigned int m_num;
};

class _kc_export KEntryId _kc_final {
	public:
	KEntryId(void) = default;
	KEntryId(KEntryId &&);
	KEntryId(ENTRYID *, size_t);
	~KEntryId(void);
	KEntryId &operator=(KEntryId &&);

	ENTRYID *lpb() { return m_eid; }
	const ENTRYID *lpb() const { return m_eid; }
	size_t cb() const { return m_size; }

	private:
	friend class KStore;
	ENTRYID *m_eid = nullptr;
	size_t m_size = 0;
};

class _kc_export_throw KMAPIError _kc_final : public std::exception {
	public:
	KMAPIError(HRESULT = hrSuccess);
	virtual ~KMAPIError(void) noexcept = default;
	HRESULT code(void) const noexcept { return m_code; }
	virtual const char *what(void) const noexcept;

	private:
	HRESULT m_code;
	std::string m_message;
};

class _kc_export KFolder _kc_final {
	public:
	KFolder(void) = default;
	KFolder(IMAPIFolder *);
	KFolder(KFolder &&);
	~KFolder(void);
	KFolder &operator=(KFolder &&);
	IMAPIFolder *operator->(void) { return m_folder; }
	operator IMAPIFolder *(void) { return m_folder; }
	KTable get_contents_table(unsigned int = 0);
	KTable get_hierarchy_table(unsigned int = 0);
	KProp get_prop(unsigned int);

	protected:
	IMAPIFolder *m_folder = nullptr;
};

class _kc_export KMessage _kc_final {
	public:
	KMessage(void) = default;
	KMessage(IMessage *);
	KMessage(KMessage &&);
	~KMessage(void);
	KMessage &operator=(KMessage &&);
	IMessage *operator->(void) { return m_message; }
	operator IMessage *(void) { return m_message; }

	KAttach create_attach(LPCIID = NULL, unsigned int = 0);
	KProp get_prop(unsigned int);
	HRESULT save_changes(unsigned int = 0);
	HRESULT set_read_flag(unsigned int = 0);

	protected:
	IMessage *m_message = nullptr;
};

class _kc_export KSession _kc_final {
	public:
	KSession(void);
	KSession(const wchar_t *, const wchar_t *);
	KSession(IMAPISession *);
	KSession(KSession &&) = delete;
	~KSession(void);
	IMAPISession *operator->(void) { return m_session; }
	operator IMAPISession *(void) { return m_session; }

	KStore open_default_store(void);

	protected:
	IMAPISession *m_session = nullptr;
};

class _kc_export KStore _kc_final {
	public:
	KStore(IMsgStore *);
	KStore(KStore &&);
	~KStore(void);
	KStore &operator=(KStore &&);
	IMsgStore *operator->(void) { return m_store; }
	operator IMsgStore *(void) { return m_store; }
	KProp get_prop(unsigned int);

	protected:
	IMsgStore *m_store;
	ULONG m_type;
};

class _kc_export KStream _kc_final {
	public:
	KStream(IStream *);
	KStream(KStream &&);
	~KStream(void);
	KStream &operator=(KStream &&);
	IStream *operator->(void) { return m_stream; }
	operator IStream *(void) { return m_stream; }
	HRESULT write(const std::string &);
	HRESULT commit(unsigned int = 0);

	protected:
	IStream *m_stream;
};

class _kc_export KRow _kc_final {
	public:
	KRow(SRow);
	KProp operator[](size_t index) const;
	unsigned int count() const;
	private:
	SRow m_row;
};

class _kc_export KRowSet _kc_final {
	public:
	KRowSet(SRowSet*);
	KRow operator[](size_t) const;
	unsigned int count() const;
	SRowSet* operator->(void) { return m_rowset.get(); }
	operator SRowSet *(void) { return m_rowset.get(); }

	protected:
	rowset_ptr m_rowset;
};

class _kc_export KTable _kc_final {
	public:
	enum SortOrder {
		ASCEND = TABLE_SORT_ASCEND,
		DESCEND = TABLE_SORT_DESCEND
	};

	KTable(IMAPITable *);
	KTable(KTable &&);
	~KTable(void);
	KTable &operator=(KTable &&);
	IMAPITable *operator->(void) { return m_table; }
	operator IMAPITable *(void) { return m_table; }

	HRESULT restrict(const SRestriction &, unsigned int = 0);
	void columns(std::initializer_list<unsigned int>, unsigned int = 0);
	void sort(std::initializer_list<std::pair<unsigned int, SortOrder> >, unsigned int = 0);
	KRowSet rows(unsigned int, unsigned int);
	unsigned int count(unsigned int = 0);

	protected:
	IMAPITable *m_table;
};

class _kc_export KUnknown _kc_final {
	public:
	KUnknown(IUnknown * = NULL);
	KUnknown(KUnknown &&);
	~KUnknown(void);
	KUnknown &operator=(KUnknown &&);
	operator KFolder(void) const;
	operator KMessage(void) const;

	protected:
	IUnknown *m_ptr;
};

} /* namespace KCHL */

#endif /* _KCHL_HPP */
