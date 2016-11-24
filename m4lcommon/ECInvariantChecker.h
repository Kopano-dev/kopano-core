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

#ifndef ECInvariantChecker_INCLUDED
#define ECInvariantChecker_INCLUDED

#include <kopano/zcdefs.h>

namespace KC {

template<typename Type> class ECInvariantChecker _kc_final {
public:
	ECInvariantChecker(const Type *p): m_p(p) { m_p->CheckInvariant(); }
	~ECInvariantChecker() { m_p->CheckInvariant(); }
private:
	const Type *m_p;
};

#ifdef DEBUG
#define DEBUG_CHECK_INVARIANT	do { this->CheckInvariant(); } while (false)
#define DEBUG_GUARD				guard __g(this);
#else
#define DEBUG_CHECK_INVARIANT	do { } while (false)
#define DEBUG_GUARD
#endif

#define DECL_INVARIANT_GUARD(__class)	typedef ECInvariantChecker<__class> guard;
#define DECL_INVARIANT_CHECK			void CheckInvariant() const;
#define DEF_INVARIANT_CHECK(__class)	void __class::CheckInvariant() const

} /* namespace */

#endif // ndef ECInvariantChecker_INCLUDED
