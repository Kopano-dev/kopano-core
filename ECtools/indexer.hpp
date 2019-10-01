#ifndef KCIDX_MAIN_HPP
#define KCIDX_MAIN_HPP 1

#include <memory>
#include <string>
#include <vector>
#include <kopano/platform.h>

namespace KC {

class ECConfig;
class ECIndexService;

using FIELDTERM = std::pair<std::vector<unsigned int>, std::vector<std::string>>;
using FIELDTERMS = std::vector<FIELDTERM>;

class KC_EXPORT IIndexer {
	public:
	struct client_state {
		private:
		GUID m_server_guid, m_store_guid;
		std::string m_orig;
		std::vector<unsigned int> m_folder_ids;
		FIELDTERMS m_fields_terms;
		friend class ECIndexService;
	};

	static HRESULT create(const char *file, IIndexer **);
	virtual ~IIndexer() = default;
	virtual HRESULT service_start() = 0;
	virtual void service_stop() {}
	virtual std::shared_ptr<ECConfig> get_config() const = 0;
	virtual std::string exec1(client_state &, const char *cmd) = 0;
	virtual std::string exec(client_state &, const std::vector<std::string> &cmd) = 0;
};

} /* namespace */

#endif /* KCIDX_MAIN_HPP */
