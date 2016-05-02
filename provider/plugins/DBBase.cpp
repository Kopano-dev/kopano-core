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
#include <memory>

#include "DBBase.h"
#include <kopano/ECDefs.h>
#include <kopano/EMSAbTag.h>
#include <kopano/stringutil.h>
#include <kopano/md5.h>
#include <mapidefs.h>
#include <kopano/base64.h>

KDLLAPI ECRESULT GetDatabaseObject(ECDatabase **lppDatabase);

DBPlugin::DBPlugin(pthread_mutex_t *pluginlock, ECPluginSharedData *shareddata) :
	UserPlugin(pluginlock, shareddata), m_lpDatabase(NULL) {
}

DBPlugin::~DBPlugin() {
    // Do not delete m_lpDatabase as it is freed when the thread exits
}

void DBPlugin::InitPlugin() {

	if(GetDatabaseObject(&m_lpDatabase) != erSuccess)
	    throw runtime_error(string("db_init: cannot get handle to database"));
}

std::unique_ptr<signatures_t>
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

std::unique_ptr<objectdetails_t>
DBPlugin::getObjectDetails(const objectid_t &objectid)
{
	std::unique_ptr<std::map<objectid_t, objectdetails_t> > objectdetails;
	list<objectid_t> objectids;

	objectids.push_back(objectid);
	objectdetails = DBPlugin::getObjectDetails(objectids);

	if (objectdetails->size() != 1)
		throw objectnotfound(objectid.id);

	return std::unique_ptr<objectdetails_t>(new objectdetails_t(objectdetails->begin()->second));
}

