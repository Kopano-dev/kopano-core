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

freebusymodule.prototype = new Module;
freebusymodule.prototype.constructor = freebusymodule;
freebusymodule.superclass = Module.prototype;

function freebusymodule(startTime)
{
	this.daysBefore = FREEBUSY_DAYBEFORE_COUNT;
	this.numberOfDays = FREEBUSY_NUMBEROFDAYS_COUNT;//total days in the view including 'this.daysBefore'
	this.userList = new Object();
	this.userList["all"] = new Object();
	this.userList["all"]["items"] = new Array();
	this.userCount = 0;
	this.zoom = 1;
	this.freezeScrollbar = false;
	this.pickerDisabled = false;
	this.hasResource = false;

	var endDtp = new DateTimePicker(dhtml.getElementById("fbenddate"),_("End time"));
	var startDtp = new DateTimePicker(dhtml.getElementById("fbstartdate"),_("Start time"));
	this.dtp = new combinedDateTimePicker(startDtp,endDtp);
	this.dtp.onchange = this.dtpOnchange;

	if(startTime){
		this.startViewDate = new Date(startTime*1000);
	}
	else{
		this.startViewDate = new Date();
	}

	// Start from this.daysBefore before today
	this.startViewDate.setHours(0);
	this.startViewDate.setMinutes(0);
	this.startViewDate.setSeconds(0);
	this.startViewDate.setDate(this.startViewDate.getDate()-this.daysBefore);

	// End this.numberOfDays after startViewDate
	this.endViewDate = new Date(this.startViewDate.getTime());
	this.endViewDate.setDate(this.endViewDate.getDate()+this.numberOfDays);

	// This flag is set when fb_module is initialized for the first time, so that we can keep the track
	// of wheather we can change the resource recipient when we switch between view while create meetings
	this.autoChangeResourceRecipient = true;
}

freebusymodule.prototype.dtpOnchange = function()
{
	// update the appointment time in appointment tab
	if(typeof module != "undefined") {
		module.setStartTime(fb_module.dtp.getStartValue());
		module.setEndTime(fb_module.dtp.getEndValue());
	}

	fb_module.updatePicker();
}

/**
 * Event handler which is called when all day checkbox in schedule is change
 */
freebusymodule.prototype.onChangeAllDay = function(){
	
	var checkbox_alldayevent = dhtml.getElementById("checkbox_allday");
	
	if(checkbox_alldayevent.checked) {
		this.dtp.startPicker.timeElement.hide();
		this.dtp.endPicker.timeElement.hide();
	}else {
		this.dtp.startPicker.timeElement.show();
		this.dtp.endPicker.timeElement.show();
	}
	
	fb_module.updatePicker();
}

freebusymodule.prototype.init = function(moduleID, element)
{
	this.moduleID = moduleID;	
	this.element = element;
	freebusymodule.superclass.init.call(this, this.moduleID);

	this.nameListElement = dhtml.getElementById("name_list","div",element); 
	this.dayHeaderElements = new Array();
	this.busyElements = new Array();
	
	dhtml.addEvent(this.moduleID,dhtml.getElementById("day_zoom","select",element),"change",fbViewChangeDayZoom);
	dhtml.addEvent(this.moduleID, document, "click", fbClickRecipientTypeBody);
}

function eventFreeBusydModuleContextMenu(moduleObject, element, event)
{
	var items = new Array();
	items.push(webclient.menu.createMenuItem("delete", _("Delete"), _("Delete"), fbViewClickRemoveUser));
	items.push(webclient.menu.createMenuItem("refresh", _("Update"), _("Update"), fbRefreshUser));
	webclient.menu.buildContextMenu(moduleObject.id, element.id, items, event.clientX, event.clientY);
}

