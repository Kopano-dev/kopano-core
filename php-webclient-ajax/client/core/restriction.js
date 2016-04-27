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

/**
 * --Restriction Object--  
 * @classDescription	 This object can be used to create and parse restrictions
 * 
 * Depends on:
 * -----> constants.js
 */

/**
 * @constructor
 * This widget can be used to create and parse restrictions
 * instead of creating class and instantiating it when we need it
 * we are creating a static object here because this class will not 
 * store any data it just creates restriction structure
 */
Restriction = new Object();

/**
 * values that can be used in below functions
 * RELOP		RELOP_LT, RELOP_LE, RELOP_GT, RELOP_GE, RELOP_EQ, RELOP_NE, RELOP_RE
 * FUZZYLEVEL	FL_FULLSTRING, FL_SUBSTRING, FL_PREFIX, FL_IGNORECASE, FL_IGNORENONSPACE, FL_LOOSE
 * BITMASKOP	BMR_EQZ, BMR_NEZ
 */

/***** Functions to create restrictions *******/

/**
 * create RES_AND structure
 * @param		Array		restrictionsArray	array of restrictions that should be ANDed
 * @return		Array		res					RES_AND restriction array
 */
Restriction.createResAnd = function(restrictionsArray) {
	var res = new Array();
	var resCondition = RES_AND;
	var resValues = new Array();

	for(var index = 0; index < restrictionsArray.length; index++) {
		resValues.push(restrictionsArray[index]);
	}

	res.push(resCondition);
	res.push(resValues);

	return res;
}

/**
 * create RES_OR structure
 * @param		Array		restrictionsArray	array of restrictions that should be ORed
 * @return		Array		res					RES_OR restriction array
 */
Restriction.createResOr = function(restrictionsArray) {
	var res = new Array();
	var resCondition = RES_OR;
	var resValues = new Array();

	for(var index = 0; index < restrictionsArray.length; index++) {
		resValues.push(restrictionsArray[index]);
	}

	res.push(resCondition);
	res.push(resValues);

	return res;
}

/**
 * create RES_NOT structure
 * @param		Array		subRestriction		a single restriction
 * @return		Array		res					RES_NOT restriction array
 */
Restriction.createResNot = function(subRestriction) {
	var res = new Array();
	var resCondition = RES_NOT;
	var resValues = new Array(subRestriction);

	res.push(resCondition);
	res.push(resValues);

	return res;
}

/**
 * create RES_COMPAREPROPS structure
 * @param		String		propTag1		first property tag
 * @param		String		propTag1		second property tag
 * @return		Array		res				RES_COMPAREPROPS restriction array
 */
Restriction.createResCompareProps = function(propTag1, propTag2) {
	var res = new Array();

	var resCondition = RES_COMPAREPROPS;

	var resValues = new Object();
	resValues[ULPROPTAG1] = propTag1;
	resValues[ULPROPTAG2] = propTag2;

	res.push(resCondition);
	res.push(resValues);

	return res;
}

/**
 * create RES_SUBRESTRICTION structure
 * @param		String			propTag				property tag
 *													(can be only PR_MESSAGE_ATTACHMENTS or PR_MESSAGE_RECIPIENTS)
 * @param		Array			subRestriction		a single sub restriction
 * @return		Array			res					RES_SUBRESTRICTION restriction array
 */
Restriction.createResSubRestriction = function(propTag, subRestriction) {
	var res = new Array();

	var resCondition = RES_SUBRESTRICTION;

	var resValues = new Object();
	resValues[ULPROPTAG] = propTag;
	resValues[RESTRICTION] = subRestriction;

	res.push(resCondition);
	res.push(resValues);

	return res;
}

/**
 * create PROPERTY structure
 * @param		String			propTag				property tag
 * @param		String/Number	propValue			value of property
 * @param		String			propTagForValue		prop tag to use in value part (multi valued properties)
 * @param		String			valueType			specifies type of value (binary)
 * @return		Array			res					PROPERTY restriction array
 */
Restriction.createPropertyObject = function(propTag, propValue, propTagForValue, valueType) {
	var prop = new Array();

	if(!propTagForValue) {
		propTagForValue = propTag;
	}

	prop[ULPROPTAG] = propTag;
	prop[VALUE] = new Object();
	prop[VALUE][propTagForValue] = propValue;
	if(typeof valueType != "undefined" && valueType) {
		prop[VALUE]["type"] = valueType;
	}

	return prop;
}

