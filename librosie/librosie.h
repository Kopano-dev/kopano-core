#ifndef _LIBROSIE_H
#define _LIBROSIE_H 1

#include <kopano/zcdefs.h>
#include <string>
#include <vector>

extern _kc_export bool CleanHtml(const std::string & in, std::string *const out, std::vector<std::string> *const errors);

#endif /* LIBROSIE_H */
