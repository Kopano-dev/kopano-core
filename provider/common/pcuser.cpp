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

#include <kopano/platform.h>

#include <kopano/stringutil.h>
#include <kopano/pcuser.hpp>

#include <sstream>

template<int(*fnCmp)(const char*, const char*)>
class StringComparer {
public:
	StringComparer(const std::string &str): m_str(str) {}
	bool operator()(const std::string &other) const
	{
		return m_str.size() == other.size() && fnCmp(m_str.c_str(), other.c_str()) == 0;
	}

private:
	const std::string &m_str;
};

objectid_t::objectid_t(const std::string &id, objectclass_t objclass)
{
	this->id = id;
	this->objclass = objclass;
}

objectid_t::objectid_t(const std::string &str)
{
	std::string objclass;
	std::string objid;
	size_t pos;

	// sendas users are "encoded" like this in a string
	pos = str.find_first_of(';');
	if (pos == std::string::npos) {
		this->id = hex2bin(str);
		this->objclass = ACTIVE_USER;
	} else {
		objid.assign(str, pos + 1, str.size() - pos);
		objclass.assign(str, 0, pos);
		this->id = hex2bin(objid);
		this->objclass = (objectclass_t)atoi(objclass.c_str());
	}
}

objectid_t::objectid_t(objectclass_t objclass)
{
	this->objclass = objclass;
}

objectid_t::objectid_t()
{
	objclass = OBJECTCLASS_UNKNOWN;
}

bool objectid_t::operator==(const objectid_t &x) const
{
	return this->objclass == x.objclass && this->id == x.id;
}

bool objectid_t::operator!=(const objectid_t &x) const
{
	return this->objclass != x.objclass || this->id != x.id;
}

std::string objectid_t::tostring() const
{
	return stringify(this->objclass) + ";" + bin2hex(this->id);
}

objectdetails_t::objectdetails_t(objectclass_t objclass) : m_objclass(objclass) {}
objectdetails_t::objectdetails_t() : m_objclass(OBJECTCLASS_UNKNOWN) {}

objectdetails_t::objectdetails_t(const objectdetails_t &objdetails) {
	m_objclass = objdetails.m_objclass;
	m_mapProps = objdetails.m_mapProps;
	m_mapMVProps = objdetails.m_mapMVProps;
}

unsigned int objectdetails_t::GetPropInt(property_key_t propname) const
{
	property_map::const_iterator item = m_mapProps.find(propname);
	return item == m_mapProps.end() ? 0 : atoi(item->second.c_str());
}

bool objectdetails_t::GetPropBool(property_key_t propname) const
{
	property_map::const_iterator item = m_mapProps.find(propname);
	return item == m_mapProps.end() ? false : atoi(item->second.c_str());
}

std::string objectdetails_t::GetPropString(property_key_t propname) const
{
	property_map::const_iterator item = m_mapProps.find(propname);
	return item == m_mapProps.end() ? std::string() : item->second;
}

objectid_t objectdetails_t::GetPropObject(property_key_t propname) const
{
	property_map::const_iterator item = m_mapProps.find(propname);
	return item == m_mapProps.end() ? objectid_t() : objectid_t(item->second);
}

void objectdetails_t::SetPropInt(property_key_t propname, unsigned int value)
{
    m_mapProps[propname].assign(stringify(value));
}

void objectdetails_t::SetPropBool(property_key_t propname, bool value)
{
    m_mapProps[propname].assign(value ? "1" : "0");
}

void objectdetails_t::SetPropString(property_key_t propname,
    const std::string &value)
{
    m_mapProps[propname].assign(value);
}

void objectdetails_t::SetPropListString(property_key_t propname,
    const std::list<std::string> &value)
{
	m_mapMVProps[propname].assign(value.begin(), value.end());
}

void objectdetails_t::SetPropObject(property_key_t propname,
    const objectid_t &value)
{
	m_mapProps[propname].assign(((objectid_t)value).tostring());
}

