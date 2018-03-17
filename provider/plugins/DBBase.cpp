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
#include <algorithm>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include "DBBase.h"
#include <kopano/ECDefs.h>
#include <kopano/EMSAbTag.h>
#include <kopano/stringutil.h>
#include <mapidefs.h>
#include "ECServerEntrypoint.h"

using namespace KC;
using std::runtime_error;
using std::string;

DBPlugin::DBPlugin(std::mutex &pluginlock, ECPluginSharedData *shareddata) :
	UserPlugin(pluginlock, shareddata)
{
}

//DBPlugin::~DBPlugin() {
//    // Do not delete m_lpDatabase as it is freed when the thread exits
//}

void DBPlugin::InitPlugin() {

	if(GetDatabaseObject(&m_lpDatabase) != erSuccess)
	    throw runtime_error(string("db_init: cannot get handle to database"));
}

signatures_t
DBPlugin::getAllObjects(const objectid_t &company, objectclass_t objclass)
{
	string strQuery =
		"SELECT om.externid, om.objectclass, op.value "
		"FROM " + (string)DB_OBJECT_TABLE + " AS om "
		"LEFT JOIN " + (string)DB_OBJECTPROPERTY_TABLE " AS op "
			"ON op.objectid = om.id "
			"AND op.propname = '" + OP_MODTIME + "' ";
	if (m_bHosted && !company.id.empty()) {
		// join company, company itself inclusive
		strQuery +=
			"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS usercompany "
				"ON usercompany.objectid = om.id "
				"AND ((usercompany.propname = '" + OP_COMPANYID + "' AND usercompany.value = hex('" + m_lpDatabase->Escape(company.id) + "')) OR"
					" (usercompany.propname = '" + OP_COMPANYNAME + "' AND om.externid = '" + m_lpDatabase->Escape(company.id) + "'))";
		if (objclass != OBJECTCLASS_UNKNOWN)
			strQuery += " AND " + OBJECTCLASS_COMPARE_SQL("om.objectclass", objclass);
	} else if (objclass != OBJECTCLASS_UNKNOWN)
		strQuery += " WHERE " + OBJECTCLASS_COMPARE_SQL("om.objectclass", objclass);

	return CreateSignatureList(strQuery);
}

objectdetails_t DBPlugin::getObjectDetails(const objectid_t &objectid)
{
	auto objectdetails = DBPlugin::getObjectDetails(std::list<objectid_t>{objectid});
	if (objectdetails.size() != 1)
		throw objectnotfound(objectid.id);
	return objectdetails.begin()->second;
}