std::unique_ptr<std::map<objectid_t, objectdetails_t> >
DBPlugin::getObjectDetails(const std::list<objectid_t> &objectids)
{
	map<objectid_t,objectdetails_t> *mapdetails = new map<objectid_t,objectdetails_t>;
	map<objectid_t,objectdetails_t>::iterator iterDetails;
	ECRESULT er;
	map<objectclass_t, string> objectstrings;
	std::map<objectclass_t, std::string>::const_iterator iterStrings;
	string strQuery;
	string strSubQuery;
	DB_RESULT_AUTOFREE lpResult(m_lpDatabase);
	DB_ROW lpDBRow = NULL;
	DB_LENGTHS lpDBLen = NULL;
	objectdetails_t details;
	objectid_t lastid;
	objectid_t curid;
	std::list<objectid_t>::const_iterator iterID;

	if(objectids.empty())
		return std::unique_ptr<std::map<objectid_t, objectdetails_t> >(mapdetails);

	LOG_PLUGIN_DEBUG("%s N=%d", __FUNCTION__, (int)objectids.size());

	for (std::list<objectid_t>::const_iterator i = objectids.begin();
	     i != objectids.end(); ++i) {
		if (!objectstrings[i->objclass].empty())
			objectstrings[i->objclass] += ", ";
		objectstrings[i->objclass] += "'" + m_lpDatabase->Escape(i->id) + "'";
	}

	/* Create subquery which combines all externids with the matching objectclass */
	for (iterStrings = objectstrings.begin();
	     iterStrings != objectstrings.end(); ++iterStrings) {
		if (iterStrings != objectstrings.begin())
			strSubQuery += " OR ";
		strSubQuery += "(o.externid IN (" + iterStrings->second + ") "
				"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", iterStrings->first) + ")";
	}

	strQuery =
		"SELECT o.externid, o.objectclass, op.propname, op.value "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"LEFT JOIN "+(string)DB_OBJECTPROPERTY_TABLE+" AS op "
		"ON op.objectid=o.id "
		"WHERE (" + strSubQuery + ") "
		"ORDER BY o.externid, o.objectclass";

	er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if(er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	while((lpDBRow = m_lpDatabase->FetchRow(lpResult)) != NULL)
	{
		// No way to determine externid
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL)
			continue;

		lpDBLen = m_lpDatabase->FetchRowLengths(lpResult);
		if (lpDBLen == NULL || lpDBLen[0] == 0)
			continue;

		curid = objectid_t(string(lpDBRow[0], lpDBLen[0]), (objectclass_t)atoi(lpDBRow[1]));

		if (lastid != curid && !lastid.id.empty()) {
			details.SetClass(lastid.objclass);
			addSendAsToDetails(lastid, &details);
			(*mapdetails)[lastid] = details;

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
			details.SetPropInt(OB_PROP_I_ADMINLEVEL, min(2, atoi(lpDBRow[3])));
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
		else if (strnicmp(lpDBRow[2], "0x", strlen("0x")) == 0) {
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
		(*mapdetails)[lastid] = details;
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

	while((lpDBRow = m_lpDatabase->FetchRow(lpResult)) != NULL) {
		if(lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL || lpDBRow[3] == NULL)
			continue;

		lpDBLen = m_lpDatabase->FetchRowLengths(lpResult);
		if (lpDBLen == NULL || lpDBLen[2] == 0)
			continue;

		curid = objectid_t(string(lpDBRow[2], lpDBLen[2]), (objectclass_t)atoi(lpDBRow[3]));

		if (lastid != curid) {
			iterDetails = mapdetails->find(curid);
			if (iterDetails == mapdetails->end())
				continue;
		}

		lastid = curid;

		if (strnicmp(lpDBRow[0], "0x", strlen("0x")) == 0) {
			unsigned int key = xtoi(lpDBRow[0]);
			if (PROP_TYPE(key) == PT_BINARY || PROP_TYPE(key) == PT_MV_BINARY)
				iterDetails->second.AddPropString((property_key_t)key, base64_decode(lpDBRow[1]));
			else
				iterDetails->second.AddPropString((property_key_t)key, lpDBRow[1]);
		}
	}

	return std::unique_ptr<std::map<objectid_t, objectdetails_t> >(mapdetails);
}

std::unique_ptr<signatures_t>
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

std::unique_ptr<signatures_t>
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
	ECRESULT er;
	string strQuery;
	string strSubQuery;
	string strDeleteQuery;
	bool bFirstOne = true;
	bool bFirstDel = true;
	string strData;
	string strMD5Pw;
	property_map anonymousProps;
	property_map::const_iterator iterAnonymous;
	property_mv_map anonymousMVProps;
	property_mv_map::const_iterator iterMVAnonymous;
	std::list<std::string>::const_iterator iterProps;
	unsigned int ulOrderId = 0;

	struct props sUserValidProps[] = {
		{ OB_PROP_S_LOGIN, OP_LOGINNAME, },
		{ OB_PROP_S_PASSWORD, OP_PASSWORD, },
		{ OB_PROP_S_EMAIL, OP_EMAILADDRESS, },
		{ OB_PROP_I_ADMINLEVEL, OP_ISADMIN, },
		{ OB_PROP_S_FULLNAME, OP_FULLNAME, },
		{ OB_PROP_O_COMPANYID, OP_COMPANYID, },
		{ OB_PROP_B_AB_HIDDEN, OB_AB_HIDDEN, },
		{ (property_key_t)0, NULL },
	};
	struct props sGroupValidProps[] = {
		{ OB_PROP_S_FULLNAME, OP_GROUPNAME, },
		{ OB_PROP_O_COMPANYID, OP_COMPANYID, },
		{ OB_PROP_S_EMAIL, OP_EMAILADDRESS, },
		{ OB_PROP_B_AB_HIDDEN, OB_AB_HIDDEN, },
		{ (property_key_t)0, NULL },
	};
	struct props sCompanyValidProps[] = {
		{ OB_PROP_S_FULLNAME, OP_COMPANYNAME, },
		{ OB_PROP_O_SYSADMIN, OP_COMPANYADMIN, },
		{ (property_key_t)0, NULL },
	};
	struct props *sValidProps;

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);

	strSubQuery = 
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

		for (iterProps = lpDeleteProps->begin();
		     iterProps != lpDeleteProps->end(); ++iterProps) {
			if (!bFirstOne)
				strDeleteQuery += ",";
			strDeleteQuery += *iterProps;
		}

		strDeleteQuery += ")";

		er = m_lpDatabase->DoDelete(strDeleteQuery);
		if(er != erSuccess)
			throw runtime_error(string("db_query: ") + strerror(er));

	}

	strQuery = "REPLACE INTO " + (string)DB_OBJECTPROPERTY_TABLE + "(objectid, propname, value) VALUES ";

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

		if (stricmp(sValidProps[i].column, OP_PASSWORD) == 0 && !propvalue.empty()) {
			// Password value has special treatment
			if (CreateMD5Hash(propvalue, &propvalue) != erSuccess) // WARNING input and output point to the same data
				throw runtime_error(string("db_changeUser: create md5"));
		}

		if (sValidProps[i].id == OB_PROP_O_COMPANYID) {
			propvalue = details.GetPropObject(OB_PROP_O_COMPANYID).id;
			// save id as hex in objectproperty.value
			propvalue = bin2hex(propvalue.length(), (const unsigned char*)propvalue.data());
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
	anonymousProps = details.GetPropMapAnonymous();
	for (iterAnonymous = anonymousProps.begin();
	     iterAnonymous != anonymousProps.end(); ++iterAnonymous) {
		if (!iterAnonymous->second.empty()) {
			if (!bFirstOne)
				strQuery += ",";
			if (PROP_TYPE(iterAnonymous->first) == PT_BINARY) {
				strData = base64_encode((const unsigned char*)iterAnonymous->second.c_str(), iterAnonymous->second.size());
			} else {
				strData = iterAnonymous->second;
			}
			strQuery +=
				"((" + strSubQuery + "),"
				"'" + m_lpDatabase->Escape(stringify(iterAnonymous->first, true)) + "',"
				"'" +  m_lpDatabase->Escape(strData) + "')";
			bFirstOne = false;
		}
	}

	/* Only update when there were actually properties provided. */
	if (!bFirstOne) {
		er = m_lpDatabase->DoInsert(strQuery);
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

	anonymousMVProps = details.GetPropMapListAnonymous();
	for (iterMVAnonymous = anonymousMVProps.begin();
	     iterMVAnonymous != anonymousMVProps.end(); ++iterMVAnonymous) {
		ulOrderId = 0;

		if (!bFirstDel)
			strDeleteQuery += ",";
		strDeleteQuery += "'" + m_lpDatabase->Escape(stringify(iterMVAnonymous->first, true)) + "'";
		bFirstDel = false;

		if (iterMVAnonymous->second.empty())
			continue;

		for (iterProps = iterMVAnonymous->second.begin();
		     iterProps != iterMVAnonymous->second.end(); ++iterProps) {
			if (!iterProps->empty()) {
				if (!bFirstOne)
					strQuery += ",";
				if (PROP_TYPE(iterMVAnonymous->first) == PT_MV_BINARY) {
					strData = base64_encode((const unsigned char*)iterProps->c_str(), iterProps->size());
				} else {
					strData = *iterProps;
				}
				strQuery +=
					"((" + strSubQuery + "),"
					"'" + m_lpDatabase->Escape(stringify(iterMVAnonymous->first, true)) + "',"
					"" + stringify(ulOrderId) + ","
					"'" +  m_lpDatabase->Escape(strData) + "')";
				++ulOrderId;
				bFirstOne = false;
			}
		}
	}

	strDeleteQuery += ")";

	/* Only update when there were actually properties provided. */
	if (!bFirstDel) {
		/* Make sure all MV properties which are being overriden are being deleted first */
		er = m_lpDatabase->DoDelete(strDeleteQuery);
		if (er != erSuccess)
			throw runtime_error(string("db_query: ") + strerror(er));
	}
	if (!bFirstOne) {
		er = m_lpDatabase->DoInsert(strQuery);
		if (er != erSuccess)
			throw runtime_error(string("db_query: ") + strerror(er));
	}

	// Remember modtime for this object
	strQuery = "REPLACE INTO " +  (string)DB_OBJECTPROPERTY_TABLE + "(objectid, propname, value) VALUES ((" + strSubQuery + "),'" + OP_MODTIME + "', FROM_UNIXTIME("+stringify(time(NULL))+"))";
	er = m_lpDatabase->DoInsert(strQuery);
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
	objectid_t objectid;

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);

	objectid = details.GetPropObject(OB_PROP_O_EXTERNID);
	if (!objectid.id.empty()) {
		// Offline "force" create object
		CreateObjectWithExternId(objectid, details);

	} else {
		// kopano-admin online create object
		objectid = CreateObject(details);
	}

	// Insert all properties into the database
	changeObject(objectid, details, NULL);

	// signature is empty on first create. This is OK because it doesn't matter what's in it, as long as it changes when the object is modified
	return objectsignature_t(objectid, string());
}