freebusymodule.prototype.initView = function(zoom,user,recips)
{
	var hourCol,curViewDate;
	var screenIn = dhtml.getElementById("screen_in");

	screenIn.style.width = (this.numberOfDays*25*24/zoom)+"px";

	if(!zoom){
		this.zoom = 1;
	}
	this.zoom = (zoom*1);
	
	//remove current zoom
	dhtml.deleteAllChildren(screenIn);
	
	//create picker
	var pickLeft = dhtml.addElement(screenIn,"div","","picker_left");
	var pick = dhtml.addElement(screenIn,"div","","picker");
	var pickRight = dhtml.addElement(screenIn,"div","","picker_right");

	//make day headers
	for(var i=0;i<this.numberOfDays;i++){
		curViewDate = new Date(this.startViewDate.getTime());
		curViewDate.setDate(curViewDate.getDate()+i);
		var textToDisplay = DAYS_SHORT[curViewDate.getDay()]+" "
			+curViewDate.getDate()+" "
			+MONTHS_SHORT[curViewDate.getMonth()]+" "
			+curViewDate.getFullYear();
		var dayHeaderElement = dhtml.addElement(screenIn,"div","day_header","",textToDisplay);
		
		dayHeaderElement.style.width = ((600 / this.zoom) - 1) + "px";
		
		// Remember day header elements for later use
		this.dayHeaderElements.push(dayHeaderElement);
	}
	
	//make hour headers
	for(var i=0;i<this.numberOfDays;i++) {
		dhtml.addElement(screenIn,"div","hour_header_" + this.zoom);
	}

	//make grid
	this.grid = dhtml.addElement(screenIn,"div","hour_grid");
	this.grid.style.width = (25 * this.numberOfDays * 24 / this.zoom) + "px";

	dhtml.addEvent(this.moduleID, this.grid, "click", eventFreebusyPickHour);

	this.gridCoors = dhtml.getElementTopLeft(this.grid);

	this.endViewDate = new Date(this.startViewDate.getTime()+(ONE_DAY*this.numberOfDays));

	//events for "add name"
	dhtml.addEvent(this.moduleID,dhtml.getElementById("new_name","input",this.element),"focus",fbViewFocusAddNameInputBox);
	dhtml.addEvent(this.moduleID,dhtml.getElementById("new_name","input",this.element),"blur",fbViewBlurAddNameInputBox);
	dhtml.addEvent(this.moduleID,dhtml.getElementById("new_name","input",this.element),"keydown",fbViewKeyDownAddNameInputBox);

	//update freebusyinfo
	for(var tmpUser in this.userList){
		this.updateBusyInfo(tmpUser);
	}

	//add user to the view
	if(user){
		this.user = user;

		var newUser = new Array();
		newUser[0] = new Object();
		newUser[0]["resolvename"] = this.user;
		newUser[0]["clientuser"] = "c_user";
		newUser[0]["recipienttype"] = MAPI_ORIG;
		newUser[0]["recipient_flags"] = recipSendable | recipOrganizer;
		// itemProps are not set if the new meeting request is created.
		if(module.itemProps != undefined && module.itemProps["sent_representing_email_address"]){
			newUser[0]["emailaddress"] = module.itemProps["sent_representing_email_address"];
		}else{
			newUser[0]["emailaddress"] = "";
		}
		
		for(i=0;i<recips.length;i++) {
			if(i==0){
				continue;
			}
			user = new Array();
			// if any user is organiser then not add him in userList as he is already added as c_user.
			if (((parseInt(recips[i]["recipient_flags"],10) & recipOrganizer) != recipOrganizer)){
				user['resolvename'] = recips[i]['fullname'];
				user['recipienttype'] = recips[i]['recipienttype'];
				user['recipient_flags'] = recips[i]['recipient_flags'];
				user['emailaddress'] = recips[i]['emailaddress'];
				newUser.push(user);
			}
		}
		
		this.createUsers(newUser);
	}

	this.resize();
	this.updatePicker();
}

/**
 * Function will resize the view (called when dialog is resized)
 */
freebusymodule.prototype.resize = function()
{
	var screenIn = dhtml.getElementById("screen_in","div",this.element);
	var screenOut = dhtml.getElementById("screen_out","div",this.element);

	var newOutSideWidth = dhtml.getElementById("freebusy_container").offsetWidth-dhtml.getElementById("fbleft").offsetWidth;
	// If scrollbars are needed subtract the width of a scrollbar from the width of the freebusy overview.
	if(dhtml.getElementById("freebusy_container").offsetHeight < dhtml.getElementById("screen_out").offsetHeight + dhtml.getElementById("freebusy_bottom_container").offsetHeight){
		newOutSideWidth = newOutSideWidth - 18;
	}
	screenOut.style.width = (newOutSideWidth>500?newOutSideWidth:500)+"px";
}

/**
 * Function which resizes the grid and header elements (called when view is changed)
 */
freebusymodule.prototype.resizeGrid = function() 
{
	var screenOut = dhtml.getElementById("screen_out","div",this.element);

	//day headers
	var dayHeaderElements = this.dayHeaderElements;
	for(var i=0; i<dayHeaderElements.length; i++){
		dayHeaderElements[i].style.width = Math.floor(599/this.zoom)+"px";
	}

	//update grid height
	this.grid.style.height = (19*(getSizeOfObject(this.userList)+1))+"px";

	//update scrollwindow height
	screenOut.style.height = 31+(getSizeOfObject(this.userList)*19)+36+"px";
	
}

/**
 * Function will add the busyinfo to the userList
 */
freebusymodule.prototype.addBusyInfo = function(inputUserList)
{
	for(var i=0; i < inputUserList.length; i++){
		var clientuser =  inputUserList[i].getAttribute("clientuser");

		if(typeof this.userList[clientuser] != "undefined"){
			this.userList[clientuser]["objecttype"] = parseInt(dhtml.getXMLValue(inputUserList[i], "objecttype"), 10);
			// Check whether perticular userinfo is there or not.
			this.userList[clientuser]["fullname"] =     dhtml.getTextNode(inputUserList[i].getElementsByTagName("fullname")[0],"");
			this.userList[clientuser]["entryid"] =      dhtml.getTextNode(inputUserList[i].getElementsByTagName("entryid")[0],"");
			this.userList[clientuser]["username"] =     dhtml.getTextNode(inputUserList[i].getElementsByTagName("username")[0],"");
			this.userList[clientuser]["emailaddress"] = dhtml.getTextNode(inputUserList[i].getElementsByTagName("email")[0], this.userList[clientuser]["emailaddress"]);
			//set this when we are adding user for the first time
			if(!this.autoChangeResourceRecipient){
				if(typeof this.userList[clientuser] != "undefined" && typeof this.userList[clientuser]["recipienttype"] == "undefined") {
					this.userList[clientuser]["recipienttype"] = parseInt(dhtml.getXMLValue(inputUserList[i], "recipienttype"), 10);
				}
			}else{//validate recipient from server data
				if(typeof this.userList[clientuser] != "undefined" &&  this.userList[clientuser]["objecttype"] && typeof (dhtml.getXMLValue(inputUserList[i], "recipienttype")) != "undefined") {
					this.userList[clientuser]["recipienttype"] = parseInt(dhtml.getXMLValue(inputUserList[i], "recipienttype"), 10);
				}else{
					this.userList[clientuser]["recipienttype"] = MAPI_TO;
				}
			}

			//put the items in the "userList"
			this.userList[clientuser]["items"] = new Array();
			var busyElement = inputUserList[i].getElementsByTagName("item");

			for(var j=0;j<busyElement.length;j++){
				var item = new Object();
				item["status"] = parseInt(busyElement[j].getElementsByTagName("status")[0].firstChild.nodeValue);
				item["start"] = busyElement[j].getElementsByTagName("start")[0].getAttribute("unixtime");
				item["end"] = busyElement[j].getElementsByTagName("end")[0].getAttribute("unixtime");
				this.userList[clientuser]["items"][this.userList[clientuser]["items"].length] = item;
			}
			this.updateBusyInfo(clientuser);
		}
	}
}

