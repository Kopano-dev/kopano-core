#include <map>
#include <string>
#include <utility>
#include <vector>
#include <mapidefs.h>

namespace KC {

struct index_doc {
	const GUID serverid, storeid, sourcekey;
	unsigned int folderid, docid;
	std::map<std::string, std::string> items;
	std::string data;
};

class IIndexerPlugin {
	public:
	virtual ~IIndexerPlugin() = default;
	virtual std::vector<std::string> extract_terms(const char *) = 0;
	virtual std::vector<std::string> search(const GUID &server, const GUID &store, const std::vector<unsigned int> &folders, const FIELDTERMS &, const std::string &query, size_t limit = 0) = 0;
	virtual std::string suggest(const GUID &server, const GUID &store, const std::vector<std::string> &terms, const std::string &orig) = 0;
	virtual void update_doc(const index_doc &) = 0;
	virtual void delete_doc(const index_doc &) = 0;
	virtual void commit(const std::string &sugg) = 0;
	virtual void reindex(const GUID &server, const GUID &store) {}
};

extern IIndexerPlugin *make_xapian_plugin(const char *);

} /* namespace */
