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

#ifndef KC_PCUSER_H
#define KC_PCUSER_H 1

#include <map>
#include <list>
#include <string>
#include <kopano/zcdefs.h>
#include <kopano/stringutil.h>
#include <kopano/ECDefs.h>

/* Extern object */
struct objectid_t {
	objectid_t(const std::string &id, objectclass_t objclass);
	explicit objectid_t(const std::string &str);
	objectid_t(objectclass_t objclass);
	objectid_t();

	bool operator==(const objectid_t &x) const;
	bool operator!=(const objectid_t &x) const;
	std::string tostring() const;

	std::string id;
	objectclass_t objclass;
};

inline bool operator < (const objectid_t &a, const objectid_t &b) {
	if (a.objclass < b.objclass)
		return true;
	if ((a.objclass == b.objclass) && a.id < b.id)
		return true;
	return false;
}

/* Object properties
 * Prefix: OB_PROP
 * Type: S - String
 *       B - Boolean
 *       I - Integer
 *		 O - objectid_t
 * Name: Property name
 */
enum property_key_t {
	OB_PROP_B_AB_HIDDEN,
	OB_PROP_S_FULLNAME,
	OB_PROP_S_LOGIN,
	OB_PROP_S_PASSWORD,
	OB_PROP_I_COMPANYID,		/* COMPANY localid */
	OB_PROP_O_COMPANYID,		/* COMPANY externid */
	OB_PROP_I_ADMINLEVEL,
	OB_PROP_S_RESOURCE_DESCRIPTION,
	OB_PROP_I_RESOURCE_CAPACITY,
	OB_PROP_S_EMAIL,
	OB_PROP_LS_ALIASES,
	OB_PROP_I_SYSADMIN,		/* SYSADMIN localid */
	OB_PROP_O_SYSADMIN,		/* SYSADMIN externid */
	OB_PROP_LS_CERTIFICATE,
	OB_PROP_LI_SENDAS,		/* SENDAS localid .. */
	OB_PROP_LO_SENDAS,		/* SENDAS externid */
	OB_PROP_S_EXCH_DN,
	OB_PROP_O_EXTERNID,
	OB_PROP_S_SERVERNAME,
	OB_PROP_S_HTTPPATH,
	OB_PROP_S_SSLPATH,
	OB_PROP_S_FILEPATH,
	OB_PROP_MAX				/* Not a propname, keep as last entry */
};

typedef std::map<property_key_t, std::string> property_map;
typedef std::map<property_key_t, std::list<std::string> > property_mv_map;

/** One user's / group's details
 */
class objectdetails_t {
public:
	objectdetails_t(const objectdetails_t &objdetails);
	objectdetails_t(objectclass_t objclass);
	objectdetails_t();

	unsigned int 			GetPropInt(property_key_t propname) const;
	bool					GetPropBool(property_key_t propname) const;
	std::string				GetPropString(property_key_t propname) const;
	std::list<unsigned int>	GetPropListInt(property_key_t propname) const;
	std::list<std::string>	GetPropListString(property_key_t propname) const;
	objectid_t				GetPropObject(property_key_t propname) const;
	std::list<objectid_t>	GetPropListObject(property_key_t propname) const;
	property_map			GetPropMapAnonymous() const;
	property_mv_map			GetPropMapListAnonymous() const;
	bool					HasProp(property_key_t propname) const;

	bool			PropListStringContains(property_key_t propname, const std::string &value, bool ignoreCase = false) const;

	void			SetPropInt(property_key_t propname, unsigned int value);
	void			SetPropBool(property_key_t propname, bool value);
	void			SetPropString(property_key_t propname, const std::string &value);
	void			SetPropListString(property_key_t propname, const std::list<std::string> &value);
	void			SetPropObject(property_key_t propname, const objectid_t &value);
	void			SetPropListObject(property_key_t propname, const std::list<objectid_t> &value);

	/* "mv" props */
	void			AddPropInt(property_key_t propname, unsigned int value);
	void			AddPropString(property_key_t propname, const std::string &value);
	void			AddPropObject(property_key_t propname, const objectid_t &value);
	void			ClearPropList(property_key_t propname);

	void			MergeFrom(const objectdetails_t &from);

	void			SetClass(objectclass_t objclass);
	objectclass_t	GetClass() const;

	virtual size_t GetObjectSize(void);

	std::string ToStr(void) const;

private:
	objectclass_t m_objclass;
	property_map m_mapProps;
	property_mv_map m_mapMVProps;
};

/** Quota Details
 */
class quotadetails_t {
public:
	quotadetails_t() : 
		bUseDefaultQuota(true), bIsUserDefaultQuota(false),
		llWarnSize(0), llSoftSize(0), llHardSize(0) {}

	bool bUseDefaultQuota;
	bool bIsUserDefaultQuota; /* Default quota for users within company */
	long long llWarnSize;
	long long llSoftSize;
	long long llHardSize;
};

/** Server Details
*/
class serverdetails_t {
public:
	serverdetails_t(const std::string &servername = std::string());

	void SetHostAddress(const std::string &hostaddress);
	void SetFilePath(const std::string &filepath);
	void SetHttpPort(unsigned port);
	void SetSslPort(unsigned port);
	void SetProxyPath(const std::string &proxy);

	const std::string&	GetServerName() const;
	const std::string&	GetHostAddress() const;
	unsigned			GetHttpPort() const;
	unsigned			GetSslPort() const;

	std::string	GetFilePath() const;
	std::string	GetHttpPath() const;
	std::string	GetSslPath() const;
	const std::string &GetProxyPath(void) const;

private:
	std::string m_strServerName;
	std::string m_strHostAddress;
	std::string m_strFilePath;
	unsigned	m_ulHttpPort;
	unsigned	m_ulSslPort;
	std::string	m_strProxyPath;
};

typedef std::list<std::string> serverlist_t;

#endif /* KC_PCUSER_H */
