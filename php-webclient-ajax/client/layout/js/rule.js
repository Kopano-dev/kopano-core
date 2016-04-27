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

//  Setup the screen to display the given rule
function showRule(rule)
{
    dhtml.getElementById("name").value = rule.rule_name.value;
    dhtml.getElementById("sequence").value = rule.rule_sequence.value;

	var processRule = false;
	if(rule.rule_state.value.indexOf("ST_EXIT_LEVEL") > 0)
		processRule = true;

	dhtml.getElementById("stop_processing_more_rules").checked = processRule;

    
    if(rule.rule_condition.restype && rule.rule_condition.restype.value == "RES_AND") {
		// first try the condition as a single condition (with multiple restrictions)
		if (showCondition(rule.rule_condition) == false){
			// it wasn't a single condition...
	        for(var i=0; i< rule.rule_condition.restriction.length; i++) {
		        if(showCondition(rule.rule_condition.restriction[i]) == false) {
			        alert(_("Unable to load a rule condition"));
				}
	        }
		}
    } else {
        if(showCondition(rule.rule_condition) == false) {
            alert(_("Unable to show the rule condition"));
            return false;
        }
    }
    
    if(rule.rule_actions.length != 1) {
        alert(_("Unable to show a rule with more than one action"));
        return false;
    }
    
    if(showAction(rule.rule_actions[0], rule.rule_state) == false) {
        alert(_("Unable to show the rule action"));
        return false;
    }
}

function showCondition(condition)
{
    // From contains
    if(condition.restype.value == "RES_CONTENT" && condition.proptag.value == "PR_SENDER_SEARCH_KEY" && condition.fuzzylevel.value == "FL_SUBSTRING")
        dhtml.getElementById("cond_from").value = hex2bin(condition.value.value).toLowerCase(); // hex because sender search key is hex ..
    // Subject contains
    else if(condition.restype.value == "RES_CONTENT" && condition.proptag.value == "PR_SUBJECT" && condition.fuzzylevel.value == "FL_SUBSTRING | FL_IGNORECASE")
        dhtml.getElementById("cond_subject").value = condition.value.value;
    // Priority is
    else if(condition.restype.value == "RES_PROPERTY" && condition.proptag.value == "PR_IMPORTANCE" && condition.relop.value == "RELOP_EQ")
        dhtml.getElementById("cond_priority").value = condition.value.value;
    // Sent to
    else if(condition.restype.value == "RES_SUBRESTRICTION" && condition.proptag.value == "PR_MESSAGE_RECIPIENTS" &&
        condition.restriction.restype.value == "RES_COMMENT" && condition.restriction.property.length == 4 && 
        condition.restriction.property[0].attributes.proptag == "0x60000003" &&
        condition.restriction.property[1].attributes.proptag == "0x00010102" &&
        condition.restriction.property[2].attributes.proptag == "0x0001001E" &&
        condition.restriction.property[3].attributes.proptag == "PR_DISPLAY_TYPE" &&
        condition.restriction.restriction.restype.value == "RES_PROPERTY" &&
            condition.restriction.restriction.relop.value == "RELOP_EQ" &&
            condition.restriction.restriction.proptag.value == "PR_SEARCH_KEY") 
    {
        dhtml.getElementById("cond_sent_to").value = condition.restriction.property[2].value;
        dhtml.getElementById("cond_sent_to").entryid = condition.restriction.property[1].value;
        dhtml.getElementById("cond_sent_to").displayname = condition.restriction.property[2].value;
        dhtml.getElementById("cond_sent_to").addrtype = hex2bin(condition.restriction.restriction.value.value).split(":")[0];
        var emailaddress = hex2bin(condition.restriction.restriction.value.value).split(":")[1];
        if(emailaddress) {
            emailaddress = emailaddress.substr(0, emailaddress.length-1); // remove trailing \0
            dhtml.getElementById("cond_sent_to").emailaddress = emailaddress;
        }
        dhtml.getElementById("cond_sent_to").searchkey = condition.restriction.restriction.value.value;
    }
    // Sent only to me
    else if(condition.restype.value == "RES_AND" &&
        condition.restriction.length == 3 &&
          condition.restriction[0].restype.value == "RES_PROPERTY" &&
          condition.restriction[0].relop.value == "RELOP_EQ" &&
          condition.restriction[0].proptag.value == "PR_MESSAGE_TO_ME" &&
            condition.restriction[1].restype.value == "RES_NOT" &&
            condition.restriction[1].restriction.restype.value == "RES_CONTENT" &&
            condition.restriction[1].restriction.fuzzylevel.value == "FL_SUBSTRING" &&
            condition.restriction[1].restriction.proptag.value == "PR_DISPLAY_TO" &&
            condition.restriction[1].restriction.value.value == ";" &&
          condition.restriction[2].restype.value == "RES_PROPERTY" &&
          condition.restriction[2].relop.value == "RELOP_EQ" &&
          condition.restriction[2].proptag.value == "PR_DISPLAY_CC")
    {      
        dhtml.getElementById("cond_sent_to_me").checked = true;
    }
    else if(condition.restype.value == "RES_EXIST" && condition.proptag.value == "PR_MESSAGE_CLASS")
    {
        // No condition selected; matches all 
    }
	// From contains to compatible with OL07
	else if(condition.restype.value == "RES_COMMENT" && condition.restriction.proptag.value == "PR_SENDER_SEARCH_KEY")
	{
		var fromString = hex2bin(condition.restriction.value.value).toLowerCase(); // hex because sender search key is hex ;
		fromString = fromString.substring(fromString.indexOf(":")+1, fromString.length-1);
		dhtml.getElementById("cond_from").value = fromString;
	}
    else return false;
        
    return true;
}