/**
 * Function will create a user
 * @param inputList = Array(Object()) "see below"
 *    [0]["resolvename"]
 *    [0]["clientuser"]
 */
freebusymodule.prototype.createUsers = function(inputList)
{
	for(i in inputList){
		if(!inputList[i]["clientuser"]){
			var clientuser = "user_"+this.userCount;
			this.userCount++;
		}
		else{
			var clientuser = inputList[i]["clientuser"];
		}
		
		//add user to "userList"
		this.userList[clientuser] = new Object();
		this.userList[clientuser]["fullname"] = inputList[i]["resolvename"];
		this.userList[clientuser]["resolvename"] = inputList[i]["resolvename"];
		this.userList[clientuser]["entryid"] = "";
		this.userList[clientuser]["username"] = "";
		this.userList[clientuser]["emailaddress"] = inputList[i]["emailaddress"];
		this.userList[clientuser]["recipienttype"] = inputList[i]["recipienttype"];
		this.userList[clientuser]["recipient_flags"] = inputList[i]["recipient_flags"];
		this.userList[clientuser]["objecttype"] = inputList[i]["objecttype"];
		this.userList[clientuser]["items"] = new Array();
	
		//send request
		var data = new Object();
		data["clientuser"] = clientuser;
		data["username"] = inputList[i]["resolvename"];
		data["emailaddress"] = inputList[i]["emailaddress"];
		data["start"] = Math.floor(this.startViewDate.getTime()/1000);
		data["end"] = Math.floor(this.endViewDate.getTime()/1000);
		webclient.xmlrequest.addData(this, "add", data);

	}

	// Get emailaddress of opened folder-object, to check organizer status
	var storeObject = parentWebclient.hierarchy.getStore(module.storeid);
	var storeEmail = webclient.emailaddress.toLowerCase();
	if(storeObject && storeObject.emailaddress)
		storeEmail = storeObject.emailaddress.toLowerCase();

	// create meeting request (module is the global appointment module)
	if(this.userCount == 0 || (this.userList["c_user"]["emailaddress"] == "") || (this.userList["c_user"]["emailaddress"].toLowerCase() == storeEmail)){
		meetingRequestSetup(mrSetupOrganiser)
	}else{
		meetingRequestSetup(mrSetupAttendee);
	}

	module.setUserList(this.getUserList());
	webclient.xmlrequest.sendRequest(false);
}

/**
 * Function will remove an user
 * @param inputList = Array() "see below"
 *   [0] = "user_1"   
 */ 
freebusymodule.prototype.removeUsers = function (inputList){
	for(i in inputList){
		clientuser = inputList[i];
		if(this.userList[clientuser]){
			//remove freebusy of "clientuser"
			this.userList[clientuser]["items"] = new Array();
			this.updateBusyInfo(clientuser);
		
			delete this.userList[clientuser];			
			
			// remove meeting request when there are no users left
			var fbUserlist = this.getUserList();
			if(fbUserlist.length==0){
				meetingRequestSetup(mrSetupNormal);
			}
		}
	}
	//update overview
	this.updateBusyInfoAllAtt();
}

/**
 * Function will clear allusers in the userlist
 */
freebusymodule.prototype.clearUserList = function()
{
	for(user in this.userList){
		if(user != "all" && user != "c_user"){
			delete this.userList[user];
			this.busyElements[user] = new Array();
		}
	}
	this.updateUserListView();
	this.updateGrid();
}

/**
 * Function will load all freebusy data
 */ 
freebusymodule.prototype.execute = function(type, action)
{
	var userElement = action.getElementsByTagName("user");
	if(type == "add"){
		this.addBusyInfo(userElement);
		this.updateUserListView();
		this.updateBusyInfoAllAtt();
		this.resizeGrid();
	}
}

/**
 * Function will compose all busyinfo to all attendees line
 */ 