void DBPlugin::deleteObject(const objectid_t &objectid)
{
	ECRESULT er;
	string strQuery;
	string strSubQuery;
	DB_RESULT_AUTOFREE lpResult(m_lpDatabase);
	DB_ROW lpDBRow = NULL;
	unsigned int ulAffRows = 0;

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);

	strSubQuery = 
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(objectid.id) + "' "
			"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", objectid.objclass);

	/* First delete company children */
	if (objectid.objclass == CONTAINER_COMPANY) {
		strQuery = "SELECT objectid FROM " + (string)DB_OBJECTPROPERTY_TABLE +
			" WHERE propname = '" + OP_COMPANYID + "' AND value = hex('" + m_lpDatabase->Escape(objectid.id) + "')";

		er = m_lpDatabase->DoSelect(strQuery, &lpResult);
		if (er != erSuccess)
			throw runtime_error(string("db_query: ") + strerror(er));

		string children;

		while ((lpDBRow = m_lpDatabase->FetchRow(lpResult)) != NULL) {
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
			if (er != erSuccess){
				//ignore error
			}

			strQuery =
				"DELETE FROM " + (string)DB_OBJECT_RELATION_TABLE + " "
				"WHERE parentobjectid IN (" + children + ")";
			er = m_lpDatabase->DoDelete(strQuery);
			if (er != erSuccess){
				//ignore error
			}

			// delete object properties
			strQuery =
				"DELETE FROM " + (string)DB_OBJECTPROPERTY_TABLE + " "
				"WHERE objectid IN (" + children + ")";
			er = m_lpDatabase->DoDelete(strQuery);
			if (er != erSuccess){
				//ignore error
			}

			// delete objects themselves
			strQuery =
				"DELETE FROM " + (string)DB_OBJECT_TABLE + " "
				"WHERE id IN (" + children + ")";
			er = m_lpDatabase->DoDelete(strQuery);
			if (er != erSuccess){
				//ignore error
			}
		}
	}

	// first delete details of user, since we need the id from the sub query, which is removed next
	strQuery = "DELETE FROM "+(string)DB_OBJECTPROPERTY_TABLE+" WHERE objectid=("+strSubQuery+")";
	er = m_lpDatabase->DoDelete(strQuery);
	if (er != erSuccess){
		// ignore error
	}

	// delete user from object table .. we now have no reference to the user anymore.
	strQuery =
		"DELETE FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(objectid.id) + "' "
			"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", objectid.objclass);

	er = m_lpDatabase->DoDelete(strQuery, &ulAffRows);
	if(er != erSuccess){
		//FIXME: ....
	}
	
	if (ulAffRows != 1)
		throw objectnotfound("db_user: " + objectid.id);
}

