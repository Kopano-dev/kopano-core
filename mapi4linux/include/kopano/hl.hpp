#ifndef _KCHL_HPP
#define _KCHL_HPP 1

#include <kopano/zcdefs.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <mapidefs.h>

struct IAttach;
struct IMAPIFolder;
struct IMAPISession;
struct IMessage;
struct IMsgFolder;
struct IMsgStore;
struct IStream;
struct IUnknown;

namespace KCHL {

class KAttach;
class KDeleter;
class KFolder;
class KMessage;
class KStore;
class KStream;
class KTable;
class KUnknown;

typedef std::unique_ptr<SPropValue, KDeleter> KProp;
typedef std::unique_ptr<SRowSet, KDeleter> KRowSet;

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
	KEntryId(void);
	KEntryId(KEntryId &&);
	KEntryId(ENTRYID *, size_t);
	~KEntryId(void);
	KEntryId &operator=(KEntryId &&);

	private:
	friend class KStore;
	ENTRYID *m_eid = nullptr;
	size_t m_size = 0;
};

class _kc_export_throw KMAPIError _kc_final : public std::exception {
	public:
	KMAPIError(HRESULT = hrSuccess);
	virtual ~KMAPIError(void) noexcept {}
	HRESULT code(void) const noexcept { return m_code; }
	virtual const char *what(void) const noexcept;

	private:
	HRESULT m_code;
	std::string m_message;
};

class _kc_export KDeleter _kc_final {
	public:
	void operator()(SPropValue *);
	void operator()(SRowSet *);
};

class _kc_export KFolder _kc_final {
	public:
	KFolder(void) {}
	KFolder(IMAPIFolder *);
	KFolder(KFolder &&);
	~KFolder(void);
	KFolder &operator=(KFolder &&);
	IMAPIFolder *operator->(void) { return m_folder; }
	operator IMAPIFolder *(void) { return m_folder; }

	KMessage create_message(LPCIID = NULL, unsigned int = 0);
	KTable get_contents_table(unsigned int = 0);

	protected:
	IMAPIFolder *m_folder = nullptr;
};

class _kc_export KMessage _kc_final {
	public:
	KMessage(void) {}
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

class _kc_export KPropertyRestriction _kc_final : public SPropertyRestriction {
	public:
	KPropertyRestriction(ULONG, SPropValue *);
	operator SRestriction(void) const;
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
	IMAPISession *m_session;
};

class _kc_export KStore _kc_final {
	public:
	KStore(IMsgStore *);
	KStore(KStore &&);
	~KStore(void);
	KStore &operator=(KStore &&);
	IMsgStore *operator->(void) { return m_store; }
	operator IMsgStore *(void) { return m_store; }

	KEntryId get_receive_folder(const char *cls = nullptr, char **xcls = nullptr);
	KUnknown open_entry(const KEntryId &, LPCIID = nullptr, unsigned int = 0);
	KUnknown open_entry(const SPropValue * = NULL, LPCIID = NULL, unsigned int = 0);

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

class _kc_export KTable _kc_final {
	public:
	KTable(IMAPITable *);
	KTable(KTable &&);
	~KTable(void);
	KTable &operator=(KTable &&);
	IMAPITable *operator->(void) { return m_table; }
	operator IMAPITable *(void) { return m_table; }

	HRESULT restrict(const SRestriction &, unsigned int = 0);

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