void objectdetails_t::AddPropInt(property_key_t propname, unsigned int value)
{
	m_mapMVProps[propname].push_back(stringify(value));
}

void objectdetails_t::AddPropString(property_key_t propname,
    const std::string &value)
{
	m_mapMVProps[propname].push_back(value);
}

void objectdetails_t::AddPropObject(property_key_t propname,
    const objectid_t &value)
{
	m_mapMVProps[propname].push_back(((objectid_t)value).tostring());
}

std::list<unsigned int>
objectdetails_t::GetPropListInt(property_key_t propname) const
{
	property_mv_map::const_iterator mvitem = m_mapMVProps.find(propname);
	if (mvitem == m_mapMVProps.end())
		return std::list<unsigned int>();
	std::list<unsigned int> l;
	for (std::list<std::string>::const_iterator i = mvitem->second.begin(); i != mvitem->second.end(); ++i)
		l.push_back(atoui(i->c_str()));
	return l;
}

std::list<std::string>
objectdetails_t::GetPropListString(property_key_t propname) const
{
	property_mv_map::const_iterator mvitem = m_mapMVProps.find(propname);
	if (mvitem != m_mapMVProps.end()) return mvitem->second;
	else return std::list<std::string>();
}

std::list<objectid_t>
objectdetails_t::GetPropListObject(property_key_t propname) const
{
	property_mv_map::const_iterator mvitem = m_mapMVProps.find(propname);
	if (mvitem == m_mapMVProps.end())
		return std::list<objectid_t>();
	std::list<objectid_t> l;
	for (std::list<std::string>::const_iterator i = mvitem->second.begin(); i != mvitem->second.end(); ++i)
		l.push_back(objectid_t(*i));
	return l;
}

property_map objectdetails_t::GetPropMapAnonymous() const {
	property_map anonymous;
	property_map::const_iterator iter;

	for (iter = m_mapProps.begin(); iter != m_mapProps.end(); ++iter)
		if (((unsigned int)iter->first) & 0xffff0000)
			anonymous.insert(*iter);
	return anonymous;
}

property_mv_map objectdetails_t::GetPropMapListAnonymous() const {
	property_mv_map anonymous;
	property_mv_map::const_iterator iter;

	for (iter = m_mapMVProps.begin(); iter != m_mapMVProps.end(); ++iter)
		if (((unsigned int)iter->first) & 0xffff0000)
			anonymous.insert(*iter);
	return anonymous;
}

bool objectdetails_t::HasProp(property_key_t propname) const
{
	return m_mapProps.find(propname) != m_mapProps.end() || m_mapMVProps.find(propname) != m_mapMVProps.end();
}

bool objectdetails_t::PropListStringContains(property_key_t propname,
    const std::string &value, bool ignoreCase) const
{
	const std::list<std::string> list = GetPropListString(propname);
	if (ignoreCase)
		return std::find_if(list.begin(), list.end(), StringComparer<strcasecmp>(value)) != list.end();
	return std::find_if(list.begin(), list.end(), StringComparer<strcmp>(value)) != list.end();
}

void objectdetails_t::ClearPropList(property_key_t propname)
{
	m_mapMVProps[propname].clear();
}

void objectdetails_t::SetClass(objectclass_t objclass)
{
	m_objclass = objclass;
}

objectclass_t objectdetails_t::GetClass() const {
    return m_objclass;
}

void objectdetails_t::MergeFrom(const objectdetails_t &from) {
	property_map::const_iterator i, fi;
	property_mv_map::const_iterator mvi, fmvi;

	ASSERT(this->m_objclass == from.m_objclass);

	for (fi = from.m_mapProps.begin(); fi != from.m_mapProps.end(); ++fi)
		this->m_mapProps[fi->first].assign(fi->second);
	for (fmvi = from.m_mapMVProps.begin(); fmvi != from.m_mapMVProps.end(); ++fmvi)
		this->m_mapMVProps[fmvi->first].assign(fmvi->second.begin(), fmvi->second.end());
}

