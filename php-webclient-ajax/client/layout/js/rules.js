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

function eventRulesOpenItem(ruleid) {
	editRule(ruleid);
}

function ruleCallBack(result, callBackData)
{
	var internalid = false;
	if (typeof(callBackData)!="undefined" && typeof(callBackData.internalid)!="undefined"){
	    internalid = callBackData.internalid;
	}
	module.setRule(internalid, result);
    
    return true;
}

// Called by rule dialog to get rule information
function getRule(internalid) {
    return window.rules[internalid];
}

// UI buttons
function addRule() {
	var callBackData = new Object;

    webclient.openModalDialog(module, "rule", DIALOG_URL+"task=rule_modal", 520, 550, ruleCallBack, callBackData);
}

function editRule(internalid) {
	if (typeof(ruleid)=="undefined"){
	    var selected = module.getSelectedIDs();
		internalid = selected[0];
	}

	if (typeof(internalid)!="undefined"){
		var callBackData = new Object;
	    callBackData.internalid = internalid;
    
	    webclient.openModalDialog(module, "rule", DIALOG_URL+"task=rule_modal&internalid=" + internalid, 520, 550, ruleCallBack, callBackData);
	}
}

function deleteRule() {
    var selected = module.getSelectedIDs();

    if (selected.length>0)
	    module.deleteRules(selected);
}

function moveUp() {
    var selected = module.getSelectedRowNumber(); 
    
    if(selected == 0) {
        // Already at top
        return;
    }
    
    var internalid1 = module.getMessageByRowNumber(selected);
    var internalid2 = module.getMessageByRowNumber(selected-1);
    
    module.swap(internalid1, internalid2);
}

function moveDown() {
    var selected = module.getSelectedRowNumber();
    
    if(selected == module.getRowCount() - 1)
        return; // Already at bottom
        
    var internalid1 = module.getMessageByRowNumber(selected);
    var internalid2 = module.getMessageByRowNumber(selected+1);
  
    module.swap(internalid2, internalid1);
}

function submitRules() {
    module.submitRules();
	return true;
}