void DBPlugin::addSubObjectRelation(userobject_relation_t relation, const objectid_t &parentobject, const objectid_t &childobject)
{
	ECRESULT er = erSuccess;
	DB_RESULT_AUTOFREE lpResult(m_lpDatabase);
	string strQuery;
	string strParentSubQuery;
	string strChildSubQuery;

	if (relation == OBJECTRELATION_USER_SENDAS && childobject.objclass != ACTIVE_USER && OBJECTCLASS_TYPE(childobject.objclass) != OBJECTTYPE_DISTLIST)
		throw notsupported("only active users can send mail");

	LOG_PLUGIN_DEBUG("%s Relation %x", __FUNCTION__, relation);

	strParentSubQuery = 
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(parentobject.id) + "' "
	   		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", parentobject.objclass);

	strChildSubQuery = 
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(childobject.id) + "'"
	   		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", childobject.objclass);

	/* Check if relation already exists */
	strQuery =
		"SELECT objectid FROM " + (string)DB_OBJECT_RELATION_TABLE + " "
		"WHERE objectid = (" + strChildSubQuery + ") "
		"AND parentobjectid = (" + strParentSubQuery + ") "
		"AND relationtype = " + stringify(relation);
	er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	if (m_lpDatabase->GetNumRows(lpResult) != 0)
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
	ECRESULT er = erSuccess;
	string strQuery;
	unsigned int ulAffRows = 0;
	string strParentSubQuery;
	string strChildSubQuery;

	LOG_PLUGIN_DEBUG("%s Relation %x", __FUNCTION__, relation);

	strParentSubQuery = 
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(parentobject.id) + "' "
	   		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", parentobject.objclass);

	strChildSubQuery = 
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(childobject.id) + "'"
	   		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", childobject.objclass);

	strQuery =
		"DELETE FROM " + (string)DB_OBJECT_RELATION_TABLE + " "
		"WHERE objectid = (" + strChildSubQuery + ") "
			"AND parentobjectid = (" + strParentSubQuery + ") "
			"AND relationtype = " + stringify(relation);

	er = m_lpDatabase->DoDelete(strQuery, &ulAffRows);
	if (er != erSuccess)
		throw runtime_error("db_query: " + string(strerror(er)));

	if (ulAffRows != 1)
		throw objectnotfound("db_user: relation " + parentobject.id);
}

