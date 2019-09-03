// SPDX-License-Identifier: GPL-2.0-or-later
//
// VMime library (http://www.vmime.org)
// Copyright (C) 2002-2008 Vincent Richard <vincent@vincent-richard.net>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of
// the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Linking this library statically or dynamically with other modules is making
// a combined work based on this library.  Thus, the terms and conditions of
// the GNU General Public License cover the whole combination.
//

#include <memory>
#include <vmime/net/serviceFactory.hpp>

namespace KC {

using namespace vmime;
using namespace vmime::net;

template <class S>
class registeredServiceImpl : public vmime::net::serviceFactory::registeredService {
public:

	registeredServiceImpl(const string& name, const int type)
		: m_type(type), m_name(name), m_servInfos(S::getInfosInstance())
	{
	}

	vmime::shared_ptr<service> create(const vmime::shared_ptr<session> &sess,
	    const vmime::shared_ptr<security::authenticator> &auth) const
	{
		return vmime::make_shared<S>(sess, auth);
	}

	const serviceInfos& getInfos() const
	{
		return (m_servInfos);
	}

	const string& getName() const
	{
		return (m_name);
	}

	int getType() const
	{
		return (m_type);
	}

private:

	const int m_type;
	const string m_name;
	const serviceInfos& m_servInfos;
};


// Basic service registerer
template<class S> class serviceRegisterer {
public:

	serviceRegisterer(const string& protocol, const service::Type type)
	{
		serviceFactory::getInstance()->registerService
			(vmime::make_shared<registeredServiceImpl<S>>(protocol, type));
	}
};

} /* namespace */

#define REGISTER_SERVICE(p_class, p_name, p_type) \
	KC::serviceRegisterer<p_class> \
		p_name(#p_name, vmime::net::service::p_type)