freebusymodule.prototype.updateBusyInfoAllAtt = function()
{
	var entryid = "all";
	var username = "all";
	var widthOneItem = 24;	
	var screenIn = dhtml.getElementById("screen_in","div",this.element);
	var firstElement = this.startViewDate.getTime()/1000;

	//empty/create
	this.userList["all"] = new Object();
	this.userList["all"]["fullname"] = _("All Attendees");
	this.userList["all"]["username"] = "all";
	this.userList["all"]["emailaddress"] = "";
	this.userList["all"]["items"] = new Array();
	
	var ts = new Array();
	var i = 0;
	for(var userId in this.userList){
		var itemsUser = this.userList[userId]["items"];
		for(var itemNr in itemsUser){
			if(itemsUser[itemNr]["status"] != " -1"){
				ts[i] = new Object();
				ts[i]["type"] = "start";
				ts[i]["time"] = itemsUser[itemNr]["start"];
				i++;
				ts[i] = new Object();
				ts[i]["type"] = "end";
				ts[i]["time"] = itemsUser[itemNr]["end"];			
				i++;
			}
		}
	}
	
	ts.sort(this.sortBusyInfo);

	var laststart=0
	var level=0;
	var newItemList = this.userList["all"]["items"];
	i=0;
	for(line in ts){
		switch (ts[line]["type"]){
			case "start":
				if(level == 0){
					newItemList[i] = new Object();
					newItemList[i]["status"] = fbBusy;
					newItemList[i]["start"] = ts[line]["time"];
				}
				level++;
				break;
			case "end":
				level--;
				if(level == 0){					
					newItemList[i]["end"] = ts[line]["time"];
					i++;
				}
				break;
		}
	}

	this.updateBusyInfo("all");
}

/**
 * Function will sort this.userList[username]["items"]
 */ 
freebusymodule.prototype.sortBusyInfo = function(a,b)
{
	return a["time"] - b["time"];
}

/**
 * Function will add/remove/update users in the list on the left 
 */ 
freebusymodule.prototype.updateUserListView = function()
{
	var currentElement = this.nameListElement.getElementsByTagName("div");
	
	var i=0;
	for(var userid in this.userList){
		if(userid != "all"){
			var fullname = this.userList[userid]["fullname"];
			var nameElement = false;
			if(currentElement[i]){
				//update item
				if(currentElement[i].firstChild.nodeValue != fullname){
					currentElement[i].setAttribute("clientuser",userid);
					dhtml.deleteAllChildren(currentElement[i]);
					nameElement = currentElement[i];
				}
			} else {
				//create item
				var newNameElement = dhtml.addElement(this.nameListElement,"div","name_list_item")//,"namelist_"+i,fullname); @FIXME
				newNameElement.setAttribute("clientuser",userid);
				nameElement = newNameElement;
			}

			if(nameElement !== false) {
				var recipType = dhtml.addElement(nameElement, "span", "icon", "" , "");
				switch(this.userList[userid].recipienttype){
					case MAPI_ORIG:
						dhtml.addClassName(recipType, "icon_meetingrequest_organizer");
						break;
					case MAPI_TO:
						dhtml.addClassName(recipType, "icon_meetingrequest_requiredattendee");
						break;
					case MAPI_CC:
						dhtml.addClassName(recipType, "icon_meetingrequest_optionalattendee");
						break;
					case MAPI_BCC:
						dhtml.addClassName(recipType, "icon_meetingrequest_resource");
						break;
				}

				// Check if the user is not the organizer of the meetingrequest
				if(userid != "c_user") {
					var dropdownRecipType = dhtml.addElement(recipType, "span", "icon meetingrequest_dropdownarrow", "", NBSP);
					recipType.userid = userid;
					dhtml.addEvent(this, recipType, "click", fbClickRecipientTypeDropdown);

					var delUserElement = dhtml.addElement(nameElement,"span","icon icon_delete","",NBSP);
					dhtml.addEvent(this.moduleID,delUserElement,"click",fbViewClickRemoveUser);
				}

				dhtml.addElement(nameElement,"span","name_list_item_fullname","",fullname);
				this.updateBusyInfo(userid);
			}

			i++;
		}
	}

	//remove items
	for(;i<currentElement.length;i++){
		dhtml.deleteElement(currentElement[i]);
	}
	//update the user list values in to/cc/bcc & toccbcc feild in appointment dialog
	module.setUserList(this.getUserList());
	this.resizeGrid();
}

/**
 * Function will remove and add freebusy information for "username"
 * with the information for "this.userList"
 * @param username  "full username" 
 */
freebusymodule.prototype.updateBusyInfo = function(clientuser)
{
	var widthOneItem = 24;
	var itemsList = this.userList[clientuser].items;
	var screenIn = dhtml.getElementById("screen_in","div",this.element);

	//remove old busyinfo
	if(this.busyElements[clientuser]) {
		for(var i=0;i<this.busyElements[clientuser].length;i++){
			dhtml.deleteElement(this.busyElements[clientuser][i]);
		}
	}
	// Clear the Array containing the busy elements for this user
	this.busyElements[clientuser] = new Array();
	
	//add new busyinfo
	for(var i=0;i<itemsList.length;i++){
		var type = itemsList[i]["status"];
		var startUnixTime = itemsList[i]["start"];
		var dueUnixTime = itemsList[i]["end"];
		
		var className = "busy_item ";
		switch(type){
			case fbFree:
				className = className+"fb_free";
				break;
			case fbTentative:
				className = className+"fb_tentative";
				break;
			case fbBusy:
				className = className+"fb_busy";
				break;
			case fbOutOfOffice:
				className = className+"fb_outoffice";
				break;
			case -1:
			default:			
				className = className+"fb_noinfo";
				break;
		}
		var newBusyElement = dhtml.addElement(screenIn, "div", className);
		newBusyElement.setAttribute("clientuser",clientuser);
		secondsToStart = this.getDSTCorrectedDiff(this.startViewDate, new Date(startUnixTime*1000));
		secondsToEnd = this.getDSTCorrectedDiff(this.startViewDate, new Date(dueUnixTime*1000));
		newBusyElement.style.left = (25*(secondsToStart/(ONE_HOUR/1000)))/this.zoom+"px";
		newBusyElement.style.top = 31+(this.getPositionUserList(clientuser)*19)+3+"px";
		newBusyElement.style.width = (25*((secondsToEnd-secondsToStart)/(ONE_HOUR/1000)))/this.zoom+"px";
		this.busyElements[clientuser].push(newBusyElement);
	}
}

