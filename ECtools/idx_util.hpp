#ifndef IDX_UTIL_HPP
#define IDX_UTIL_HPP 1

#include <string>
#include <mapidefs.h>

class IMAPIProp;

namespace KC {

class mapitable_iterator {
	public:
	mapitable_iterator(IMAPITable *, size_t = 0);
	mapitable_iterator(mapitable_iterator &&) = default;
	void load();
	const SRow &operator*();
	bool operator==(const mapitable_iterator &) const;
	bool operator!=(const mapitable_iterator &o) const { return !(*this == o); }
	mapitable_iterator &operator++();

	private:
	IMAPITable *m_table = nullptr;
	rowset_ptr m_rows;
	size_t m_pos = 0;
};

class mapitable_range {
	public:
	mapitable_range(IMAPITable *t) : m_table(t) {}
	mapitable_iterator begin() { return mapitable_iterator(m_table, 0); }
	mapitable_iterator end();
	private:
	IMAPITable *m_table;
};

extern HRESULT server_guid(IMsgStore *, GUID &);
extern HRESULT ics_state(IMAPIProp *, bool assoc, std::string &);

}

#endif /* IDX_UTIL_HPP */