function showAction(action, state)
{
    if(typeof(action.action) == "undefined")
        return;
    if(action.action.value == "OP_MOVE") {
		if (action.folderentryid.value == parentwindow.module.wastebasket && state.value.match("ST_EXIT_LEVEL")){
			dhtml.getElementById("action_delete").checked = true;
			dhtml.getElementById("stop_processing_more_rules").disabled = true;
		}else{
	        dhtml.getElementById("action_move").checked = true;
	        dhtml.getElementById("action_move_folder").folderentryid = action.folderentryid.value;
	        dhtml.getElementById("action_move_folder").storeentryid = action.storeentryid.value;
	        dhtml.getElementById("action_move_folder").innerHTML = action.foldername.value.htmlEntities();
		}
    } else if(action.action.value == "OP_COPY") {
        dhtml.getElementById("action_copy").checked = true;
        dhtml.getElementById("action_copy_folder").folderentryid = action.folderentryid.value;
        dhtml.getElementById("action_copy_folder").storeentryid = action.storeentryid.value;
        dhtml.getElementById("action_copy_folder").innerHTML = action.foldername.value.htmlEntities();
    } else if(action.action.value == "OP_FORWARD") {
		var smtpaddress = "";
		var emailaddress = "";
		var addrType = "";
		var displayName = "";
		var objectType = MAPI_MAILUSER;
		
		for(var i=0;i<action.adrlist.address.property.length;i++) {
			if(!action.adrlist.address.property[i].attributes) {
				continue;
			}

			if(action.adrlist.address.property[i].attributes.proptag == "PR_SMTP_ADDRESS") {
				smtpaddress = action.adrlist.address.property[i].value;
			}
			if(action.adrlist.address.property[i].attributes.proptag == "PR_EMAIL_ADDRESS") {
				emailaddress = action.adrlist.address.property[i].value;
			}
			if(action.adrlist.address.property[i].attributes.proptag == "PR_ADDRTYPE") {
				addrType = action.adrlist.address.property[i].value;
			}
			if(action.adrlist.address.property[i].attributes.proptag == "PR_OBJECT_TYPE") {
				objectType = action.adrlist.address.property[i].value;
			}
			if(action.adrlist.address.property[i].attributes.proptag == "PR_DISPLAY_NAME") {
				displayName = action.adrlist.address.property[i].value;
			}
		}

		if(smtpaddress && displayName) {
			emailaddress = nameAndEmailToString(displayName, smtpaddress, objectType);
		}

		if(action.flavor.value == "0"){
			dhtml.getElementById("action_forward").checked = true;
			dhtml.getElementById("action_forward_address").value = emailaddress;
		}else if(action.flavor.value == "FWD_AS_ATTACHMENT"){
			dhtml.getElementById("action_forward_attach").checked = true;
			dhtml.getElementById("action_forward_attach_address").value = emailaddress;
		}else if(action.flavor.value == "FWD_PRESERVE_SENDER | FWD_DO_NOT_MUNGE_MSG"){
			dhtml.getElementById("action_redirect").checked = true;
			dhtml.getElementById("action_redirect_address").value = emailaddress;
		}
	}
}