std::map<objectid_t, objectdetails_t>
DBPlugin::getObjectDetails(const std::list<objectid_t> &objectids)
{
	std::map<objectid_t, objectdetails_t> mapdetails;
	std::map<objectclass_t, std::string> objectstrings;
	string strSubQuery;
	DB_RESULT lpResult;
	DB_ROW lpDBRow = NULL;
	objectdetails_t details;
	objectid_t lastid;

	if(objectids.empty())
		return mapdetails;

	LOG_PLUGIN_DEBUG("%s N=%d", __FUNCTION__, (int)objectids.size());

	for (const auto &id : objectids) {
		if (!objectstrings[id.objclass].empty())
			objectstrings[id.objclass] += ", ";
		objectstrings[id.objclass] += "'" + m_lpDatabase->Escape(id.id) + "'";
	}

	/* Create subquery which combines all externids with the matching objectclass */
	for (auto iterStrings = objectstrings.cbegin();
	     iterStrings != objectstrings.cend(); ++iterStrings) {
		if (iterStrings != objectstrings.cbegin())
			strSubQuery += " OR ";
		strSubQuery += "(o.externid IN (" + iterStrings->second + ") "
				"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", iterStrings->first) + ")";
	}

	auto strQuery =
		"SELECT o.externid, o.objectclass, op.propname, op.value "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"LEFT JOIN "+(string)DB_OBJECTPROPERTY_TABLE+" AS op "
		"ON op.objectid=o.id "
		"WHERE (" + strSubQuery + ") "
		"ORDER BY o.externid, o.objectclass";
	auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if(er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		// No way to determine externid
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL)
			continue;
		auto lpDBLen = lpResult.fetch_row_lengths();
		if (lpDBLen == NULL || lpDBLen[0] == 0)
			continue;

		auto curid = objectid_t(string(lpDBRow[0], lpDBLen[0]), (objectclass_t)atoi(lpDBRow[1]));
		if (lastid != curid && !lastid.id.empty()) {
			details.SetClass(lastid.objclass);
			addSendAsToDetails(lastid, &details);
			mapdetails[lastid] = details;

			// clear details for new object
			details = objectdetails_t((objectclass_t)atoi(lpDBRow[1]));
			/* By default the sysadmin is SYSTEM, will be overwritten later */
			if (details.GetClass() == CONTAINER_COMPANY)
				details.SetPropObject(OB_PROP_O_SYSADMIN, objectid_t("SYSTEM", ACTIVE_USER));
		}

		lastid = curid;

		// no properties
		if(lpDBRow[2] == NULL || lpDBRow[3] == NULL)
			continue;

		if(strcmp(lpDBRow[2], OP_LOGINNAME) == 0)
			details.SetPropString(OB_PROP_S_LOGIN, lpDBRow[3]);
		else if(strcmp(lpDBRow[2], OP_FULLNAME) == 0)
			details.SetPropString(OB_PROP_S_FULLNAME, lpDBRow[3]);
		else if(strcmp(lpDBRow[2], OP_EMAILADDRESS) == 0)
			details.SetPropString(OB_PROP_S_EMAIL, lpDBRow[3]);
		else if(strcmp(lpDBRow[2], OP_ISADMIN) == 0)
			details.SetPropInt(OB_PROP_I_ADMINLEVEL, std::min(2, atoi(lpDBRow[3])));
		else if(strcmp(lpDBRow[2], OP_GROUPNAME) == 0) {
			details.SetPropString(OB_PROP_S_LOGIN, lpDBRow[3]);
			details.SetPropString(OB_PROP_S_FULLNAME, lpDBRow[3]);
		} else if (strcmp(lpDBRow[2], OP_COMPANYNAME) == 0) {
			details.SetPropString(OB_PROP_S_LOGIN, lpDBRow[3]);
			details.SetPropString(OB_PROP_S_FULLNAME, lpDBRow[3]);
		} else if (strcmp(lpDBRow[2], OP_COMPANYID) == 0) {
			// unhex lpDBRow[3]
			details.SetPropObject(OB_PROP_O_COMPANYID, objectid_t(hex2bin(lpDBRow[3]), CONTAINER_COMPANY));
		} else if (strcmp(lpDBRow[2], OP_COMPANYADMIN) == 0) {
			details.SetPropString(OB_PROP_O_SYSADMIN, lpDBRow[3]);
		} else if (strcmp(lpDBRow[2], OB_AB_HIDDEN) == 0)
			details.SetPropString(OB_PROP_B_AB_HIDDEN, lpDBRow[3]);
		else if (strncasecmp(lpDBRow[2], "0x", strlen("0x")) == 0) {
			unsigned int key = xtoi(lpDBRow[2]);
			if (PROP_TYPE(key) == PT_BINARY)
				details.SetPropString((property_key_t)key, base64_decode(lpDBRow[3]));
			else
				details.SetPropString((property_key_t)xtoi(lpDBRow[2]), lpDBRow[3]);
		}
	}

	if(!lastid.id.empty()) {
		details.SetClass(lastid.objclass);
		addSendAsToDetails(lastid, &details);
		mapdetails[lastid] = details;
	}

	/* Reset lastid */
	lastid = objectid_t();

	/* We not have all regular properties, but we might need some MV properties as well */
	strQuery =
		"SELECT op.propname, op.value, o.externid, o.objectclass "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"JOIN " + (string)DB_OBJECTMVPROPERTY_TABLE + " AS op "
		"ON op.objectid=o.id "
		"WHERE (" + strSubQuery + ") "
		"ORDER BY o.externid, op.orderid";

	er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if(er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	std::map<objectid_t, objectdetails_t>::iterator iterDetails;
	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if(lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL || lpDBRow[3] == NULL)
			continue;
		auto lpDBLen = lpResult.fetch_row_lengths();
		if (lpDBLen == NULL || lpDBLen[2] == 0)
			continue;

		auto curid = objectid_t(string(lpDBRow[2], lpDBLen[2]), (objectclass_t)atoi(lpDBRow[3]));
		if (lastid != curid) {
			iterDetails = mapdetails.find(curid);
			if (iterDetails == mapdetails.cend())
				continue;
		}

		lastid = curid;

		if (strncasecmp(lpDBRow[0], "0x", strlen("0x")) == 0) {
			unsigned int key = xtoi(lpDBRow[0]);
			if (PROP_TYPE(key) == PT_BINARY || PROP_TYPE(key) == PT_MV_BINARY)
				iterDetails->second.AddPropString((property_key_t)key, base64_decode(lpDBRow[1]));
			else
				iterDetails->second.AddPropString((property_key_t)key, lpDBRow[1]);
		}
	}

	return mapdetails;
}

