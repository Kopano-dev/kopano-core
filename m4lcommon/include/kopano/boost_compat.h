/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef boost_compat_INCLUDED
#define boost_compat_INCLUDED

#include <string>

#include <boost/filesystem.hpp>

namespace KC {

#if !defined(BOOST_FILESYSTEM_VERSION) || BOOST_FILESYSTEM_VERSION == 2

static inline std::string path_to_string(const boost::filesystem::path &p) {
    return p.file_string();
}

static inline boost::filesystem::path& remove_filename_from_path(boost::filesystem::path &p) {
    return p.remove_leaf();
}

static inline std::string filename_from_path(const boost::filesystem::path &p) {
    return p.leaf();
}
    
#else

static inline std::string path_to_string(const boost::filesystem::path &p) {
    return p.string();
}

static inline boost::filesystem::path& remove_filename_from_path(boost::filesystem::path &p) {
    return p.remove_filename();
}

static inline std::string filename_from_path(const boost::filesystem::path &p) {
    return p.filename().string();
}
    
#endif

} /* namespace */

#endif // ndef boost_compat_INCLUDED