// Called when user presses 'ok'. Returns true if window should close, false otherwise
function submitRule()
{
    var result;
    // Check sanity
    var action_move = dhtml.getElementById("action_move").checked;
    var action_copy = dhtml.getElementById("action_copy").checked;
    var action_delete = dhtml.getElementById("action_delete").checked;
    var action_forward = dhtml.getElementById("action_forward").checked;
    var action_forward_attach = dhtml.getElementById("action_forward_attach").checked;
    var action_redirect = dhtml.getElementById("action_redirect").checked;
    
    var action_move_folder = dhtml.getElementById("action_move_folder");
    var action_copy_folder = dhtml.getElementById("action_copy_folder");

	if(action_forward){
		var action_address = dhtml.getElementById("action_forward_address").value;
	}else if(action_forward_attach){
		var action_address = dhtml.getElementById("action_forward_attach_address").value;
	}else if(action_redirect){
	    var action_address = dhtml.getElementById("action_redirect_address").value;
	}
    
    var name = dhtml.getElementById("name").value;
    var sequence = dhtml.getElementById("sequence").value;
    
    var cond_from = dhtml.getElementById("cond_from").value;
    var cond_subject = dhtml.getElementById("cond_subject").value;
    var cond_priority = dhtml.getElementById("cond_priority").value;
    
    var cond_sent_to = dhtml.getElementById("cond_sent_to").value;
    var cond_sent_to_me = dhtml.getElementById("cond_sent_to_me").checked;
    
    if(!action_move && !action_copy && !action_delete && !action_forward && !action_forward_attach && !action_redirect) {
        alert(_("Please select an appropriate action for this rule"));
        return false;
    }
    
    if(action_move && (!action_move_folder.folderentryid || !action_move_folder.storeentryid)) {
        alert(_("Please select a folder to move the item to"));
        return false;
    }
    
    if(action_copy && (!action_copy_folder.folderentryid || !action_copy_folder.storeentryid)) {
        alert(_("Please select a folder to copy the item to"));
        return false;
    }
    
	// check for validity of forwarding email address
	if(action_forward && !action_address) {
		alert(_("Please specify a forwarding address"));
		return false;
	}
	if(action_forward && !parseEmailAddress(action_address)) {
		alert(_("Please specify a forwarding address")+"\n"+_("Please input a valid email address!"));
		return false;
	}

	// check for validity of forwarding as attachment email address
	if(action_forward_attach && !action_address) {
		alert(_("Please specify a forwarding as attachment address"));
		return false;
	}
	if(action_forward_attach && !parseEmailAddress(action_address)) {
		alert(_("Please specify a forwarding as attachment address")+"\n"+_("Please input a valid email address!"));
		return false;
	}

	// check for validity of redirecting email address
	if(action_redirect && !action_address) {
		alert(_("Please specify a redirecting address"));
		return false;
	}
	if(action_redirect && !parseEmailAddress(action_address)) {
		alert(_("Please specify a redirecting address")+"\n"+_("Please input a valid email address!"));
		return false;
	}

    // Create name if none given
    if(!name) {
        var conditions = new Array();
        if(cond_from) 
            conditions.push(_("From") + " " + cond_from);
        if(cond_subject)
            conditions.push(_("Subject") + " " + cond_subject);
        if(cond_priority != -1) {
			cond_priority = parseInt(cond_priority);
            switch(cond_priority) {
                case 0: conditions.push(_("Low priority")); break;
                case 1: conditions.push(_("Normal priority")); break;
                case 2: conditions.push(_("High priority")); break;
            }
        }
        if(cond_sent_to)
            conditions.push(_("Sent to") + " " + cond_sent_to);
        if(cond_sent_to_me)
            conditions.push(_("Sent only to me"));
            
        name = conditions.join(" "+_("and")+" ");
    }
    
    // build rule from dialog settings
    result = new Object;
    
    var conditions = getConditions();
    var action = getAction();
    var condition = new Object();
    
    // If there are multiple conditions, make a big AND condition from them
    if(conditions.length > 1) {
        condition.restype = new Object;
        condition.restype.value = "RES_AND";
        condition.restriction = new Array();
        for(var i=0;i<conditions.length;i++) {
            condition.restriction.push(conditions[i]);
        }
    } else if(conditions.length == 0) {
        // No conditions, so the rule should always match. We use the following condition (RES_EXIST PR_MESSAGE_CLASS).
        condition.restype = new Object;
        condition.restype.value = "RES_EXIST";
        condition.proptag = new Object;
        condition.proptag.value = "PR_MESSAGE_CLASS";
    } else{
        // Otherwise, return just the one condition
        condition = conditions[0];
    }
    
    result.rule_name = new Object;
    result.rule_name.value = name;
    result.rule_actions = new Array(action); // server wants array of actions
    result.rule_condition = condition;
    result.rule_sequence = new Object;
    result.rule_sequence.value = sequence;
    
	result.rule_state = new Object;
    result.rule_state.value = "ST_ENABLED";
	if (dhtml.getElementById("action_delete").checked || dhtml.getElementById("stop_processing_more_rules").checked){
		result.rule_state.value += " | ST_EXIT_LEVEL";
	}

    if(window.resultCallBack(result, window.callBackData) == true)
        window.close();
    else
        window.focus();
}