/**
 * Get the DST corrected difference between start and end in seconds
 *
 * This is used for displaying times with a constant scale (so the scale doesn't
 * show any DST jumps). This function outputs the number of seconds that there would 
 * be between start and end if there were no DST jumps at all.
 * @param Date start The start date
 * @param Date end The end date
 * @return Number The number of seconds without DST jumps
 */
freebusymodule.prototype.getDSTCorrectedDiff = function(start, end)
{
	var actualSeconds = (end.getTime() - start.getTime())/1000;
	var bias = end.getTimezoneOffset() * 60 - start.getTimezoneOffset() * 60;
	
	return actualSeconds - bias;
}

/**
 * Function will look for a posible meeting point
 */ 
freebusymodule.prototype.findNextPickPoint = function()
{
	var itemList = this.userList["all"]["items"];
	var startTime = this.getStartMeetingTime();
	var endTime = this.getEndMeetingTime();
	var duration = this.getEndMeetingTime()-this.getStartMeetingTime();

	var result=false;
	while(result==false){
		startTime = (startTime+1800);//1800 = HALF_HOUR
		endTime = (startTime+duration);
		if(itemList.length == 0){
			result=true;
		}
		for(var i=0; i<itemList.length; i++){
			if(itemList[i-1]){
				var tmpStart = itemList[i-1]["end"];
				var tmpEnd = itemList[i]["start"];
				
				if(duration<=(tmpEnd-tmpStart) &&
				startTime>=tmpStart &&
				endTime<=tmpEnd ){
					result = true;
				}
			}
			else{
				//first busyinfo
				if(endTime <= itemList[i]["start"]){
					//if endTime before first busyinfo
					result = true;
				}
			}
			//last busyinfo
			if(i==(itemList.length-1) && result==false && startTime>=(itemList[i]["end"])){
				//if startTime afster last busyinfo
				result = true;				
			}
		}
	}
	this.setStartMeetingTime(startTime);
	this.setEndMeetingTime(endTime);

	// update the appointment time in appointment tab
	if(typeof module != "undefined") {
		module.setStartTime(startTime);
		module.setEndTime(endTime);
	}

	this.updatePicker();
}

/**
 * Function will go back to a posible meeting point
 */
freebusymodule.prototype.findPrevPickPoint = function()
{
	var itemList = this.userList["all"]["items"];
	var startTime = this.getStartMeetingTime();
	var endTime = this.getEndMeetingTime();
	var duration = this.getEndMeetingTime()-this.getStartMeetingTime();

	var result=false;
	while(result==false){
		startTime = (startTime-1800);//1800 = HALF_HOUR
		endTime = (startTime+duration);
		if(itemList.length == 0){
			result=true;
		}
		for(var i=0; i<itemList.length; i++){
			if(itemList[i-1]){
				var tmpStart = itemList[i-1]["end"];
				var tmpEnd = itemList[i]["start"];
				
				if(duration<=(tmpEnd-tmpStart) &&
				startTime>=tmpStart &&
				endTime<=tmpEnd ){
					result = true;
				}
			}
			else{
				//first busyinfo
				if(endTime <= itemList[i]["start"]){
					//if endTime before first busyinfo
					result = true;
				}
			}
			//last busyinfo
			if(i==(itemList.length-1) && result==false && startTime>=(itemList[i]["end"])){
				//if startTime afster last busyinfo
				result = true;				
			}
		}
	}
	this.setStartMeetingTime(startTime);
	this.setEndMeetingTime(endTime);

	// update the appointment time in appointment tab
	if(typeof module != "undefined") {
		module.setStartTime(startTime);
		module.setEndTime(endTime);
	}

	this.updatePicker();
}

/**
 * Enable the displaying of the picker and the changing of the picker. 
 * Also the picker will be unhidden.
 */ 
freebusymodule.prototype.enablePicker = function(){
	this.pickerDisabled = false;
	var pickerLeft = dhtml.getElementById("picker_left","div",this.element);
	var picker = dhtml.getElementById("picker","div",this.element);
	var pickerRight = dhtml.getElementById("picker_right","div",this.element);
	pickerLeft.style.display = "";
	picker.style.display = "";
	pickerRight.style.display = "";
}

/**
 * Disable the displaying of the picker and the changing of the picker. 
 * Also the picker will be hidden.
 */ 
freebusymodule.prototype.disablePicker = function(){
	this.pickerDisabled = true;
	var pickerLeft = dhtml.getElementById("picker_left","div",this.element);
	var picker = dhtml.getElementById("picker","div",this.element);
	var pickerRight = dhtml.getElementById("picker_right","div",this.element);
	pickerLeft.style.display = "none";
	picker.style.display = "none";
	pickerRight.style.display = "none";
}

/**
 * Funtion will move the "picker" and move the scrollbar 
 */ 
