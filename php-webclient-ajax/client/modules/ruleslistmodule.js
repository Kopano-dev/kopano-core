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
 * Rules list module
 *
 * Retrieves all rules from the server. These rules are both stored in the module (this.rules) and
 * displayed to the user.
 *
 * Requires: storeid via setData
 */
 
ruleslistmodule.prototype = new ListModule;
ruleslistmodule.prototype.constructor = ruleslistmodule;
ruleslistmodule.superclass = ListModule.prototype;

function ruleslistmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
}

ruleslistmodule.prototype.init = function(id, element, title, data)
{
	ruleslistmodule.superclass.init.call(this, id, element, title, data, "internalid");

	this.initializeView();

	this.action = "list";
	this.rules = new Array();
	this.xml = new XMLBuilder();
	
	this.events["rowcolumn"]["rule_state"] = new Object();
	this.events["rowcolumn"]["rule_state"]["click"] = eventListChangeCompleteStatus;

	// remove column events, to disable sorting on columns
	delete this.events["column"];
}

// the only reason we override the execute function is that we want to both
// display the data to the user AND save the rules in the window object so 
// we can send them back when the user wants to save things
ruleslistmodule.prototype.execute = function(type, action)
{
	var actionRules = action.getElementsByTagName("item");
	for(var i = 0; i < actionRules.length; i++) {
		var rule = collapseXML(actionRules[i]);
		
		// Add into the xml document an internalid field, as if it came from 
		// the server.
		var child = action.ownerDocument.createElement("internalid");
		child.appendChild(action.ownerDocument.createTextNode(""+this.rules.length));
		actionRules[i].appendChild(child);
		
		this.rules.push(rule);
	}

	// display data
	ruleslistmodule.superclass.execute.call(this, type, action);

	// when entryid of wastebasket is send, store it
	this.wastebasket = dhtml.getXMLValue(action, "wastebasket_entryid", "");
}

// Sends the request to the server. We only need the store ID because we only
// support the standard rules on the inbox. Theoretically we could have different
// rules per folder, but as outlook doesn't support rules on folders other than
// the inbox, neither do we.
ruleslistmodule.prototype.list = function()
{
	if(this.storeid) {
		var data = new Object();
		data["store"] = this.storeid;
		
		webclient.xmlrequest.addData(this, this.action, data);
		webclient.xmlrequest.sendRequest();

		this.loadMessage();
	}
}

// Override listmodule's onOpenItem to do nothing; we don't want to open with the
// default viewer when the item when it is clicked
ruleslistmodule.prototype.onOpenItem = function() {
}

// Allows users of the module to get a rule
ruleslistmodule.prototype.getRule = function(internalid) {
	var rule = this.rules[internalid];
	return rule;
}

// Allows users of the module to set a rule (edit/new)
ruleslistmodule.prototype.setRule = function(internalid, rule) {
	// Store rule in this.rules

	// Rule is 'dirty', ie it should be sent to server on 'Ok'
	rule.dirty = new Object;
	rule.dirty.value = "1";
	
	if(typeof(internalid)!="boolean" && typeof(internalid)!="undefined") { // check if we want to edit a rule
		// set the rule_id when the original item has one
		if (typeof(this.rules[internalid].rule_id)!="undefined"){
			rule.rule_id = new Object;
			rule.rule_id.value = this.rules[internalid].rule_id.value;
		}

		// Put the internal rule id into the rule again. This is needed because
		// the listmodule needs to know the internalid
		rule.internalid = new Object;
		rule.internalid.value = ""+internalid;
		this.rules[internalid] = rule;
	} else {
		// Use the given sequence if available, otherwise create a new sequence ID
		// Because we add rules to the bottom, we add a higher sequence ID than other rules
		if(!rule.rule_sequence || !rule.rule_sequence.value) {
			var maxseq = 10; // Outlook starts at 10, so so do we
		
			for(var i=0;i<this.rules.length;i++) {
				if(this.rules[i].rule_sequence && parseInt(this.rules[i].rule_sequence.value) >= maxseq)
					maxseq =  parseInt(this.rules[i].rule_sequence.value) + 1;
			}
			
			rule.rule_sequence = new Object;
			rule.rule_sequence.value = maxseq;
		}

		// New rule, internalid is simply the length of the rules array
		rule.internalid = new Object;
		rule.internalid.value = "" + this.rules.length;
		this.rules.push(rule);
	}
		
	// Build a DOM document just like it had been sent from the server. This would normally
	// be sent in an 'item' container.
	var dom = buildDOM(rule, "item");

	ruleslistmodule.superclass.execute.call(this, "item", dom);	
}