signatures_t
DBPlugin::getSubObjectsForObject(userobject_relation_t relation,
    const objectid_t &parentobject)
{
	string strQuery =
		"SELECT o.externid, o.objectclass, modtime.value "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"JOIN " + (string)DB_OBJECT_RELATION_TABLE + " AS ort "
			"ON o.id = ort.objectid "
		"JOIN " + (string)DB_OBJECT_TABLE + " AS p "
			"ON p.id = ort.parentobjectid "
		"LEFT JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS modtime "
			"ON modtime.objectid=o.id "
			"AND modtime.propname = '" + OP_MODTIME + "' "
		"WHERE p.externid = '" +  m_lpDatabase->Escape(parentobject.id) + "' "
			"AND ort.relationtype = " + stringify(relation) + " ";
			"AND " + OBJECTCLASS_COMPARE_SQL("p.objectclass", parentobject.objclass);

	LOG_PLUGIN_DEBUG("%s Relation %x", __FUNCTION__, relation);
	return CreateSignatureList(strQuery);
}

signatures_t
DBPlugin::getParentObjectsForObject(userobject_relation_t relation,
    const objectid_t &childobject)
{
	string strQuery =
		"SELECT o.externid, o.objectclass, modtime.value "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"JOIN " + (string)DB_OBJECT_RELATION_TABLE + " AS ort "
			"ON o.id = ort.parentobjectid "
		"JOIN " + (string)DB_OBJECT_TABLE + " AS c "
			"ON ort.objectid = c.id "
		"LEFT JOIN " +(string)DB_OBJECTPROPERTY_TABLE + " AS modtime "
			"ON modtime.objectid = o.id "
			"AND modtime.propname = '" + OP_MODTIME + "' "
		"WHERE c.externid = '" +  m_lpDatabase->Escape(childobject.id) + "' "
			"AND ort.relationtype = " + stringify(relation) + " "
			"AND " + OBJECTCLASS_COMPARE_SQL("c.objectclass", childobject.objclass);

	LOG_PLUGIN_DEBUG("%s Relation %x", __FUNCTION__, relation);
	return CreateSignatureList(strQuery);
}

struct props {
	property_key_t id;
	const char *column;
};