/**
 * create RES_COMMENT structure
 * @param		Array		subRestriction		a single sub restriction
 * @param		Array		propertiesArray		array of RES_PROPERTY objects
 * @return		Array		res					RES_COMMENT restriction array
 */
Restriction.dataResComment = function(subRestriction, propertiesArray) {
	var res = new Array();

	var resCondition = RES_COMMENT;

	var resValues = new Object();
	resValues[RESTRICTION] = subRestriction;
	resValues[PROPS] = new Array();

	for(var index = 0; index < propertiesArray.length; index++) {
		resValues[PROPS].push(propertiesArray[index]);
	}

	res.push(resCondition);
	res.push(resValues);

	return res;
}

/**
 * create RES_PROPERTY structure
 * @param		String			propTag				property tag
 * @param		Number			relOp				relational operator
 * @param		String/Number	propValue			value of property
 * @param		String			propTagForValue		prop tag to use in value part (multi valued properties)
 * @param		String			valueType			specifies type of value (binary)
 * @return		Array			res					RES_PROPERTY restriction array
 */
Restriction.dataResProperty = function(propTag, relOp, propValue, propTagForValue, valueType) {
	var res = new Array();

	if(!propTagForValue) {
		propTagForValue = propTag;
	}

	var resCondition = RES_PROPERTY;

	var resValues = new Object();
	resValues[RELOP] = relOp;
	resValues[ULPROPTAG] = propTag;
	resValues[VALUE] = new Object();
	resValues[VALUE][propTagForValue] = propValue;
	if(typeof valueType != "undefined" && valueType) {
		resValues[VALUE]["type"] = valueType;
	}

	res.push(resCondition);
	res.push(resValues);

	return res;
}

/**
 * create RES_EXIST structure
 * @param		String			propTag			property tag
 * @return		Array			res				RES_EXIST restriction array
 */
Restriction.dataResExist = function(propTag) {
	var res = new Array();

	var resCondition = RES_EXIST;

	var resValues = new Object();
	resValues[ULPROPTAG] = propTag;

	res.push(resCondition);
	res.push(resValues);

	return res;
}

/**
 * create RES_SIZE structure
 * @param		String			propTag			property tag
 * @param		Number			relOp			relational operator
 * @param		Number			sizeValue		value of size
 * @return		Array			res				RES_SIZE restriction array
 */
Restriction.dataResSize = function(propTag, relOp, sizeValue) {
	var res = new Array();

	var resCondition = RES_SIZE;

	var resValues = new Object();
	resValues[ULPROPTAG] = propTag;
	resValues[RELOP] = relOp;
	resValues[CB] = sizeValue;

	res.push(resCondition);
	res.push(resValues);

	return res;
}

/**
 * create RES_BITMASK structure
 * @param		String			propTag			property tag
 * @param		Number			bitMaskType		bitmask flag type
 * @param		Number			bitMaskValue	bitmask flag value
 * @return		Array			res				RES_BITMASK restriction array
 */
Restriction.dataResBitmask = function(propTag, bitMaskType, bitMaskValue) {
	var res = new Array();

	var resCondition = RES_BITMASK;

	var resValues = new Object();
	resValues[ULPROPTAG] = propTag;
	resValues[ULTYPE] = bitMaskType;
	resValues[ULMASK] = bitMaskValue;

	res.push(resCondition);
	res.push(resValues);

	return res;
}

/**
 * create RES_CONTENT structure
 * @param		String			propTag				property tag
 * @param		Number			fuzzyLevel			string comparison type
 * @param		String			propValue			string value for comparison
 * @param		String			propTagForValue		prop tag to use in value part (multi valued properties)
 * @param		String			valueType			specifies type of value (binary)
 * @return		Array			res					RES_CONTENT restriction array
 */
Restriction.dataResContent = function(propTag, fuzzyLevel, propValue, propTagForValue, valueType) {
	var res = new Array();

	if(!propTagForValue) {
		propTagForValue = propTag;
	}

	var resCondition = RES_CONTENT;

	var resValues = new Object();
	resValues[FUZZYLEVEL] = 0;
	if(typeof fuzzyLevel == "object") {
		for(var index in fuzzyLevel) {
			resValues[FUZZYLEVEL] += fuzzyLevel[index];
		}
	} else {
		resValues[FUZZYLEVEL] = fuzzyLevel;
	}
	resValues[ULPROPTAG] = propTag;
	resValues[VALUE] = new Object();
	resValues[VALUE][propTagForValue] = propValue;
	if(typeof valueType != "undefined" && valueType) {
		resValues[VALUE]["type"] = valueType;
	}

	res.push(resCondition);
	res.push(resValues);

	return res;
}