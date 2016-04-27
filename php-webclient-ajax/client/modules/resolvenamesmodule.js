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
 * --ResolveNames Module--   
 * 
 * This module is able to resolve names  
 * 
 * HOWTO BUILD:
 *  - add the module to the webclient:
 *  		var rnm = new resolvenamesmodule();
 * HOWTO USE:
 *  - to resolve a string:
 *  		resolveObj = new Object();
 * 		  resolveObj["to"] = "fra;mi";
 *  		rnm.resolveNames(resolveObj,handlerFunction);
 * HOWTO REACT:
 *  - when all names are resolved "handlerFunction" will be executed and get the users in a Object  
 * DEPENDS ON:
 * |------> dhtml.js
 * |------> utils.js
 * |------> webclient.js
 * |------> module.js 
 * |----+-> resolvenames.php
 * |    |----> dialog.js
 * |----+-> class.resolvenamesmodule.php
 * |    |----> class.module.php
 * |    |----> class.mapisession.php
 * |    |----> class.bus.php  
 */
resolvenamesmodule.prototype = new Module;
resolvenamesmodule.prototype.constructor = resolvenamesmodule;
resolvenamesmodule.superclass = Module.prototype;

//PUBLIC
/**
 * Function is the constuctor of the module
 * @param id = "int" id of the module in the webclient enviroment 
 */ 
function resolvenamesmodule(id)
{
	this.id = id;
}

/**
 * Function will execute the received XML and send it to the "addResultToQue" function
 * after that "walkQue" function will be executed     
 * @param type = "string" containing the type name
 * @param action = "XML Object" of all received items 
 */
resolvenamesmodule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "checknames":
			this.addResultToQue(action);
			break;
	}
	this.walkQue();
}

/**
 * Function have te be called to resolve names
 * it will reset the old resolveQue and build a new one 
 * @param resolveObj = "Object" like:  ["to"] = "fran;mi"
 *                                     ["cc"] = "vee;erk"
 * @param handler = "Function" this param will be called when all the names are resolved
 * @param excludeusercontacts in boolean which specify the type of contacts folder to check while resolving the names.
 */ 
resolvenamesmodule.prototype.resolveNames = function(resolveObj, handler, excludeusercontacts)
{
	this.resolveQue = new Array();
	this.returnList = new Object();
	this.fieldCounter = 0;
	this.keywordCounter = 0;
	
	var names = new Array();
	for(type in resolveObj){
		var recipientnames = resolveObj[type].split(";");
		for(var j = 0; j < recipientnames.length; j++)
		{
			var name = new Object();
			name["id"] = recipientnames[j];
			name["type"] = type;
			names.push(name);
		}
	}
	
	this.handler = handler;
	this.verifyNames(names, excludeusercontacts);
}

/**
 * Function will be executed when one name is resolved by popup or "walkQue" function
 * @param type = "string" of the type like: "to"
 * @param nameid = "string" of the source resolve name like: "joh"
 * @param nameObj = "Object" like:  ["nameid"] = "joh"
 *                                  ["nametype"] = "to"  
 *                                  ["fullname"] = "John Doe"
 *                                  ["emailaddress"] = "john.doe@foo.com"
 *                                  etc. 
 */ 
resolvenamesmodule.prototype.verifiedName = function(type,nameid,nameObj)
{
	if(!this.returnList[type]){
		this.returnList[type] = new Object();
	}
	if(nameObj["message_class"] == "IPM.DistList"){
		var tempNames = new Array();
		for (var i=0; i<nameObj.members.length ; i++){
			var member = nameObj.members[i];
			if(member.type == "ZARAFA"){
				tempNames.push("["+member.fullname+"]");
			}else{
				tempNames.push(member.fullname + " <" + member.emailaddress +">");
			}
		}
		nameObj["fullname"] = "";
		nameObj["emailaddress"] = tempNames.join("; ");
	}
	this.returnList[type][nameid] = nameObj;
	if(this.checkName){
		this.checkName.close();
	}
	this.keywordCounter++;
	this.walkQue();
}

//PRIVATE
/**
 * Function will send "names" to the webserver to resolve the names
 * @param names = "Array of Objects" like:  [0]["id"] = "fra";
 *                                             ["type"] = "to";
 *                                          [1]["id"] = "mi";
 *                                             ["type"] = "to"; 
 * @param excludeusercontacts in boolean which specify the type of contacts folder to check for resolving the names.
 */ 
resolvenamesmodule.prototype.verifyNames = function(names, excludeusercontacts)
{
	if(names) {
		var data = new Object();
		data["excludeusercontacts"] = excludeusercontacts;
		data["name"] = names;

		webclient.xmlrequest.addData(this, "checknames", data);
		webclient.xmlrequest.sendRequest();
	}
}

/**
 * Function will add "items" to the ResolveQue
 * @param items = "XML Object" like:   <action type="checknames"><name> 
 *                                     	<fullname>Frans van Veen</fullname>
 *                                     	etc. 
 *                                     </name>
 *                                     <name>
 *                                     	<fullname>Michael Erkens</fullname>
 *                                     	etc. 
 *                                     </name></action>  
 */ 