void DBPlugin::changeObject(const objectid_t &objectid, const objectdetails_t &details, const std::list<std::string> *lpDeleteProps)
{
	std::string strDeleteQuery, strData;
	bool bFirstOne = true, bFirstDel = true;
	const struct props sUserValidProps[] = {
		{ OB_PROP_S_LOGIN, OP_LOGINNAME, },
		{ OB_PROP_S_PASSWORD, OP_PASSWORD, },
		{ OB_PROP_S_EMAIL, OP_EMAILADDRESS, },
		{ OB_PROP_I_ADMINLEVEL, OP_ISADMIN, },
		{ OB_PROP_S_FULLNAME, OP_FULLNAME, },
		{ OB_PROP_O_COMPANYID, OP_COMPANYID, },
		{ OB_PROP_B_AB_HIDDEN, OB_AB_HIDDEN, },
		{ (property_key_t)0, NULL },
	};
	const struct props sGroupValidProps[] = {
		{ OB_PROP_S_FULLNAME, OP_GROUPNAME, },
		{ OB_PROP_O_COMPANYID, OP_COMPANYID, },
		{ OB_PROP_S_EMAIL, OP_EMAILADDRESS, },
		{ OB_PROP_B_AB_HIDDEN, OB_AB_HIDDEN, },
		{ (property_key_t)0, NULL },
	};
	const struct props sCompanyValidProps[] = {
		{ OB_PROP_S_FULLNAME, OP_COMPANYNAME, },
		{ OB_PROP_O_SYSADMIN, OP_COMPANYADMIN, },
		{ (property_key_t)0, NULL },
	};
	const struct props *sValidProps;

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);
	auto strSubQuery =
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(objectid.id) + "' " +
		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", objectid.objclass);

	if (lpDeleteProps) {
		// delete properties
		strDeleteQuery =
			"DELETE FROM " + (string)DB_OBJECTPROPERTY_TABLE + " "
			"WHERE objectid = (" + strSubQuery + ") " +
			" AND propname IN (";

		bFirstOne = true;

		for (const auto &prop : *lpDeleteProps) {
			if (!bFirstOne)
				strDeleteQuery += ",";
			strDeleteQuery += prop;
			bFirstOne = false;
		}

		strDeleteQuery += ")";
		auto er = m_lpDatabase->DoDelete(strDeleteQuery);
		if(er != erSuccess)
			throw runtime_error(string("db_query: ") + strerror(er));
	}

	auto strQuery = "REPLACE INTO " + std::string(DB_OBJECTPROPERTY_TABLE) + "(objectid, propname, value) VALUES ";
	switch (objectid.objclass) {
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
		sValidProps = sUserValidProps;
		break;
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
	case DISTLIST_DYNAMIC:
		sValidProps = sGroupValidProps;
		break;
	case CONTAINER_COMPANY:
		sValidProps = sCompanyValidProps;
		break;
	case CONTAINER_ADDRESSLIST:
	default:
		throw runtime_error("Object is wrong type");
	}

	bFirstOne = true;
	unsigned int i = 0;
	while (sValidProps[i].column != NULL) {
		string propvalue = details.GetPropString(sValidProps[i].id);

		if (strcasecmp(sValidProps[i].column, OP_PASSWORD) == 0 &&
		    !propvalue.empty() &&
			/* Password value has special treatment */
		    CreateMD5Hash(propvalue, &propvalue) != erSuccess)
			/* WARNING input and output point to the same data */
			throw runtime_error(string("db_changeUser: create md5"));

		if (sValidProps[i].id == OB_PROP_O_COMPANYID) {
			propvalue = details.GetPropObject(OB_PROP_O_COMPANYID).id;
			// save id as hex in objectproperty.value
			propvalue = bin2hex(propvalue.length(), propvalue.data());
		}

		if (!propvalue.empty()) {
			if (!bFirstOne)
				strQuery += ",";
			strQuery += "((" + strSubQuery + "),'" + m_lpDatabase->Escape(sValidProps[i].column) + "','" +  m_lpDatabase->Escape(propvalue) + "')";
			bFirstOne = false;
		}
		++i;
	}

	/* Load optional anonymous attributes */
	auto anonymousProps = details.GetPropMapAnonymous();
	for (const auto &ap : anonymousProps) {
		if (ap.second.empty())
			continue;
		if (!bFirstOne)
			strQuery += ",";
		if (PROP_TYPE(ap.first) == PT_BINARY)
			strData = base64_encode(ap.second.c_str(), ap.second.size());
		else
			strData = ap.second;
		strQuery +=
			"((" + strSubQuery + "),"
			"'" + m_lpDatabase->Escape(stringify(ap.first, true)) + "',"
			"'" +  m_lpDatabase->Escape(strData) + "')";
		bFirstOne = false;
	}

	/* Only update when there were actually properties provided. */
	if (!bFirstOne) {
		auto er = m_lpDatabase->DoInsert(strQuery);
		if (er != erSuccess)
			throw runtime_error(string("db_query: ") + strerror(er));
	}

	/* Normal properties have been inserted, check for additional MV properties */
	bFirstOne = true;
	strQuery = "REPLACE INTO " + (string)DB_OBJECTMVPROPERTY_TABLE + "(objectid, propname, orderid, value) VALUES ";
	strDeleteQuery =
		"DELETE FROM " + (string)DB_OBJECTMVPROPERTY_TABLE + " "
		"WHERE objectid = (" + strSubQuery + ") " +
		" AND propname IN (";

	auto anonymousMVProps = details.GetPropMapListAnonymous();
	for (const auto &mva : anonymousMVProps) {
		unsigned int ulOrderId = 0;
		if (!bFirstDel)
			strDeleteQuery += ",";
		strDeleteQuery += "'" + m_lpDatabase->Escape(stringify(mva.first, true)) + "'";
		bFirstDel = false;

		if (mva.second.empty())
			continue;

		for (const auto &prop : mva.second) {
			if (prop.empty())
				continue;
			if (!bFirstOne)
				strQuery += ",";
			if (PROP_TYPE(mva.first) == PT_MV_BINARY)
				strData = base64_encode(prop.c_str(), prop.size());
			else
				strData = prop;
			strQuery +=
				"((" + strSubQuery + "),"
				"'" + m_lpDatabase->Escape(stringify(mva.first, true)) + "',"
				"" + stringify(ulOrderId) + ","
				"'" +  m_lpDatabase->Escape(strData) + "')";
			++ulOrderId;
			bFirstOne = false;
		}
	}

	strDeleteQuery += ")";

	/* Only update when there were actually properties provided. */
	if (!bFirstDel) {
		/* Make sure all MV properties which are being overriden are being deleted first */
		auto er = m_lpDatabase->DoDelete(strDeleteQuery);
		if (er != erSuccess)
			throw runtime_error(string("db_query: ") + strerror(er));
	}
	if (!bFirstOne) {
		auto er = m_lpDatabase->DoInsert(strQuery);
		if (er != erSuccess)
			throw runtime_error(string("db_query: ") + strerror(er));
	}

	// Remember modtime for this object
	strQuery = "REPLACE INTO " +  (string)DB_OBJECTPROPERTY_TABLE + "(objectid, propname, value) VALUES ((" + strSubQuery + "),'" + OP_MODTIME + "', FROM_UNIXTIME("+stringify(time(NULL))+"))";
	auto er = m_lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	// Maybe change user type from active to something nonactive
	if (objectid.objclass != details.GetClass() && OBJECTCLASS_TYPE(objectid.objclass) == OBJECTCLASS_TYPE(details.GetClass())) {
		strQuery = "UPDATE object SET objectclass = " + stringify(details.GetClass()) +
			" WHERE externid = '" + m_lpDatabase->Escape(objectid.id) + "' AND objectclass = " + stringify(objectid.objclass);
		er = m_lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess)
			throw runtime_error(string("db_query: ") + strerror(er));
	}
}

