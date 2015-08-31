#ifndef _LIBROSIE_H
#define _LIBROSIE_H 1

#include <string>
#include <vector>
#include <kopano/zcdefs.h>

extern "C" {

extern _kc_export bool rosie_clean_html(const std::string &in, std::string *out, std::vector<std::string> *errors);

} /* extern "C" */

#endif /* LIBROSIE_H */