// Allows users of the module to delete rules
ruleslistmodule.prototype.deleteRules = function(internalids) {
	var delreq = new Object;
	delreq.internalid = new Array;
	for(var i=0;i<internalids.length;i++) {
		// Mark rule as deleted
		this.rules[internalids[i]].deleted = new Object;
		this.rules[internalids[i]].deleted.value = true;
		
		// Delete rule from view
		var delobj = new Object;
		delobj.value = internalids[i];
		delreq.internalid.push(delobj);
	}
	
	var dom = buildDOM(delreq, "delete");
	ruleslistmodule.superclass.execute.call(this, "delete", dom);
}

// Get all modified rules
ruleslistmodule.prototype.getModifiedRules = function() {
	var result = new Array();
	
	for(var i=0;i<this.rules.length;i++) {
		if(this.rules[i].dirty && this.rules[i].dirty.value) {
			result.push(this.rules[i]);
		} 
		if(this.rules[i].deleted && this.rules[i].deleted.value) {
			result.push(this.rules[i]);
		} 
	}
	
	return result;
}

// Send all modified rules to the server
ruleslistmodule.prototype.submitRules = function() {
	var rules = this.getModifiedRules();
	var data = new Object;
	data["store"] = this.storeid;
	
	data.rules = new Array();
	for(var rule in rules) {
		data.rules.push(buildXML(rules[rule]));
	}
	
	parentWebclient.xmlrequest.addData(this, "setRules", data, webclient.modulePrefix);
	parentWebclient.xmlrequest.sendRequest(true);
}

// Swaps two rules, but leaves the rule_sequence the same. Ie the rules swap position, and 
// get eachothers sequence. The internalid remains unchanged, as the ID of the row in the table
// (which is equal to the internalid), also remains unchanged.
ruleslistmodule.prototype.swap = function(internalid1, internalid2) {
	var seq1 = this.rules[internalid1].rule_sequence.value;
	var seq2 = this.rules[internalid2].rule_sequence.value;
	
	// Give the rules eachothers sequence ID
	this.rules[internalid1].rule_sequence.value = seq2;
	this.rules[internalid2].rule_sequence.value = seq1;

	// Both rules are now dirty
	this.rules[internalid1].dirty = new Object;
	this.rules[internalid1].dirty.value = true;
	this.rules[internalid2].dirty = new Object;
	this.rules[internalid2].dirty.value = true;
	
	// Send an update to the view to swap the two rows
	
	var data = new Object;
	data.internalid = new Array;
	var internalid = new Object;
	internalid.value = internalid1;
	data.internalid.push(internalid);
	internalid = new Object;
	internalid.value = internalid2;
	data.internalid.push(internalid);
	
	// Call the swap
	var dom = buildDOM(data, "swap");
	ruleslistmodule.superclass.execute.call(this, "swap", dom);
}

/**
 * Called when the 'completestatus' is changed for a row; ie when a checkbox is modified
 */
ruleslistmodule.prototype.completeStatus = function(internalid, status) 
{
	if (status){
		this.rules[internalid].rule_state.value = this.rules[internalid].rule_state.value.replace("ST_DISABLED", "ST_ENABLED");
	}else{
		this.rules[internalid].rule_state.value = this.rules[internalid].rule_state.value.replace("ST_ENABLED", "ST_DISABLED");
	}
	this.rules[internalid].dirty = new Object;
	this.rules[internalid].dirty.value = true;
}