objectsignature_t DBPlugin::createObject(const objectdetails_t &details)
{
	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);
	auto objectid = details.GetPropObject(OB_PROP_O_EXTERNID);
	if (!objectid.id.empty())
		// Offline "force" create object
		CreateObjectWithExternId(objectid, details);
	else
		// kopano-admin online create object
		objectid = CreateObject(details);

	// Insert all properties into the database
	changeObject(objectid, details, NULL);

	// signature is empty on first create. This is OK because it doesn't matter what's in it, as long as it changes when the object is modified
	return objectsignature_t(objectid, string());
}

void DBPlugin::deleteObject(const objectid_t &objectid)
{
	DB_RESULT lpResult;
	DB_ROW lpDBRow = NULL;
	unsigned int ulAffRows = 0;

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);

	std::string strSubQuery =
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(objectid.id) + "' "
			"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", objectid.objclass);

	/* First delete company children */
	if (objectid.objclass == CONTAINER_COMPANY) {
		auto strQuery = "SELECT objectid FROM " + std::string(DB_OBJECTPROPERTY_TABLE) +
			" WHERE propname = '" + OP_COMPANYID + "' AND value = hex('" + m_lpDatabase->Escape(objectid.id) + "')";
		auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
		if (er != erSuccess)
			throw runtime_error(string("db_query: ") + strerror(er));

		string children;
		while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
			if(lpDBRow[0] == NULL)
				throw runtime_error(string("db_row_failed: object null"));

			if (!children.empty())
				children += ",";
			children += lpDBRow[0];
		}

		if (!children.empty()) {
			// remove relations for deleted objects
			strQuery =
				"DELETE FROM " + (string)DB_OBJECT_RELATION_TABLE + " "
				"WHERE objectid IN (" + children + ")";
			er = m_lpDatabase->DoDelete(strQuery);
			if (er != erSuccess)
				;//ignore error

			strQuery =
				"DELETE FROM " + (string)DB_OBJECT_RELATION_TABLE + " "
				"WHERE parentobjectid IN (" + children + ")";
			er = m_lpDatabase->DoDelete(strQuery);
			if (er != erSuccess)
				;//ignore error

			// delete object properties
			strQuery =
				"DELETE FROM " + (string)DB_OBJECTPROPERTY_TABLE + " "
				"WHERE objectid IN (" + children + ")";
			er = m_lpDatabase->DoDelete(strQuery);
			if (er != erSuccess)
				;//ignore error

			// delete objects themselves
			strQuery =
				"DELETE FROM " + (string)DB_OBJECT_TABLE + " "
				"WHERE id IN (" + children + ")";
			er = m_lpDatabase->DoDelete(strQuery);
			if (er != erSuccess)
				;//ignore error
		}
	}

	// first delete details of user, since we need the id from the sub query, which is removed next
	auto strQuery = "DELETE FROM " + std::string(DB_OBJECTPROPERTY_TABLE) + " WHERE objectid=(" + strSubQuery + ")";
	auto er = m_lpDatabase->DoDelete(strQuery);
	if (er != erSuccess)
		;// ignore error

	// delete user from object table .. we now have no reference to the user anymore.
	strQuery =
		"DELETE FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(objectid.id) + "' "
			"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", objectid.objclass);

	er = m_lpDatabase->DoDelete(strQuery, &ulAffRows);
	if (er != erSuccess)
		;//FIXME: ....
	if (ulAffRows != 1)
		throw objectnotfound("db_user: " + objectid.id);
}

void DBPlugin::addSubObjectRelation(userobject_relation_t relation, const objectid_t &parentobject, const objectid_t &childobject)
{
	DB_RESULT lpResult;

	if (relation == OBJECTRELATION_USER_SENDAS && childobject.objclass != ACTIVE_USER && OBJECTCLASS_TYPE(childobject.objclass) != OBJECTTYPE_DISTLIST)
		throw notsupported("only active users can send mail");

	LOG_PLUGIN_DEBUG("%s Relation %x", __FUNCTION__, relation);

	auto strParentSubQuery =
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(parentobject.id) + "' "
	   		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", parentobject.objclass);
	auto strChildSubQuery =
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(childobject.id) + "'"
	   		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", childobject.objclass);

	/* Check if relation already exists */
	auto strQuery =
		"SELECT objectid FROM " + (string)DB_OBJECT_RELATION_TABLE + " "
		"WHERE objectid = (" + strChildSubQuery + ") "
		"AND parentobjectid = (" + strParentSubQuery + ") "
		"AND relationtype = " + stringify(relation);
	auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));
	if (lpResult.get_num_rows() != 0)
		throw collision_error(string("Relation exist: ") + stringify(relation));

	/* Insert new relation */ 
	strQuery =
		"INSERT INTO " + (string)DB_OBJECT_RELATION_TABLE + " (objectid, parentobjectid, relationtype) "
		"VALUES ((" + strChildSubQuery + "),(" + strParentSubQuery + ")," + stringify(relation) + ")";

	er = m_lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));
}