// Returns an array of all conditions specified by the user
function getConditions()
{
    var cond_from = dhtml.getElementById("cond_from").value;
    var cond_subject = dhtml.getElementById("cond_subject").value;
    var cond_priority = dhtml.getElementById("cond_priority").value;
    
    var cond_sent_to = dhtml.getElementById("cond_sent_to");
    var cond_sent_to_me = dhtml.getElementById("cond_sent_to_me").checked;
    
    var conditions = new Array;
    var condition;
    
    if(cond_from) {
        // From contains <string>
        condition = new Object;
        condition.restype = new Object;
        condition.proptag = new Object;
        condition.fuzzylevel = new Object;
        condition.value = new Object;
        
        condition.restype.value = "RES_CONTENT";
        condition.proptag.value = "PR_SENDER_SEARCH_KEY";
        condition.fuzzylevel.value = "FL_SUBSTRING";
        condition.value.value = bin2hex(cond_from.toUpperCase());
		condition.value.attributes = {proptag: "PR_SENDER_SEARCH_KEY", type: "binary"};
        conditions.push(condition);
    }
    
    if(cond_subject) {
        // Subject contains <string>
        condition = new Object;
        condition.restype = new Object;
        condition.restype.value = "RES_CONTENT";
        condition.proptag = new Object;
        condition.proptag.value = "PR_SUBJECT";
        condition.fuzzylevel = new Object;
        condition.fuzzylevel.value = "FL_SUBSTRING | FL_IGNORECASE";
        condition.value = new Object;
        condition.value.value = cond_subject;
		condition.value.attributes = {proptag: "PR_SUBJECT"};
        conditions.push(condition);
    }
    
    if(cond_priority != -1) {
        condition = new Object;
        condition.restype = new Object;
        condition.restype.value = "RES_PROPERTY";
        condition.proptag = new Object;
        condition.proptag.value = "PR_IMPORTANCE";
        condition.relop = new Object;
        condition.relop.value = "RELOP_EQ";
        condition.value = new Object;
        condition.value.value = cond_priority;
		condition.value.attributes = {proptag: "PR_IMPORTANCE"};
        conditions.push(condition);
    }
    
    if(cond_sent_to.value) {
		var parsed = parseEmailAddress(cond_sent_to.value);
		var addrtype = cond_sent_to.addrtype ? cond_sent_to.addrtype : "SMTP";
		var emailaddr = '';
		if(cond_sent_to.emailaddress) {
			emailaddr = cond_sent_to.emailaddress;
		}
		var smtpaddr = cond_sent_to.smtpaddr ? cond_sent_to.smtpaddr : parsed.emailaddress;
		var name = cond_sent_to.displayname ? cond_sent_to.displayname : (parsed.displayname ? parsed.displayname : parsed.emailaddress);
		var entryid = cond_sent_to.entryid ? cond_sent_to.entryid : createOneOff(name, addrtype, smtpaddr);
		var searchkey = cond_sent_to.searchkey ? cond_sent_to.searchkey : createSearchKey(addrtype, smtpaddr);

        condition = new Object;
        condition.restype = new Object;
        condition.restype.value = "RES_SUBRESTRICTION";
        condition.proptag = new Object;
        condition.proptag.value = "PR_MESSAGE_RECIPIENTS";
        condition.restriction = new Object;
        condition.restriction.restype = new Object;
        condition.restriction.restype.value = "RES_COMMENT";
        condition.restriction.property = new Array;
        
        var property = new Object;
        property.attributes = new Object;
        property.attributes.proptag = "0x60000003";
        property.value = 1;
        condition.restriction.property.push(property);
        
        property = new Object;
        property.attributes = new Object;
        property.attributes.proptag = "0x00010102";
		property.attributes.type = "binary";
        property.value = entryid;
        condition.restriction.property.push(property);
        
        property = new Object;
        property.attributes = new Object;
        property.attributes.proptag = "0x0001001E";
        property.value = cond_sent_to.value;
        condition.restriction.property.push(property);
        
        property = new Object;
        property.attributes = new Object;
        property.attributes.proptag = "PR_DISPLAY_TYPE";
        property.value = 0;
        condition.restriction.property.push(property);
        
        condition.restriction.restriction = new Object;
        condition.restriction.restriction.restype = new Object;
        condition.restriction.restriction.restype.value = "RES_PROPERTY";
        condition.restriction.restriction.relop = new Object;
        condition.restriction.restriction.relop.value = "RELOP_EQ";
        condition.restriction.restriction.proptag = new Object;
        condition.restriction.restriction.proptag.value = "PR_SEARCH_KEY";
        condition.restriction.restriction.value = new Object;
        condition.restriction.restriction.value.value = searchkey;
		condition.restriction.restriction.value.attributes = {type: "binary", proptag: "0x00010102"};

        conditions.push(condition);
        
    }
    
    if(cond_sent_to_me) {
        condition = new Object;
        
        condition.restype = new Object;
        condition.restype.value = "RES_AND";
        condition.restriction = new Array;
        
        var subcondition = new Object;
        subcondition.restype = new Object;
        subcondition.restype.value = "RES_PROPERTY";
        subcondition.relop = new Object;
        subcondition.relop.value = "RELOP_EQ";
        subcondition.proptag = new Object;
        subcondition.proptag.value = "PR_MESSAGE_TO_ME";
		subcondition.value = new Object;
		subcondition.value.value = true;
		subcondition.value.attributes = {proptag: "PR_MESSAGE_TO_ME"};
        condition.restriction.push(subcondition);
        
        subcondition = new Object;
        subcondition.restype = new Object;
        subcondition.restype.value = "RES_NOT";
        
        var subsubcondition = new Object;
        subsubcondition.restype = new Object;
        subsubcondition.restype.value = "RES_CONTENT";
        subsubcondition.fuzzylevel = new Object;
        subsubcondition.fuzzylevel.value = "FL_SUBSTRING";
        subsubcondition.proptag = new Object;
        subsubcondition.proptag.value = "PR_DISPLAY_TO";
        subsubcondition.value = new Object;
        subsubcondition.value.value = ";";
		subsubcondition.value.attributes = {proptag: "PR_DISPLAY_TO"};
        subcondition.restriction = subsubcondition;
        
        condition.restriction.push(subcondition);
        
        subcondition = new Object;
        subcondition.restype = new Object;
        subcondition.restype.value = "RES_PROPERTY";
        subcondition.relop = new Object;
        subcondition.relop.value = "RELOP_EQ";
        subcondition.proptag = new Object;
        subcondition.proptag.value = "PR_DISPLAY_CC";
        subcondition.value = new Object;
        subcondition.value.value = "";								// FIXME: because this value is "empty" the attributes are not send to the server!
		subcondition.value.attributes = {proptag: "PR_DISPLAY_CC"};
        condition.restriction.push(subcondition);
        
        conditions.push(condition);
        
    }
    
    return conditions;
}