freebusymodule.prototype.updatePicker = function()
{
	var screenIn = dhtml.getElementById("screen_in","div",this.element);
	var screenOut = dhtml.getElementById("screen_out","div",this.element);
	var pickerLeft = dhtml.getElementById("picker_left","div",this.element);
	var picker = dhtml.getElementById("picker","div",this.element);
	var pickerRight = dhtml.getElementById("picker_right","div",this.element);
	
	var startUnixTime = this.dtp.getStartValue(); 

	var checkbox_alldayevent = dhtml.getElementById("checkbox_allday");
	//if appointment is an allday event then add one day in endtime 
	if(checkbox_alldayevent.checked) {
		var enddate = new Date(this.dtp.getEndValue()*1000);
		enddate.addDays(1);
		var dueUnixTime = enddate.getTime()/1000;
	}else{
		var dueUnixTime = this.dtp.getEndValue(); 
	}

	var tmpWidth,tmpLeft,tmpRight,secondsDiff;
	
	// Calculate the nubmer of pixels of the left edge from the start of the timeline
	secondsDiff = this.getDSTCorrectedDiff(this.startViewDate, new Date(startUnixTime*1000) );
	tmpLeft = (25*(( secondsDiff )/(ONE_HOUR/1000)))/this.zoom;

	// Calculate the width by first calculating the number of pixels to the right edge from the start of the timeline
	secondsDiff = this.getDSTCorrectedDiff(this.startViewDate, new Date(dueUnixTime*1000) );
	tmpRight = (25*(( secondsDiff )/(ONE_HOUR/1000)))/this.zoom;
	tmpWidth = tmpRight - tmpLeft;
	
	//start time out of view leftside
	if(tmpLeft<0){
		tmpWidth = tmpWidth+tmpLeft;
		tmpLeft=0;
	}
	//end time out of view leftside
	if(tmpWidth<0){
		tmpWidth=0;
	}
	
	//start time out of view rightside
	if(tmpLeft>screenIn.offsetWidth){
		tmpLeft = screenIn.offsetWidth-4;
		tmpWidth = 0;
	}
	
	//end time out of view leftside
	if((tmpLeft+tmpWidth)>screenIn.offsetWidth){
		tmpWidth = screenIn.offsetWidth-tmpLeft;
	}

	if(!this.freezeScrollbar){
		//move scrollbar to the right position
		var tmpScrollStart = (screenOut.offsetWidth-tmpWidth)/2;
		if(tmpScrollStart<0){
			tmpScrollStart = 0;
		}
		screenOut.scrollLeft = tmpLeft-tmpScrollStart; 
	}

	picker.style.left = tmpLeft+"px";
	picker.style.width = tmpWidth+"px";
	picker.style.height = (19*(1+getSizeOfObject(this.userList)))+"px";
	
	pickerLeft.style.left = tmpLeft+"px";
	pickerLeft.style.width = 4+"px";
	pickerLeft.style.height = (19*(1+getSizeOfObject(this.userList)))+"px";
	
	pickerRight.style.left = (tmpLeft+tmpWidth)-4+"px";
	pickerRight.style.width = 4+"px";
	pickerRight.style.height = (19*(1+getSizeOfObject(this.userList)))+"px";
}

/**
 * Function will return the position number of the username in the array "this.userList"
 * @param usernamer fullusername
 */  
freebusymodule.prototype.getPositionUserList = function(clientuser)
{
	var result = false;
	var i = 0;

	for(var item in this.userList){		
		if(item==clientuser){
			result = i;
		}
		i++;
	}
	return result;
}

/**
 * User this function to change the StartMeetingTime
 * @param unixtime
 */
freebusymodule.prototype.setStartMeetingTime = function(unixtime)
{
	if(this.dtp.getStartValue() != unixtime)
		this.dtp.setStartValue(unixtime);
}

/**
 * User this function to change the EndMeetingTime
 * @param unixtime
 */
freebusymodule.prototype.setEndMeetingTime = function(unixtime)
{
	if(this.dtp.getEndValue() != unixtime)
		this.dtp.setEndValue(unixtime);
}

/**
 * Function to change the the allday event checkbox in schedule tab
 * @param boolean allDayEvent Flag
 */
freebusymodule.prototype.setAllDayEvent = function(allDayEventFlag)
{
	if(allDayEventFlag){
		this.dtp.startPicker.timeElement.hide();
		this.dtp.endPicker.timeElement.hide();
	}else{
		this.dtp.startPicker.timeElement.show();
		this.dtp.endPicker.timeElement.show();
	}

	dhtml.getElementById("checkbox_allday").checked = allDayEventFlag;
}
/**
 * User this function to get the StartMeetingTime
 * @return int unixtime
 */
freebusymodule.prototype.getStartMeetingTime = function()
{
	return this.dtp.getStartValue();
}

/**
 * User this function to get the EndMeetingTime
 * @return int unixtime
 */
freebusymodule.prototype.getEndMeetingTime = function()
{
	return this.dtp.getEndValue();
}

/**
 * Function which will return the all day status of an appointment
 */
freebusymodule.prototype.isAllDayEvent = function()
{
	return dhtml.getElementById("checkbox_allday").checked;
}

/**
 * Use this function to get the resource
 * @return string location
 */
freebusymodule.prototype.getResource = function()
{	
	for(var userid in this.userList){
		if(this.userList[userid].recipienttype == MAPI_BCC){
			this.hasResource = true;
			return this.userList[userid].fullname;
		}
	}
}

/**
 * Function will return the selected users
 * @return Array[]->entryid
 *                ->fullname
 *                ->emailaddress
 */ 
