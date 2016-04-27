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

suggestEmailAddressModule.prototype = new Module;
suggestEmailAddressModule.prototype.constructor = suggestEmailAddressModule;
suggestEmailAddressModule.superclass = Module.prototype;

function suggestEmailAddressModule(id, suggestlist){
	if(arguments.length > 1) {
		this.init(id, suggestlist);
	}
}

suggestEmailAddressModule.prototype.init = function(id, suggestlist){
	this.id = id;
	this.suggestionlists = new Array();
	this.addSuggestionList(suggestlist);
	this.cache = new Array();
	suggestEmailAddressModule.superclass.init.call(this, id);
}

suggestEmailAddressModule.prototype.addSuggestionList = function(suggestlist){
	if(typeof suggestlist == "object"){
		this.suggestionlists[suggestlist.id] = suggestlist;
	}
}

suggestEmailAddressModule.prototype.getList = function(suggestlistId, str){
	if(typeof this.cache[str] == "object"){
		if(typeof this.suggestionlists[suggestlistId] == "object"){
			this.suggestionlists[suggestlistId].handleResult(str, this.cache[str]);
		}
	}else{
		webclient.xmlrequest.addData(this, "getRecipientList", {searchstring: str, returnid: suggestlistId});
		webclient.xmlrequest.sendRequest();
	}
}

suggestEmailAddressModule.prototype.deleteRecipient = function(suggestlistId, recipient){
	var emailAddresses = new Array();
	// retieve the email addresses from the text and sent to server to delete it.
	var recipients = (recipient.indexOf(";") > 1) ? recipient.split(";") : [recipient];
	for (var i=0; i<recipients.length; i++ ){
		emailAddresses.push(recipients[i].substring(recipients[i].indexOf("<")+1, recipients[i].length-1).trim());
	}
	recipient = emailAddresses.join(";");
	webclient.xmlrequest.addData(this, "deleteRecipient", {deleteRecipient: recipient, returnid: suggestlistId});
	webclient.xmlrequest.sendRequest();
	this.cache = new Array();
}

suggestEmailAddressModule.prototype.execute = function(type, action){
	var result = new Array();
	var searchstring = false;
	try{
		var resultcollection = action.getElementsByTagName("result");
		for(var i=0;i<resultcollection.length;i++){
			if(resultcollection[i].firstChild != null){
				result.push(resultcollection[i].firstChild.nodeValue);
			}
		}
		searchstring = dhtml.getXMLValue(action, "searchstring", "");
		var returnId = dhtml.getXMLValue(action, "returnid", false);

		this.cache[searchstring] = result;
	}
	catch(e){
	}

	if(typeof returnId != "undefined" && typeof this.suggestionlists[returnId] == "object"){
		this.suggestionlists[returnId].handleResult(searchstring, result);
	}
}