void DBPlugin::deleteSubObjectRelation(userobject_relation_t relation, const objectid_t &parentobject, const objectid_t &childobject)
{
	unsigned int ulAffRows = 0;

	LOG_PLUGIN_DEBUG("%s Relation %x", __FUNCTION__, relation);

	auto strParentSubQuery =
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(parentobject.id) + "' "
	   		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", parentobject.objclass);
	auto strChildSubQuery =
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(childobject.id) + "'"
	   		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", childobject.objclass);
	auto strQuery =
		"DELETE FROM " + (string)DB_OBJECT_RELATION_TABLE + " "
		"WHERE objectid = (" + strChildSubQuery + ") "
			"AND parentobjectid = (" + strParentSubQuery + ") "
			"AND relationtype = " + stringify(relation);
	auto er = m_lpDatabase->DoDelete(strQuery, &ulAffRows);
	if (er != erSuccess)
		throw runtime_error("db_query: " + string(strerror(er)));

	if (ulAffRows != 1)
		throw objectnotfound("db_user: relation " + parentobject.id);
}

signatures_t DBPlugin::searchObjects(const std::string &match,
    const char *const *search_props, const char *return_prop,
    unsigned int ulFlags)
{
	std::string strQuery = "SELECT DISTINCT ";
	if (return_prop)
		strQuery += "opret.value, o.objectclass, modtime.value ";
	else
		strQuery += "o.externid, o.objectclass, modtime.value ";
	strQuery +=
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS op "
			"ON op.objectid=o.id ";
    
	if (return_prop != nullptr)
		strQuery +=
			"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS opret "
				"ON opret.objectid=o.id ";

	strQuery +=
		"LEFT JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS modtime "
			"ON modtime.objectid=o.id "
			"AND modtime.propname = '" + OP_MODTIME + "' "
		"WHERE (";

	string strMatch = m_lpDatabase->Escape(match);
	string strMatchPrefix;
	
	if (!(ulFlags & EMS_AB_ADDRESS_LOOKUP)) {
		strMatch = "%" + strMatch + "%";
		strMatchPrefix = " LIKE ";
	} else {
		strMatchPrefix = " = ";
	}

	for (unsigned int i = 0; search_props[i] != NULL; ++i) {
		strQuery += "(op.propname='" + (string)search_props[i] + "' AND op.value " + strMatchPrefix + " '" + strMatch + "')";
		if (search_props[i + 1] != NULL)
			strQuery += " OR ";
	}
	
	strQuery += ")";

	/*
	 * TODO: check with a point system,
	 * if you have 2 objects, one have a match of 99% and one 50%
	 * use the one with 99%
	 */
	auto lpSignatures = CreateSignatureList(strQuery);
	if (lpSignatures.empty())
		throw objectnotfound("db_user: no match: " + match);

	return lpSignatures;
}

quotadetails_t DBPlugin::getQuota(const objectid_t &objectid,
    bool bGetUserDefault)
{
	DB_RESULT lpResult;
	DB_ROW lpDBRow = NULL;

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);
	auto strQuery =
		"SELECT op.propname, op.value "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS op "
			"ON op.objectid = o.id "
		"WHERE o.externid = '" +  m_lpDatabase->Escape(objectid.id) + "' "
	   		"AND " + OBJECTCLASS_COMPARE_SQL("o.objectclass", objectid.objclass);
	auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));
	quotadetails_t lpDetails;
	lpDetails.bIsUserDefaultQuota = bGetUserDefault;

	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if(lpDBRow[0] == NULL || lpDBRow[1] == NULL)
			continue;

		if (bGetUserDefault) {
			if (objectid.objclass != CONTAINER_COMPANY && strcmp(lpDBRow[0], OP_UD_HARDQUOTA) == 0)
				lpDetails.llHardSize = atoll(lpDBRow[1]);
			else if(objectid.objclass != CONTAINER_COMPANY && strcmp(lpDBRow[0], OP_UD_SOFTQUOTA) == 0)
				lpDetails.llSoftSize = atoll(lpDBRow[1]);
			else if(strcmp(lpDBRow[0], OP_UD_WARNQUOTA) == 0)
				lpDetails.llWarnSize = atoll(lpDBRow[1]);
			else if(strcmp(lpDBRow[0], OP_UD_USEDEFAULTQUOTA) == 0)
				lpDetails.bUseDefaultQuota = !!atoi(lpDBRow[1]);
		} else {
			if (objectid.objclass != CONTAINER_COMPANY && strcmp(lpDBRow[0], OP_HARDQUOTA) == 0)
				lpDetails.llHardSize = atoll(lpDBRow[1]);
			else if(objectid.objclass != CONTAINER_COMPANY && strcmp(lpDBRow[0], OP_SOFTQUOTA) == 0)
				lpDetails.llSoftSize = atoll(lpDBRow[1]);
			else if(strcmp(lpDBRow[0], OP_WARNQUOTA) == 0)
				lpDetails.llWarnSize = atoll(lpDBRow[1]);
			else if(strcmp(lpDBRow[0], OP_USEDEFAULTQUOTA) == 0)
				lpDetails.bUseDefaultQuota = !!atoi(lpDBRow[1]);
		}
	}

	return lpDetails;
}