resolvenamesmodule.prototype.addResultToQue = function(items)
{
	items = items.getElementsByTagName("name");
	for(var i=0; i<items.length; i++){
		var item = new Object();
		item["userid"] = dhtml.getTextNode(items[i].getElementsByTagName("userid")[0],"");
		item["username"] = dhtml.getTextNode(items[i].getElementsByTagName("username")[0],"");
		item["fullname"] = dhtml.getTextNode(items[i].getElementsByTagName("fullname")[0],"");
		item["emailaddress"] = dhtml.getTextNode(items[i].getElementsByTagName("emailaddress")[0],"");
		item["admin"] = dhtml.getTextNode(items[i].getElementsByTagName("admin")[0],"");
		item["nonactive"] = dhtml.getTextNode(items[i].getElementsByTagName("nonactive")[0],"");
		item["nameid"] = dhtml.getTextNode(items[i].getElementsByTagName("nameid")[0],"");
		item["nametype"] = dhtml.getTextNode(items[i].getElementsByTagName("nametype")[0],"");
		item["objecttype"] = dhtml.getTextNode(items[i].getElementsByTagName("objecttype")[0],"");
		item["message_class"] = dhtml.getTextNode(items[i].getElementsByTagName("message_class")[0],"");
		// if item is DistList then get all the members in it and create an array of members.
		// as we cant send distlist as group, so we have to expand those members details in return.
		if (item["message_class"] == "IPM.DistList"){
			var membersData = dhtml.getXMLNode(items[i], "members").getElementsByTagName("member");
			if(membersData){
				item["members"] = this.createMembersOfDistlist(membersData);
			}
		}
		
		var typeEntry = -1;
		for(var j=0; j<this.resolveQue.length; j++){
			if(this.resolveQue[j]["value"] == item["nametype"]){
				typeEntry = j;
			}
		}
		if(typeEntry == -1){
			var temp = new Object();
			temp["value"] = item["nametype"];
			temp["message_class"] = item["message_class"];
			temp["names"] = Array();
			typeEntry = this.resolveQue.length;
			this.resolveQue.push(temp);
		}

		var nameidEntry = -1;
		for(var j=0; j<this.resolveQue[typeEntry]["names"].length; j++){
			if(this.resolveQue[typeEntry]["names"][j]["value"] == item["nameid"]){
				nameidEntry = j;
			}
		}
		if(nameidEntry == -1){
			var temp = new Object();
			temp["value"] = item["nameid"];
			temp["items"] = Array();
			nameidEntry = this.resolveQue[typeEntry]["names"].length;
			this.resolveQue[typeEntry]["names"].push(temp);
		}

		this.resolveQue[typeEntry]["names"][nameidEntry]["items"].push(item);
	}
}

/**
 * Function will calculate all members in a distlist
 */ 
resolvenamesmodule.prototype.createMembersOfDistlist = function(membersData){
	var members = new Array();
	for (var i=0; i < membersData.length ; i++){
		var memObj = new Object();
		memObj["fullname"] = dhtml.getXMLValue(membersData[i], "display_name", "");
		memObj["emailaddress"] = dhtml.getXMLValue(membersData[i], "emailaddress", "");
		memObj["nameid"] = dhtml.getXMLValue(membersData[i], "nameid", "");
		memObj["nametype"] = dhtml.getXMLValue(membersData[i], "nametype", "");
		memObj["type"] = dhtml.getXMLValue(membersData[i], "type", "");
		members.push(memObj);
	}
	return members;
 }
/**
 * Function will walk the resolveQue list
 * if there are more then one item is connected to a resolve this function will show popup
 * if all names are resolved this function will execute "this.handler"
 */ 
resolvenamesmodule.prototype.walkQue = function()
{
	if(this.resolveQue[this.fieldCounter]) {
		if(this.resolveQue[this.fieldCounter]["names"][this.keywordCounter]) {
		
			var keyword = this.resolveQue[this.fieldCounter]["names"][this.keywordCounter]["value"];
			var type = this.resolveQue[this.fieldCounter]["value"];
			if(this.resolveQue[this.fieldCounter]["names"][this.keywordCounter]["items"].length > 1){	
				this.checkName = webclient.openWindow(this, "checkNames", DIALOG_URL+"task=resolvenames_modal&module_id="+this.id+"&keyword="+keyword+"&type="+type, 400, 290, false);
			} else {
				var nameObj = this.resolveQue[this.fieldCounter]["names"][this.keywordCounter]["items"][0];
				this.verifiedName(type,keyword,nameObj);
			}
			
		} else {
			this.fieldCounter++;
			this.keywordCounter = 0;
			this.walkQue();
		}
	} else {
		this.handler(this.returnList);
		this.resolveQue = new Array();
		this.returnList = new Array();
	}
}
