#ifndef _LIBROSIE_H
#define _LIBROSIE_H 1

#include <kopano/zcdefs.h>
#include <string>
#include <vector>

namespace KC {

extern _kc_export bool rosie_clean_html(const std::string &in, std::string *out, std::vector<std::string> *err);

}

#endif /* LIBROSIE_H */