std::unique_ptr<signatures_t> DBPlugin::searchObjects(const std::string &match,
    const char **search_props, const char *return_prop, unsigned int ulFlags)
{
	string signature;
	objectid_t objectid;
	std::unique_ptr<signatures_t> lpSignatures(new signatures_t());

	string strQuery =
		"SELECT DISTINCT ";
	if (return_prop)
		strQuery += "opret.value, o.objectclass, modtime.value ";
	else
		strQuery += "o.externid, o.objectclass, modtime.value ";
	strQuery +=
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS op "
			"ON op.objectid=o.id ";
    
	if (return_prop) {
		strQuery +=
			"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS opret "
				"ON opret.objectid=o.id ";
	}

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
	lpSignatures = CreateSignatureList(strQuery);
	if (lpSignatures->empty())
		throw objectnotfound("db_user: no match: " + match);

	return lpSignatures;
}

std::unique_ptr<quotadetails_t> DBPlugin::getQuota(const objectid_t &objectid,
    bool bGetUserDefault)
{
	std::unique_ptr<quotadetails_t> lpDetails;
	ECRESULT er = erSuccess;
	string strQuery;
	DB_RESULT_AUTOFREE lpResult(m_lpDatabase);
	DB_ROW lpDBRow = NULL;

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);

	strQuery =
		"SELECT op.propname, op.value "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS op "
			"ON op.objectid = o.id "
		"WHERE o.externid = '" +  m_lpDatabase->Escape(objectid.id) + "' "
	   		"AND " + OBJECTCLASS_COMPARE_SQL("o.objectclass", objectid.objclass);

	er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	lpDetails = std::unique_ptr<quotadetails_t>(new quotadetails_t());
	lpDetails->bIsUserDefaultQuota = bGetUserDefault;

	while ((lpDBRow = m_lpDatabase->FetchRow(lpResult)) != NULL) {
		if(lpDBRow[0] == NULL || lpDBRow[1] == NULL)
			continue;

		if (bGetUserDefault) {
			if (objectid.objclass != CONTAINER_COMPANY && strcmp(lpDBRow[0], OP_UD_HARDQUOTA) == 0)
				lpDetails->llHardSize = _atoi64(lpDBRow[1]);
			else if(objectid.objclass != CONTAINER_COMPANY && strcmp(lpDBRow[0], OP_UD_SOFTQUOTA) == 0)
				lpDetails->llSoftSize = _atoi64(lpDBRow[1]);
			else if(strcmp(lpDBRow[0], OP_UD_WARNQUOTA) == 0)
				lpDetails->llWarnSize = _atoi64(lpDBRow[1]);
			else if(strcmp(lpDBRow[0], OP_UD_USEDEFAULTQUOTA) == 0)
				lpDetails->bUseDefaultQuota = !!atoi(lpDBRow[1]);
		} else {
			if (objectid.objclass != CONTAINER_COMPANY && strcmp(lpDBRow[0], OP_HARDQUOTA) == 0)
				lpDetails->llHardSize = _atoi64(lpDBRow[1]);
			else if(objectid.objclass != CONTAINER_COMPANY && strcmp(lpDBRow[0], OP_SOFTQUOTA) == 0)
				lpDetails->llSoftSize = _atoi64(lpDBRow[1]);
			else if(strcmp(lpDBRow[0], OP_WARNQUOTA) == 0)
				lpDetails->llWarnSize = _atoi64(lpDBRow[1]);
			else if(strcmp(lpDBRow[0], OP_USEDEFAULTQUOTA) == 0)
				lpDetails->bUseDefaultQuota = !!atoi(lpDBRow[1]);
		}
	}

	return lpDetails;
}