void DBPlugin::setQuota(const objectid_t &objectid, const quotadetails_t &quotadetails)
{
	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);
	auto b = quotadetails.bIsUserDefaultQuota;
	std::string op_default = b ? OP_UD_USEDEFAULTQUOTA : OP_USEDEFAULTQUOTA;
	std::string op_hard    = b ? OP_UD_HARDQUOTA : OP_HARDQUOTA;
	std::string op_soft    = b ? OP_UD_SOFTQUOTA : OP_SOFTQUOTA;
	std::string op_warn    = b ? OP_UD_WARNQUOTA : OP_WARNQUOTA;
	auto strSubQuery =
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(objectid.id) + "' "
	   		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", objectid.objclass);

	// Update new quota settings
	auto strQuery =
		"REPLACE INTO " + (string)DB_OBJECTPROPERTY_TABLE + "(objectid, propname, value) VALUES"
			"((" + strSubQuery + "), '" + op_default + "','" + stringify(quotadetails.bUseDefaultQuota) + "'),"
			"((" + strSubQuery + "), '" + op_hard + "','" + stringify_int64(quotadetails.llHardSize) + "'),"
			"((" + strSubQuery + "), '" + op_soft + "','" + stringify_int64(quotadetails.llSoftSize) + "'),"
			"((" + strSubQuery + "), '" + op_warn + "','" + stringify_int64(quotadetails.llWarnSize) + "')";
	auto er = m_lpDatabase->DoInsert(strQuery);
	if (er != erSuccess) // Maybe on this point the user is broken.
		throw runtime_error(string("db_query: ") + strerror(er));
}

signatures_t DBPlugin::CreateSignatureList(const std::string &query)
{
	signatures_t objectlist;
	DB_RESULT lpResult;
	DB_ROW lpDBRow = NULL;
	string signature;

	auto er = m_lpDatabase->DoSelect(query, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if(lpDBRow[0] == NULL || lpDBRow[1] == NULL)
		    continue;

		if (lpDBRow[2] != NULL)
			signature = lpDBRow[2];
		auto objclass = objectclass_t(atoi(lpDBRow[1]));
		auto lpDBLen = lpResult.fetch_row_lengths();
		assert(lpDBLen != NULL);
		if (lpDBLen[0] == 0)
			throw runtime_error(string("db_row_failed: object empty"));
		objectlist.emplace_back(objectid_t({lpDBRow[0], lpDBLen[0]}, objclass), signature);
	}

	return objectlist;
}

ECRESULT DBPlugin::CreateMD5Hash(const std::string &strData, std::string* lpstrResult)
{
	MD5_CTX crypt;
	std::ostringstream s;

	if (strData.empty() || lpstrResult == NULL)
		return KCERR_INVALID_PARAMETER;

	s.setf(std::ios::hex, std::ios::basefield);
	s.fill('0');
	s.width(8);
	s << rand_mt();
	auto salt = s.str();
	MD5_Init(&crypt);
	MD5_Update(&crypt, salt.c_str(), salt.size());
	MD5_Update(&crypt, strData.c_str(), strData.size());
	*lpstrResult = salt + zcp_md5_final_hex(&crypt);
	return erSuccess;
}

void DBPlugin::addSendAsToDetails(const objectid_t &objectid, objectdetails_t *lpDetails)
{
	for (const auto &objlist : getSubObjectsForObject(OBJECTRELATION_USER_SENDAS, objectid))
		lpDetails->AddPropObject(OB_PROP_LO_SENDAS, objlist.id);
}