freebusymodule.prototype.getUserList = function()
{
	var result = new Array();
	
	var index = 0;
	for(user in this.userList){
		if(user != "all" && user != "c_user"){
			result[index] = new Object();
			result[index]["entryid"] =  this.userList[user]["entryid"];
			result[index]["fullname"] =  this.userList[user]["fullname"];
			result[index]["emailaddress"] =  this.userList[user]["emailaddress"];
			result[index]["recipienttype"] = this.userList[user]["recipienttype"];
			result[index]["recipient_flags"] = this.userList[user]["recipient_flags"];
			result[index]["objecttype"] = this.userList[user]["objecttype"];
			index++;
		}else if(user == "c_user"){
			result[index] = new Object();
			result[index]["fullname"] =  this.userList[user]["fullname"];
			result[index]["emailaddress"] =  this.userList[user]["emailaddress"];
			result[index]["recipienttype"] = MAPI_ORIG;
			result[index]["entryid"] = "";
			result[index]["recipient_flags"] = this.userList[user]["recipient_flags"];
			result[index]["objecttype"] = this.userList[user]["objecttype"];
			index++;
		}
	}
	return result;
}

/**
 * Function will create the users in this given list
 * @param  Object[]->entryid
 *                 ->fullname
 *                 ->emailaddress 
 */ 
freebusymodule.prototype.setUserList = function(inputUserlist)
{
	//remove old
	var i=0;
	var removeList = new Array();
	
	for(oldUser in this.userList){
		for(newUser in inputUserlist){
			//check if user "this.userList" exists in the inputUserlist
			if((this.userList[oldUser]["fullname"] != inputUserlist[newUser]["fullname"] ||
			 this.userList[oldUser]["emailaddress"] != inputUserlist[newUser]["emailaddress"] ||
			 this.userList[oldUser]["fullname"] != inputUserlist[newUser]["resolvename"]) &&
			 (oldUser != "all" && oldUser != "c_user")){
				removeList[i] = oldUser;//put user on the remove list
				i++;
				break;	// break inner loop to avoid duplicates
			}
		}
	}
	this.removeUsers(removeList);
	
	//create new
	var newUserList = new Array();
	var i=0;
	for(userInput in inputUserlist){
		/**
		 * Check to see that the user is not the owner (It is still possible to 
		 * have the owner added twice (once as owner and once as normal 
		 * recipient)
		 */
		if(inputUserlist[userInput]["recipient_flags"] != 3){
			if(inputUserlist[userInput]["fullname"] != undefined){
				newUserList[i] = new Object();
				newUserList[i]["resolvename"] = inputUserlist[userInput]["fullname"];
				newUserList[i]["recipienttype"] = inputUserlist[userInput]["recipienttype"];
				newUserList[i]["emailaddress"] = inputUserlist[userInput]["emailaddress"];
				newUserList[i]["objecttype"] = inputUserlist[userInput]["objecttype"];
				i++;
			}
		}
	}
	this.createUsers(newUserList);
}

function eventFreebusyPickHour(moduleObject, element, event)
{
	if(!moduleObject.pickerDisabled){
		var screenOut = dhtml.getElementById("screen_out");
		var screenOutCoors = dhtml.getElementTopLeft(screenOut);
		
		// xrel is relative to the scrollable area
		var xrel = event.clientX - screenOutCoors[0];
		// compensate for scroller
		xrel += screenOut.scrollLeft;
		
		// xrel is now pixel offset into grid
		
		fb_module.freezeScrollbar = true;
		var duration = (fb_module.getEndMeetingTime() - fb_module.getStartMeetingTime());

		var startTimeline = new Date(fb_module.startViewDate.getTime());
		var hoursSinceStartTimeline = xrel/25 * fb_module.zoom;
		// If hoursSinceStartTimeline is higher than 24 it will go to the next day(s)
		startTimeline.setHours( Math.floor(hoursSinceStartTimeline) );
		// Use modulo to get the remainder of the hour and multiply it by 60 to get the minutes
		// clicktime is the exact moment of clicking
		var clicktime = startTimeline.setMinutes( (hoursSinceStartTimeline % 1) * 60 ) / 1000;

		var start = Math.floor(clicktime/(30*60))*30*60;
		fb_module.setStartMeetingTime(start);
		fb_module.setEndMeetingTime(start + duration);
		fb_module.updatePicker();
		fb_module.freezeScrollbar = false;

		// update the appointment time in appointment tab
		if(typeof module != "undefined") {
			module.setStartTime(start);
			module.setEndTime(start + duration);
		}
	}
}

function fbViewFocusAddNameInputBox(moduleObject, element, event)
{
	textBoxValue = dhtml.getElementById("new_name","input",this.element).value;
	dhtml.getElementById("new_name","input",this.element).className = "seleted";
	if(textBoxValue == _("Add a name")){
		dhtml.getElementById("new_name","input",this.element).value = "";
	}
}

function fbViewBlurAddNameInputBox(moduleObject, element, event)
{
	textBoxValue = dhtml.getElementById("new_name","input",this.element).value;
	dhtml.getElementById("new_name","input",this.element).className = "idle";
	if(textBoxValue.trim() == ""){
		dhtml.getElementById("new_name","input",this.element).value = _("Add a name");
	}
}

function fbViewKeyDownAddNameInputBox(moduleObject, element, event)
{
	if(event.keyCode == 13 || event.keyCode == 9){
		if (event.keyCode == 9 && element.value.trim() != "") event.preventDefault();
	
		var newUser = new Array();
		newUser[0] = new Object();
		newUser[0]["resolvename"] = dhtml.getElementById("new_name","input",this.element).value;
		moduleObject.createUsers(newUser);
		
		dhtml.getElementById("new_name","input",this.element).value = "";
		moduleObject.updateUserListView();
		moduleObject.resizeGrid();
		moduleObject.updatePicker();
		moduleObject.resize();
	}
}