void DBPlugin::setQuota(const objectid_t &objectid, const quotadetails_t &quotadetails)
{
	ECRESULT er = erSuccess;
	string strQuery;
	string strSubQuery;
	string op_default;
	string op_hard;
	string op_soft;
	string op_warn;

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);

	if (quotadetails.bIsUserDefaultQuota) {
		op_default = OP_UD_USEDEFAULTQUOTA;
		op_hard = OP_UD_HARDQUOTA;
		op_soft = OP_UD_SOFTQUOTA;
		op_warn = OP_UD_WARNQUOTA;
	} else {
		op_default = OP_USEDEFAULTQUOTA;
		op_hard = OP_HARDQUOTA;
		op_soft = OP_SOFTQUOTA;
		op_warn = OP_WARNQUOTA;
	}

	strSubQuery = 
		"SELECT id FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = '" + m_lpDatabase->Escape(objectid.id) + "' "
	   		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", objectid.objclass);

	// Update new quota settings
	strQuery =
		"REPLACE INTO " + (string)DB_OBJECTPROPERTY_TABLE + "(objectid, propname, value) VALUES"
			"((" + strSubQuery + "), '" + op_default + "','" + stringify(quotadetails.bUseDefaultQuota) + "'),"
			"((" + strSubQuery + "), '" + op_hard + "','" + stringify_int64(quotadetails.llHardSize) + "'),"
			"((" + strSubQuery + "), '" + op_soft + "','" + stringify_int64(quotadetails.llSoftSize) + "'),"
			"((" + strSubQuery + "), '" + op_warn + "','" + stringify_int64(quotadetails.llWarnSize) + "')";

	er = m_lpDatabase->DoInsert(strQuery);
	if (er != erSuccess) // Maybe on this point the user is broken.
		throw runtime_error(string("db_query: ") + strerror(er));
}

std::unique_ptr<signatures_t>
DBPlugin::CreateSignatureList(const std::string &query)
{
	ECRESULT er = erSuccess;
	std::unique_ptr<signatures_t> objectlist(new signatures_t());
	DB_RESULT_AUTOFREE lpResult(m_lpDatabase);
	DB_ROW lpDBRow = NULL;
	DB_LENGTHS lpDBLen = NULL;
	string signature;
	objectclass_t objclass;
	objectid_t objectid;

	er = m_lpDatabase->DoSelect(query, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	while ((lpDBRow = m_lpDatabase->FetchRow(lpResult)) != NULL) {
		if(lpDBRow[0] == NULL || lpDBRow[1] == NULL)
		    continue;

		if (lpDBRow[2] != NULL)
			signature = lpDBRow[2];

		objclass = objectclass_t(atoi(lpDBRow[1]));

		lpDBLen = m_lpDatabase->FetchRowLengths(lpResult);
		ASSERT(lpDBLen != NULL);
		if (lpDBLen[0] == 0)
			throw runtime_error(string("db_row_failed: object empty"));

		objectid = objectid_t(string(lpDBRow[0], lpDBLen[0]), objclass);
		objectlist->push_back(objectsignature_t(objectid, signature));
	}

	return objectlist;
}

ECRESULT DBPlugin::CreateMD5Hash(const std::string &strData, std::string* lpstrResult)
{
	MD5_CTX crypt;
	std::string salt;
	std::ostringstream s;

	if (strData.empty() || lpstrResult == NULL)
		return KCERR_INVALID_PARAMETER;

	s.setf(ios::hex, ios::basefield);
	s.fill('0');
	s.width(8);
	s << rand_mt();
	salt = s.str();

	MD5_Init(&crypt);
	MD5_Update(&crypt, salt.c_str(), salt.size());
	MD5_Update(&crypt, strData.c_str(), strData.size());
	*lpstrResult = salt + zcp_md5_final_hex(&crypt);
	return erSuccess;
}

void DBPlugin::addSendAsToDetails(const objectid_t &objectid, objectdetails_t *lpDetails)
{
	std::unique_ptr<signatures_t> sendas;
	signatures_t::const_iterator iter;

	sendas = getSubObjectsForObject(OBJECTRELATION_USER_SENDAS, objectid);

	for (iter = sendas->begin(); iter != sendas->end(); ++iter)
		lpDetails->AddPropObject(OB_PROP_LO_SENDAS, iter->id);
}

std::unique_ptr<abprops_t> DBPlugin::getExtraAddressbookProperties(void)
{
	ECRESULT er = erSuccess;
	std::unique_ptr<abprops_t> proplist(new abprops_t());
	DB_RESULT_AUTOFREE lpResult(m_lpDatabase);
	DB_ROW lpDBRow = NULL;
	std::string strQuery;
	std::string strTable[2];

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);

	strTable[0] = (string)DB_OBJECTPROPERTY_TABLE;
	strTable[1] = (string)DB_OBJECTMVPROPERTY_TABLE;

	for (unsigned int i = 0; i < 2; ++i) {
		strQuery =
			"SELECT op.propname "
			"FROM " + strTable[i] + " AS op "
			"WHERE op.propname LIKE '0x%' "
				"OR op.propname LIKE '0X%' "
			"GROUP BY op.propname";

		er = m_lpDatabase->DoSelect(strQuery, &lpResult);
		if (er != erSuccess)
			throw runtime_error(string("db_query: ") + strerror(er));

		while ((lpDBRow = m_lpDatabase->FetchRow(lpResult)) != NULL) {
			if(lpDBRow[0] == NULL)
				continue;

			proplist->push_back(xtoi(lpDBRow[0]));
		}

	}

	return proplist;
}