abprops_t DBPlugin::getExtraAddressbookProperties()
{
	abprops_t proplist;
	DB_RESULT lpResult;
	DB_ROW lpDBRow = NULL;
	std::string strTable[2] = {DB_OBJECTPROPERTY_TABLE, DB_OBJECTMVPROPERTY_TABLE};

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);

	for (unsigned int i = 0; i < 2; ++i) {
		auto strQuery =
			"SELECT op.propname "
			"FROM " + strTable[i] + " AS op "
			"WHERE op.propname LIKE '0x%' "
				"OR op.propname LIKE '0X%' "
			"GROUP BY op.propname";
		auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
		if (er != erSuccess)
			throw runtime_error(string("db_query: ") + strerror(er));
		while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
			if(lpDBRow[0] == NULL)
				continue;
			proplist.emplace_back(xtoi(lpDBRow[0]));
		}
	}

	return proplist;
}

void DBPlugin::removeAllObjects(objectid_t except)
{
	auto strQuery = "DELETE objectproperty.* FROM objectproperty JOIN object ON object.id = objectproperty.objectid WHERE externid != " + m_lpDatabase->EscapeBinary(except.id);
	auto er = m_lpDatabase->DoDelete(strQuery);
	if(er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));
		
	strQuery = "DELETE FROM object WHERE externid != " + m_lpDatabase->EscapeBinary(except.id);
	er = m_lpDatabase->DoDelete(strQuery);
	if(er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));
}

void DBPlugin::CreateObjectWithExternId(const objectid_t &objectid, const objectdetails_t &details)
{
	DB_RESULT lpResult;

	// check if object already exists
	auto strQuery =
		"SELECT id "
		"FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = " + m_lpDatabase->EscapeBinary(objectid.id) + " "
		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", OBJECTCLASS_CLASSTYPE(details.GetClass()));

	auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));
	if (lpResult.fetch_row() != nullptr)
		throw collision_error(string("Object exists: ") + bin2hex(objectid.id));

	strQuery =
		"INSERT INTO " + (string)DB_OBJECT_TABLE + "(externid, objectclass) "
		"VALUES('" + m_lpDatabase->Escape(objectid.id) + "'," + stringify(objectid.objclass) + ")";

	er = m_lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));
}

objectid_t DBPlugin::CreateObject(const objectdetails_t &details)
{
	DB_RESULT lpResult;
	DB_ROW lpDBRow = NULL;
	std::string strPropName, strPropValue;
	GUID guidExternId = {0};

	switch (details.GetClass()) {
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
		strPropName = OP_LOGINNAME;
		strPropValue = details.GetPropString(OB_PROP_S_LOGIN);
		break;
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
	case DISTLIST_DYNAMIC:
		strPropName = OP_GROUPNAME;
		strPropValue = details.GetPropString(OB_PROP_S_FULLNAME);
		break;
	case CONTAINER_COMPANY:
		strPropName = OP_COMPANYNAME;
		strPropValue = details.GetPropString(OB_PROP_S_FULLNAME);
		break;
	case CONTAINER_ADDRESSLIST:
	default:
		throw runtime_error("Object is wrong type");
	}

	// check if object already exists
	auto strQuery =
		"SELECT o.id, op.value "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS op "
			"ON op.objectid = o.id AND op.propname = '" + strPropName + "' "
		"LEFT JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS oc "
			"ON oc.objectid = o.id AND oc.propname = '" + (string)OP_COMPANYID + "' "
		"WHERE op.value = '" + m_lpDatabase->Escape(strPropValue) + "' "
			"AND " + OBJECTCLASS_COMPARE_SQL("o.objectclass", OBJECTCLASS_CLASSTYPE(details.GetClass()));

		if (m_bHosted && details.GetClass() != CONTAINER_COMPANY)
			strQuery += " AND (oc.value IS NULL OR oc.value = hex('" + m_lpDatabase->Escape(details.GetPropObject(OB_PROP_O_COMPANYID).id) + "'))";

	auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));
	while ((lpDBRow = lpResult.fetch_row()) != nullptr)
		if (lpDBRow[1] != NULL && strcasecmp(lpDBRow[1], strPropValue.c_str()) == 0)
			throw collision_error(string("Object exist: ") + strPropValue);

	if (CoCreateGuid(&guidExternId) != S_OK)
		throw runtime_error("failed to generate extern id");
	std::string strExternId(reinterpret_cast<char *>(&guidExternId), sizeof(guidExternId));
	strQuery =
		"INSERT INTO " + (string)DB_OBJECT_TABLE + "(objectclass, externid) "
		"VALUES (" + stringify(details.GetClass()) + "," +
		m_lpDatabase->EscapeBinary(strExternId) + ")";

	er = m_lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	return objectid_t(strExternId, details.GetClass());
}