function getAction()
{
    var action_move = dhtml.getElementById("action_move").checked;
    var action_copy = dhtml.getElementById("action_copy").checked;
    var action_delete = dhtml.getElementById("action_delete").checked;
    var action_redirect = dhtml.getElementById("action_redirect").checked;
    var action_forward = dhtml.getElementById("action_forward").checked;
    var action_forward_attach = dhtml.getElementById("action_forward_attach").checked;
    
    var action_move_folder = dhtml.getElementById("action_move_folder");
    var action_copy_folder = dhtml.getElementById("action_copy_folder");
	
	if(action_redirect){
		var action_address = dhtml.getElementById("action_redirect_address");
	}else if(action_forward){
		var action_address = dhtml.getElementById("action_forward_address");
	}else if(action_forward_attach){
		var action_address = dhtml.getElementById("action_forward_attach_address");
	}
    
    var action = new Object;
    
    if(action_move) {
        action.action = new Object;
        action.action.value = "OP_MOVE";
        action.folderentryid = new Object;
        action.folderentryid.value = action_move_folder.folderentryid;
        action.storeentryid = new Object;
        action.storeentryid.value = action_move_folder.storeentryid;
        action.foldername = new Object;
        action.foldername.value = html_entity_decode(action_move_folder.innerHTML);
    } else if(action_copy) {
        action.action = new Object;
        action.action.value = "OP_COPY";
        action.folderentryid = new Object;
        action.folderentryid.value = action_copy_folder.folderentryid;
        action.storeentryid = new Object;
        action.storeentryid.value = action_copy_folder.storeentryid;
        action.foldername = new Object;
        action.foldername.value = html_entity_decode(action_copy_folder.innerHTML);
    } else if(action_delete) {
        action.action = new Object;
        action.action.value = "OP_MOVE";
        action.folderentryid = new Object;
        action.folderentryid.value = parentwindow.module.wastebasket;
        action.storeentryid = new Object;
        action.storeentryid.value = parentwindow.module.storeid;
        action.foldername = new Object;
        action.foldername.value = "DELETE";
    } else if(action_forward || action_redirect || action_forward_attach) {

        var parsed = parseEmailAddress(action_address.value);
        var addrtype = action_address.addrtype ? action_address.addrtype : "SMTP";
		var emailaddr = '';
		if(action_address.emailaddress) {
			emailaddr = action_address.emailaddress;
		}
        var smtpaddr = action_address.smtpaddr ? action_address.smtpaddr : parsed.emailaddress;
        var name = action_address.displayname ? action_address.displayname : (parsed.displayname ? parsed.displayname : parsed.emailaddress);
        var entryid = action_address.entryid ? action_address.entryid : createOneOff(name, addrtype, smtpaddr);
		var searchkey = action_address.searchkey ? action_address.searchkey : createSearchKey(addrtype, smtpaddr);

        action.action = new Object;
        action.action.value = "OP_FORWARD";
        action.flavor = new Object;

		if (action_forward){
			action.flavor.value = "0"; 
		} else if (action_forward_attach) {
			action.flavor.value = "FWD_AS_ATTACHMENT";
		} else if (action_redirect) {
			action.flavor.value = "FWD_PRESERVE_SENDER | FWD_DO_NOT_MUNGE_MSG"; // Redirect
		}

        action.adrlist = new Object;
        action.adrlist.address = new Object;
        action.adrlist.address.property = new Array();

        var property = new Object;
        property.attributes = new Object;
        property.attributes.proptag = "PR_RECIPIENT_TYPE";
        property.value = 1;
        action.adrlist.address.property.push(property);
        
        var property = new Object;
        property.attributes = new Object;
        property.attributes.proptag = "PR_DISPLAY_NAME";
        property.value = name;
        action.adrlist.address.property.push(property);

        var property = new Object;
        property.attributes = new Object;
        property.attributes.proptag = "PR_ENTRYID";
        property.attributes.type = "binary";
        property.value = entryid;
        action.adrlist.address.property.push(property);

        var property = new Object;
        property.attributes = new Object;
        property.attributes.proptag = "PR_ADDRTYPE";
        property.value = addrtype;
        action.adrlist.address.property.push(property);

        var property = new Object;
        property.attributes = new Object;
        property.attributes.proptag = "PR_SEARCH_KEY";
        property.attributes.type = "binary";
        property.value = searchkey;
        action.adrlist.address.property.push(property);

        var property = new Object;
        property.attributes = new Object;
        property.attributes.proptag = "PR_DISPLAY_TYPE";
        property.value = 0;
        action.adrlist.address.property.push(property);

        var property = new Object;
        property.attributes = new Object;
        property.attributes.proptag = "PR_OBJECT_TYPE";
        property.value = 6;
        action.adrlist.address.property.push(property);

		if(emailaddr) {
			var property = new Object;
			property.attributes = new Object;
			property.attributes.proptag = "PR_EMAIL_ADDRESS";
			property.value = emailaddr;
			action.adrlist.address.property.push(property);
		}

		var property = new Object;
		property.attributes = new Object;
		property.attributes.proptag = "PR_SMTP_ADDRESS";
		property.value = smtpaddr;
		action.adrlist.address.property.push(property);

		if (addrtype == "SMTP"){
	        var property = new Object;
	        property.attributes = new Object;
	        property.attributes.proptag = "PR_SEND_INTERNET_ENCODING";
	        property.value = 0;
	        action.adrlist.address.property.push(property);

			var property = new Object;
			property.attributes = new Object;
			property.attributes.proptag = "PR_SEND_RICH_INFO";
			property.value = false;
			action.adrlist.address.property.push(property);

			var property = new Object;
	        property.attributes = new Object;
	        property.attributes.proptag = "PR_RECORD_KEY";
		    property.attributes.type = "binary";
			property.value = searchkey;
	        action.adrlist.address.property.push(property);
		}
    }
    
    return action;
}