void DBPlugin::removeAllObjects(objectid_t except)
{
	ECRESULT er = erSuccess;
	std::string strQuery;
	
	strQuery = "DELETE objectproperty.* FROM objectproperty JOIN object ON object.id = objectproperty.objectid WHERE externid != " + m_lpDatabase->EscapeBinary(except.id);
	er = m_lpDatabase->DoDelete(strQuery);
	if(er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));
		
	strQuery = "DELETE FROM object WHERE externid != " + m_lpDatabase->EscapeBinary(except.id);
	er = m_lpDatabase->DoDelete(strQuery);
	if(er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));
}

void DBPlugin::CreateObjectWithExternId(const objectid_t &objectid, const objectdetails_t &details)
{
	ECRESULT er;
	string strQuery;
	DB_RESULT_AUTOFREE lpResult(m_lpDatabase);

	// check if object already exists
	strQuery = 
		"SELECT id "
		"FROM " + (string)DB_OBJECT_TABLE + " "
		"WHERE externid = " + m_lpDatabase->EscapeBinary((unsigned char*)objectid.id.c_str(), objectid.id.length()) + " "
			"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", OBJECTCLASS_CLASSTYPE(details.GetClass()));

	er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	if (m_lpDatabase->FetchRow(lpResult) != NULL)
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
	ECRESULT er;
	string strQuery;
	DB_RESULT_AUTOFREE lpResult(m_lpDatabase);
	DB_ROW lpDBRow = NULL;
	string strPropName;
	string strPropValue;
	GUID guidExternId = {0};
	string strExternId;

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
	strQuery =
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

	er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	while ((lpDBRow = m_lpDatabase->FetchRow(lpResult)) != NULL)
		if (lpDBRow[1] != NULL && stricmp(lpDBRow[1], strPropValue.c_str()) == 0)
			throw collision_error(string("Object exist: ") + strPropValue);

	if (CoCreateGuid(&guidExternId) != S_OK)
		throw runtime_error("failed to generate extern id");

	strExternId.assign((const char*)&guidExternId, sizeof(guidExternId));

	strQuery =
		"INSERT INTO " + (string)DB_OBJECT_TABLE + "(objectclass, externid) "
		"VALUES (" + stringify(details.GetClass()) + "," +
		m_lpDatabase->EscapeBinary((unsigned char*)strExternId.c_str(), strExternId.length()) + ")";

	er = m_lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	return objectid_t(strExternId, details.GetClass());
}