/**
 * Get the size of this object
 *
 * @return Memory usage of this object in bytes
 */
size_t objectdetails_t::GetObjectSize(void)
{
	size_t ulSize = sizeof(*this);
	property_map::const_iterator i;
	property_mv_map::const_iterator mvi;
	std::list<std::string>::const_iterator istr;

	ulSize += MEMORY_USAGE_MAP(m_mapProps.size(), property_map);
	for (i = m_mapProps.begin(); i != m_mapProps.end(); ++i)
		ulSize += MEMORY_USAGE_STRING(i->second);

	ulSize += MEMORY_USAGE_MAP(m_mapMVProps.size(), property_mv_map);
	for (mvi = m_mapMVProps.begin(); mvi != m_mapMVProps.end(); ++mvi)
		for (istr = mvi->second.begin(); istr != mvi->second.end(); ++istr)
			ulSize += MEMORY_USAGE_STRING((*istr));
	return ulSize;
}

std::string objectdetails_t::ToStr(void) const
{
	std::string str;
	property_map::const_iterator i;
	property_mv_map::const_iterator mvi;
	std::list<std::string>::const_iterator istr;

	str = "propmap: ";
	for (i = m_mapProps.begin(); i != m_mapProps.end(); ++i) {
		if(i != m_mapProps.begin())  str+= ", ";
		str+= stringify(i->first) + "='";
		str+= i->second + "'";
	}

	str += " mvpropmap: ";
	for (mvi = m_mapMVProps.begin(); mvi != m_mapMVProps.end(); ++mvi) {
		if(mvi != m_mapMVProps.begin()) str += ", ";
		str += stringify(mvi->first) + "=(";
		for (istr = mvi->second.begin(); istr != mvi->second.end(); ++istr) {
			if(istr != mvi->second.begin()) str +=", ";
			str += *istr;
		}
		str +=")";
	}

	return str;
}

serverdetails_t::serverdetails_t(const std::string &servername)
: m_strServerName(servername)
, m_ulHttpPort(0)
, m_ulSslPort(0)
{ }

void serverdetails_t::SetHostAddress(const std::string &hostaddress) {
	m_strHostAddress = hostaddress;
}

void serverdetails_t::SetFilePath(const std::string &filepath) {
	m_strFilePath = filepath;
}

void serverdetails_t::SetHttpPort(unsigned port) {
	m_ulHttpPort = port;
}

void serverdetails_t::SetSslPort(unsigned port) {
	m_ulSslPort = port;
}

void serverdetails_t::SetProxyPath(const std::string &proxy) {
	m_strProxyPath = proxy;
}

const std::string& serverdetails_t::GetServerName() const {
	return m_strServerName;
}

const std::string& serverdetails_t::GetHostAddress() const {
	return m_strHostAddress;
}

unsigned serverdetails_t::GetHttpPort() const {
	return m_ulHttpPort;
}

unsigned serverdetails_t::GetSslPort() const {
	return m_ulSslPort;
}

std::string serverdetails_t::GetFilePath() const {
	if (!m_strFilePath.empty())
		return "file://"+m_strFilePath;
	return std::string();
}

std::string serverdetails_t::GetHttpPath() const {
	if (!m_strHostAddress.empty() && m_ulHttpPort > 0) {
		std::ostringstream oss;
		oss << "http://" << m_strHostAddress << ":" << m_ulHttpPort << "/";
		return oss.str();
	}
	return std::string();	
}

std::string serverdetails_t::GetSslPath() const {
	if (!m_strHostAddress.empty() && m_ulSslPort > 0) {
		std::ostringstream oss;
		oss << "https://" << m_strHostAddress << ":" << m_ulSslPort << "/";
		return oss.str();
	}
	return std::string();	
}

const std::string &serverdetails_t::GetProxyPath() const {
	return m_strProxyPath;
}