/**
 * Function will refresh selected user
 */
function fbRefreshUser(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	element = element.parentNode;
	
	var newUser = new Array();
	newUser[0] = new Object();
	newUser[0]["resolvename"] = moduleObject.userList[clientuser]["username"];
	newUser[0]["clientuser"] = element.getAttribute("clientuser");
	moduleObject.createUsers(newUser);
	
	moduleObject.updateUserListView();
}

/**
 * Function will remove selected user
 */
function fbViewClickRemoveUser(moduleObject, element, event)
{
	element = element.parentNode;
	moduleObject.removeUsers(new Array(element.getAttribute("clientuser")));
	moduleObject.updateUserListView();
	moduleObject.updatePicker();
	moduleObject.resize();
}

/**
 * Function will change the view for example 1day in scroll of 3days etc.
 */
function fbViewChangeDayZoom(moduleObject, element, event)
{
	moduleObject.initView(element.value);
}


/**
 * Function will open the Recipient Type selection list
 */
function fbClickRecipientTypeDropdown(moduleObject, element, event)
{
	var selectionlist = dhtml.getElementById("meetingrequest_recipienttype_selectionlist")
	if(selectionlist){
		selectionlist.parentNode.removeChild(selectionlist);
	}

	var selectionlist = dhtml.addElement(document.getElementsByTagName("body")[0], "div", "meetingrequest_recipienttype_selectionlist", "meetingrequest_recipienttype_selectionlist");
	selectionlist.userid = element.userid;
	selectionlist.icon_element = element;

	var elemPos = dhtml.getElementTopLeft(element);
	selectionlist.style.left = elemPos[0] + "px";
	selectionlist.style.top = (elemPos[1] + Number(element.offsetHeight)) + "px";

	var ul = dhtml.addElement(selectionlist, "ul");
	ul.container = selectionlist;
	//dhtml.addEvent(moduleObject, ul, "mouseout", fbMouseOutRecipientTypeDropDownList);

	var liReqAtt = dhtml.addElement(ul, "li", "icon icon_meetingrequest_requiredattendee", "", _("Required Attendee"));
	liReqAtt.recipientType = 1;
	var liOptAtt = dhtml.addElement(ul, "li", "icon icon_meetingrequest_optionalattendee", "", _("Optional Attendee"));
	liOptAtt.recipientType = 2;
	var liRes =    dhtml.addElement(ul, "li", "icon icon_meetingrequest_resource", "", _("Resource (Room or Equipment)"));
	liRes.recipientType = 3;
	dhtml.addEvent(moduleObject, liReqAtt, "mouseover", fbMouseOverRecipientTypeDropDownListItem);
	dhtml.addEvent(moduleObject, liReqAtt, "mouseout", fbMouseOutRecipientTypeDropDownListItem);
	dhtml.addEvent(moduleObject, liReqAtt, "click", fbClickRecipientTypeDropDownListItem);

	dhtml.addEvent(moduleObject, liOptAtt, "mouseover", fbMouseOverRecipientTypeDropDownListItem);
	dhtml.addEvent(moduleObject, liOptAtt, "mouseout", fbMouseOutRecipientTypeDropDownListItem);
	dhtml.addEvent(moduleObject, liOptAtt, "click", fbClickRecipientTypeDropDownListItem);

	dhtml.addEvent(moduleObject, liRes, "mouseover", fbMouseOverRecipientTypeDropDownListItem);
	dhtml.addEvent(moduleObject, liRes, "mouseout", fbMouseOutRecipientTypeDropDownListItem);
	dhtml.addEvent(moduleObject, liRes, "click", fbClickRecipientTypeDropDownListItem);

	// Cancel further event handlers
	event.stopPropagation();
}

function fbClickRecipientTypeBody(moduleObject, element, event){
	var selectionlist = dhtml.getElementById("meetingrequest_recipienttype_selectionlist")
	if(selectionlist){
		selectionlist.parentNode.removeChild(selectionlist);
	}
}
function fbMouseOverRecipientTypeDropDownListItem(moduleObject, element, event){
	dhtml.addClassName(element, "selected");
}
function fbMouseOutRecipientTypeDropDownListItem(moduleObject, element, event){
	dhtml.removeClassName(element, "selected");
}
function fbClickRecipientTypeDropDownListItem(moduleObject, element, event){
	moduleObject.userList[ element.parentNode.parentNode.userid ].recipienttype = element.recipientType;

	var iconElement = element.parentNode.parentNode.icon_element;
	dhtml.removeClassName(iconElement, "icon_meetingrequest_requiredattendee");
	dhtml.removeClassName(iconElement, "icon_meetingrequest_optionalattendee");
	dhtml.removeClassName(iconElement, "icon_meetingrequest_resource");
	switch(moduleObject.userList[ element.parentNode.parentNode.userid ].recipienttype){
		case MAPI_ORIG:
			dhtml.addClassName(iconElement, "icon_meetingrequest_organizer");
			break;
		case MAPI_TO:
			dhtml.addClassName(iconElement, "icon_meetingrequest_requiredattendee");
			break;
		case MAPI_CC:
			dhtml.addClassName(iconElement, "icon_meetingrequest_optionalattendee");
			break;
		case MAPI_BCC:
			dhtml.addClassName(iconElement, "icon_meetingrequest_resource");
			break;
	}

	// Remove selectionlist container
	var selectionlist = dhtml.getElementById("meetingrequest_recipienttype_selectionlist")
	if(selectionlist){
		selectionlist.parentNode.removeChild(selectionlist);
	}
}