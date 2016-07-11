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

#ifndef EC_FEATURELIST_H
#define EC_FEATURELIST_H

#include <kopano/platform.h>
#include <string>
#include <set>

///< all kopano features that are checked for access before allowing it
static const char *const kopano_features[] = {
	"imap", "pop3", "mobile", "outlook"
};

/** 
 * Return a set of all available kopano features.
 * 
 * @return unique set of feature names
 */
inline std::set<std::string> getFeatures() {
	return std::set<std::string>(kopano_features,
	       kopano_features + ARRAY_SIZE(kopano_features));
}

#endif