// Called when user presses one of the addressbook buttons
function getFromAddressBook(targetElementId, type)
{
    var callBackData = new Object;
    callBackData.elementId = targetElementId;

	if (typeof(type)=="undefined")
		type = "fullemail_single";

    webclient.openModalDialog(module, "addressbook", DIALOG_URL+"task=addressbook_modal&type="+type+"&storeid=" + parentwindow.module.storeid, 800, 500, abCallBack, callBackData);
}

// Called when the addressbook is done getting a user
function abCallBack(result, callBackData)
{
    var elementId = callBackData.elementId;
    
    var element = dhtml.getElementById(elementId);
    if(element) {
        element.value = result.value;
        element.entryid = result.entryid; // will only be set for GAB entries
        element.addrtype = result.addrtype;  // only for GAB
        element.displayname = result.displayname; // only for GAB
		element.smtpaddr = result.smtp_address; // only for GAB
		element.emailaddress = result.email_address;
		element.searchkey = result.search_key;
    }
        
    return true;
}

// Called when user presses one of the folders
function selectFolder(elementId)
{
    var entryid = dhtml.getElementById(elementId).folderentryid;
    var callBackData = new Object;
    callBackData.elementId = elementId;
    dhtml.getElementById(elementId.replace("_folder","")).checked = true;
    webclient.openModalDialog(module, "selectfolder", DIALOG_URL+"task=selectfolder_modal&entryid=" + entryid, 300, 300, folderCallBack, callBackData);
}

function folderCallBack(result, callBackData)
{
    var elementId = callBackData.elementId;
    
    var element = dhtml.getElementById(elementId);
    
    element.innerHTML = result.foldername.htmlEntities();
    element.folderentryid = result.folderentryid;
    element.storeentryid = result.storeentryid;
    
    return true;
}

// Create a binary search key in the form "SMTP:user@domain"
function createSearchKey(addrtype, address)
{
    var key = bin2hex(addrtype.toUpperCase() + ":" + address.toUpperCase()) + "00";
    
    return key;
}

// Create a one-off entryid
function createOneOff(displayname, addrtype, emailaddress)
{
    var oneoff = "00000000" + "812B1FA4BEA310199D6E00DD010F5402" + "00000000" + bin2hex(displayname) + "00" + bin2hex(addrtype) + "00" + bin2hex(emailaddress) + "00";
    
    return oneoff;
}

function getEmailAddressFromString()
{
}
function eventActionSelectionChanged(moduleObject, element, event)
{
	if(element && element.id == "action_delete")
		dhtml.getElementById("stop_processing_more_rules").disabled = true;
	else
		dhtml.getElementById("stop_processing_more_rules").disabled = false;
}
