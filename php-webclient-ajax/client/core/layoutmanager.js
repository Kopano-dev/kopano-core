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
 * LayoutManager
 * This object is responisble for managing the layout. It has three 
 * positions to manage, namely left, right and main.
 * @todo
 * - implement changeLayout function. If an user for example wants the preview pane
 *   and the mail table next to each other      
 */ 
function LayoutManager()
{
	// Layout:
	// max: max number of items (modules) in that position
	// layout: layout constant (see webclient.js)
	// elements: html elements in that position
	this.layout = new Object();
	this.layout["left"] = new Object();
	this.layout["left"]["max"] = 3;
	this.layout["left"]["layout"] = BOX_LAYOUT;
	this.layout["left"]["fixed_layout"] = true;
	this.layout["left"]["elements"] = new Object();

	this.layout["main"] = new Object();
	this.layout["main"]["max"] = 10;
	this.layout["main"]["layout"] = BORDER_LAYOUT;
	this.layout["main"]["fixed_layout"] = false;
	this.layout["main"]["elements"] = new Object();

	this.layout["right"] = new Object();
	this.layout["right"]["max"] = 3;
	this.layout["right"]["layout"] = BOX_LAYOUT;
	this.layout["right"]["fixed_layout"] = true;
	this.layout["right"]["elements"] = new Object();

	// todo: footer must also be an layout, so we can add stuff to it like "counter", "quota", "buttons"
	this.setFooter();
	
	//add events for resizing panes...
	var leftmain_resizebar = dhtml.getElementById("leftmain_resizebar");
	var mainright_resizebar = dhtml.getElementById("mainright_resizebar");
	
	dhtml.addEvent(-1, leftmain_resizebar, "mouseover", eventBodyResizePanesMouseOver);
	dhtml.addEvent(-1, mainright_resizebar, "mouseover", eventBodyResizePanesMouseOver);
	dhtml.addEvent(-1, leftmain_resizebar, "mousedown", eventBodyResizePanesMouseDown);
	dhtml.addEvent(-1, mainright_resizebar, "mousedown", eventBodyResizePanesMouseDown);
	dhtml.addEvent(-1, document.body, "mouseup", eventBodyResizePanesMouseUp);
}
/**
 * Function which adds a new element in the given position.
 * @param integer moduleID the module id
 * @param string position the position of the new element
 * @param integer LAYOUT the LAYOUT constant
 * @param integer INSERT_ELEMENT the INSERT_ELEMENT constant
 * @return object the new element 
 */ 
LayoutManager.prototype.addModule = function(moduleID, position, LAYOUT, INSERT_ELEMENT)
{
	var element = false;

	// Check if the given position exists
	if(this.layout[position] && this.layout[position]["max"]) {
		// Get number of items in the given position
		var length = 0;
		for(var i in this.layout[position]["elements"])
		{
			length++;
		}

		// Verify if max is reached
		if((length + 1) <= this.layout[position]["max"]) {
			// Get position element
			var layoutElement = dhtml.getElementById(position);
			// Set layout, not for left and right, because these two are always in a BOX_LAYOUT
			if(this.layout[position]["fixed_layout"] == false) {
				this.layout[position]["layout"] = LAYOUT;
			}

			if(layoutElement) {
				// Add the element, depending on the LAYOUT
				switch(this.layout[position]["layout"])
				{
					case BORDER_LAYOUT:
						// Remove all elements in layoutElement
						dhtml.deleteAllChildren(layoutElement);
						element = dhtml.addElement(layoutElement, "div", "layoutelement");
						break;
					case FLOW_LAYOUT:

						break;
					case GRID_LAYOUT:

						break;
					case BOX_LAYOUT:
						// Add resizebar
						if (length > 0 && position == "main"){
							// Create resize bar
							var resizeBar = dhtml.addElement(layoutElement, "div", "icon icon_resizebar_vertical vertical_resizebar", "main_vertical_resizebar"+ length);
							resizeBar.innerHTML = "&nbsp;";			// IE bug
							resizeBar.setAttribute("resizebar", 1);		// Set attribute which says it is resize bar.
							// Add events on resize bar.
							dhtml.addEvent(-1, resizeBar, "mouseover", eventBodyResizePanesMouseOver);
							dhtml.addEvent(-1, resizeBar, "mousedown", eventBodyResizePanesMouseDown);
						}
						// Add the element
						element = dhtml.addElement(layoutElement, "div", "layoutelement", position +"_"+ length);

						// Verify where the element should be added
						switch(INSERT_ELEMENT)
						{
							case INSERT_ELEMENT_AT_TOP:
								if(layoutElement.firstChild) {
									layoutElement.insertBefore(element, layoutElement.firstChild);
								}
								break;
							case INSERT_ELEMENT_BETWEEN:
								var childNodes = layoutElement.childNodes.length;
								var elementPosition = Math.round(childNodes.length / (length + 1));

								if(layoutElement.childNodes[elementPosition]) {
									layoutElement.insertBefore(element, layoutElement.childNodes[elementPosition]);
								}
								break;
							case INSERT_ELEMENT_AT_BOTTOM:
								// is default
								break;
						}
						break;
				}

				this.layout[position]["elements"][moduleID] = element;
			}
		}
	}
	return element;
}

/**
 * Function which updates all elements in the layout (resize). 
 */ 
LayoutManager.prototype.updateAllElements = function()
{
	for(var i in this.layout)
	{
		this.updateElements(i);
	}
}

/**
 * Function which updates the elements in the given position.
 * @param string position the position 
 */ 
LayoutManager.prototype.updateElements = function(position)
{
	if(this.layout[position]) {
		var nVariableHeight = 0;
		// Height of the position element (total height available)
		var height = dhtml.getElementById(position).offsetHeight;

		// Loop through the elements in the given position
		for(var moduleID in this.layout[position]["elements"])
		{
			// Get module
			var module = webclient.getModule(moduleID);

			// Verify if the module has a fixed height.
			if(module) {
				if(module.elementHeight) {
						// nVariableHeight minus 1
					if(nVariableHeight > 0) {
						// Height minus the fixed height plus the border
						height -= module.elementHeight + 4;
					}					
				} else {

					// Module with variable height
					nVariableHeight++;
				}
			} else {
				// No module -> variable height
				nVariableHeight++;
			}
		}

		// Calculate the height of every variable height element in the given position
		// Remove margin height for n-1 modules (n modules, n-1 margins)
		height = (height - (nVariableHeight-1) * 4) / nVariableHeight;

		// Loop through the elements and set the height
		for(var moduleID in this.layout[position]["elements"])
		{
			var module = webclient.getModule(moduleID);

			if(module) {
				var element = this.layout[position]["elements"][moduleID];

				// Set the height of the element
				if(module.elementHeight) {
					element.style.height = module.elementHeight + "px";			
				} else {
					if (height<0) height = 0;
					element.style.height = height + "px";
				}
				
				// Now resizebars are placed to no margin is required.
				
				// Call the resize function
				if(module.resize) {
					module.resize();
				}
			}
		}
	}
}

/**
 * Function which deletes an element.
 * @param integer moduleID the module id which will be deleted 
 */ 
LayoutManager.prototype.deleteElement = function(moduleID)
{
	for(var position in this.layout)
	{
		if(this.layout[position]["elements"] && this.layout[position]["elements"][moduleID]) {
			dhtml.deleteElement(this.layout[position]["elements"][moduleID]);
			delete this.layout[position]["elements"][moduleID];

			this.updateElements(position);
		}
	}
}

/**
 * Function which returns the elements in the given position.
 * @param string position the position 
 * @return array list of elements in the given position
 */ 
LayoutManager.prototype.getElements = function(position)
{
	var elements = new Array();

	if(this.layout[position] && this.layout[position]["elements"]) {
		elements = this.layout[position]["elements"];
	}

	return elements;
}

/**
 * Function which returns a list of all moduleID's in the view
 */
LayoutManager.prototype.getModuleIDs = function()
{
	var result = new Array();
	for(var position in this.layout){
		var elements = this.getElements(position);
		for(var e in elements){
			if (elements[e].moduleID){
				result.push(elements[e].moduleID);
			}
		}
	}
	return result;
}

/**
 * Function which changes the LAYOUT in the given position.
 * @param string position the position
 * @param integer LAYOUT the LAYOUT constant
 * @todo
 * - implement this function and resize the elements   
 */ 
LayoutManager.prototype.changeLayout = function(position, LAYOUT)
{
}

/**
 * Function which sets the footer of the webclient
 */ 
LayoutManager.prototype.setFooter = function()
{
	var loggedon = dhtml.getElementById("loggedon");

	var loggedonas = document.createElement("span");
	loggedonas.innerHTML = _("you are logged on as") + " " + escapeHtml(webclient.fullname);
	loggedon.appendChild(loggedonas);

	var seperator = document.createElement("span");
	seperator.innerHTML = "&nbsp;<b>&#183;</b>&nbsp;";
	loggedon.appendChild(seperator);

	var settings = document.createElement("span");
	settings.className = "settingsbutton";
	settings.onclick =  function() { webclient.openModalDialog(null, 'settings', DIALOG_URL+'task=open_settings', 700, 620); }
	settings.innerHTML = _("settings");
	loggedon.appendChild(settings);

	if (!SSO_LOGIN) {
		var seperator2 = document.createElement("span");
		seperator2.innerHTML = "&nbsp;<b>&#183;</b>&nbsp;";
		loggedon.appendChild(seperator2);

		var logout = dhtml.addElement(loggedon, "span", "loggoutbutton", false, _("log out"));
		dhtml.addEvent(-1, logout, "click", eventLogout);
	}
}

/**
 * Function which registers mousemove events on body for resizing.
 */
function eventBodyResizePanesMouseDown(moduleObject, element, event)
{
	//if resizing panes, then add / remove events for resizing...
	if (webclient.resizePane){
		dhtml.addEvent(-1, document.body, "mousemove", eventBodyResizePanesMouseMove);
	}
}


/**
 * Function which saves sizes of all panes in settings.
 */
function eventBodyResizePanesMouseUp(moduleObject, element, event)
{
	if (webclient.resizePane !== false) {
		
		//Stop resizing panes by removing mousemove event.
		webclient.resizePane = false;
		dhtml.removeEvent(document.body, "mousemove", eventBodyResizePanesMouseMove);
		document.body.style.cursor = "default";
		var preview = dhtml.getElementById("main_1");
		var main_top = dhtml.getElementById("main_0");
		var windowWidth = document.documentElement.clientWidth;
		var windowHeight = document.documentElement.clientHeight;
		
		webclient.settings.set("global/hierarchylistwidth", parseFloat((dhtml.getElementById("left").clientWidth / windowWidth), 100));
		if (webclient.hasRightPane){
			/**
			 * NOTE: Though sizes are stored in percent of window size,
			 * the value for this setting is not stored in percent.
			 */
			webclient.settings.set("global/maillistwidth", dhtml.getElementById("main").clientWidth);
		} else if (preview){
			webclient.settings.set("global/mainupperpaneheight", parseFloat((main_top.clientHeight / windowHeight), 100));
		}
		
		
		webclient.settings.set("global/sizeinpercent", "true");
	}
	
	//remove div element over the webaccess.
	var divOverWebaccess = dhtml.getElementById("divOverWebaccess");
	if (divOverWebaccess){
	   dhtml.deleteElement(divOverWebaccess);
	}
}

/**
 * Function which sets and positions all the panes
 * while loading and also on resize of webclient.
 */
function eventResizePanes(moduleObject, element, event)
{
	var windowWidth = document.documentElement.clientWidth;
	var windowHeight = document.documentElement.clientHeight;
	
	var top = dhtml.getElementById("top");
	var footer = dhtml.getElementById("footer");

	// Setup left pane (height only)
	var left = dhtml.getElementById("left");
	if(left){
		if (webclient.settings.get("global/sizeinpercent", false)){
			var hierarchyListWidth = parseFloat(webclient.settings.get("global/hierarchylistwidth", 0.15));
			if(hierarchyListWidth > 0.5) {
				/** 
				 * width of hierarchy list can not be greater than 50%,
				 * if incase its greater than 50% then set it to its default value
				 */
				hierarchyListWidth = 0.15;
			}
			left.style.width = hierarchyListWidth * parseFloat(windowWidth) + "px";
		} else {
			left.style.width = parseInt(windowWidth * 0.15) + "px";
		}
	}
	
	var main = dhtml.getElementById("main");
	//Setup main pane
	if(main) {
		main.style.left = (parseInt(left.clientWidth, 10) + 8) + "px";
		var main_top = dhtml.getElementById("main_0");
		var previewpane = dhtml.getElementById("main_1");
		var divelement = dhtml.getElementById("divelement");

		if (webclient.hasRightPane) {
			
			if (webclient.settings.get("global/maillistwidth", 375) > parseInt((windowWidth - left.clientWidth), 10)) {
				main.style.width = parseInt((windowWidth - left.clientWidth), 10) - 80 + "px";
				webclient.settings.set("global/maillistwidth", main.clientWidth);
			} else {
				main.style.width = webclient.settings.get("global/maillistwidth", 375) + "px";
			}
			
			// PreviewPane exists, so remove all resize bars from main pane.
			webclient.layoutmanager.removeAllresizebars("main");
		} else {
			main.style.width = (windowWidth - main.offsetLeft - 4) + "px";
		
			//Set height for previewpane bottom
			if (main_top && previewpane){
				if (webclient.settings.get("global/sizeinpercent", false)){
					main_top.style.height = parseFloat(webclient.settings.get("global/mainupperpaneheight", 0.25)) * parseFloat(windowHeight) + "px";
				} else {
					main_top.style.height = 375 + "px";
				}
				
	 			if (parseInt((main.clientHeight - main_top.clientHeight), 10) < 80){
	 				main_top.style.height = main.clientHeight - 80 + "px";
	 			} else {
	 				previewpane.style.height = main.clientHeight - main_top.clientHeight + "px";	
	 			}
	 			
	 			//Set height of element that contains list of items.
				if (divelement) 
					divelement.style.height = main_top.clientHeight - divelement.offsetTop + "px";
			}
		}
	}
	
	// Setup resizebar between left and main
	var leftmain_resizebar = dhtml.getElementById("leftmain_resizebar");
	if (leftmain_resizebar && left) {
		leftmain_resizebar.style.left = left.offsetLeft + left.clientWidth + "px";
		leftmain_resizebar.style.top = left.offsetTop + "px"; 
		leftmain_resizebar.style.height = left.clientHeight + "px";
	}
	
	// Setup right pane
	var right = dhtml.getElementById("right");
	var mainright_resizebar = dhtml.getElementById("mainright_resizebar");
	if(right) {
		if(webclient.hasRightPane) {
			right.style.width = parseInt(windowWidth, 10) - parseInt(main.offsetLeft, 10) - 4 - parseInt(webclient.settings.get("global/maillistwidth", 375), 10) + "px";
			right.style.left = parseInt(main.offsetLeft, 10) + parseInt(webclient.settings.get("global/maillistwidth", 375), 10) + 4 + "px";
			right.style.display = "block";
			
			// Setup resizebar between main and right.
			if (mainright_resizebar && main) {
				mainright_resizebar.style.display = "block";
				mainright_resizebar.style.left = main.offsetLeft + main.clientWidth + "px";
				mainright_resizebar.style.top = main.offsetTop + "px"; 
				mainright_resizebar.style.height = main.clientHeight + "px";
				mainright_resizebar.style.width = "5px";
			}
		} else {
			right.style.width = "0px";
			right.style.display = "none";
			
			// Hide resizebar when right is absent.
			if (mainright_resizebar) {
				mainright_resizebar.style.width = "0px";
				mainright_resizebar.style.display = "none";
			}
		}
		right.style.height = main.style.height;
	}
}

/**
 * Function which indicates resizing 
 * capabilities of webaccess, shows resize cursors
 */
function eventBodyResizePanesMouseOver(moduleObject, element, event)
{
	webclient.resizePane = element.id.substring(0, element.id.indexOf("_"));
}

/**
 * Function which drags and resizes panes.
 */
function eventBodyResizePanesMouseMove(moduleObject, element, event)
{
	var divOverWebaccess = dhtml.getElementById("divOverWebaccess");
	
	//Check div over entire webaccess is present
	if (divOverWebaccess){
		var left = dhtml.getElementById("left");
		var main = dhtml.getElementById("main");
		var right = dhtml.getElementById("right");
		var footer = dhtml.getElementById("footer");
		var windowwidth = document.documentElement.clientWidth;
		var previewpane = dhtml.getElementById("main_1");
		
		//Contains name of panes, that are needed for module resize after resizing.
		var resizeModule = new Array();
	
		if (webclient.resizePane !== false) {
			
			switch(webclient.resizePane)
			{
				case "leftmain":
					resizeModule.push("left");
					resizeModule.push("main");
					if (event.clientX < parseInt((windowwidth / 2), 10) && event.clientX > 128) {
						left.style.width = event.clientX - 5 + "px";
						main.style.left = event.clientX + 3 + "px";
						
						// Also set resizebar
						var leftmain_resizebar = dhtml.getElementById("leftmain_resizebar");
						if (leftmain_resizebar && left) {
							leftmain_resizebar.style.left = left.offsetLeft + left.clientWidth + "px";
						}
						
						if (webclient.hasRightPane) {
							resizeModule.push("right");
							var temp = main.clientWidth + right.clientWidth; 
							if (right.clientWidth < 40 && temp > parseInt((windowwidth / 2), 10)) {
								main.style.width = right.offsetLeft - event.clientX - 7 + "px";
							} else {
								right.style.left = event.clientX + main.clientWidth + 7 + "px";
								right.style.width = windowwidth - right.offsetLeft - 4 +"px";
							}
							
							// Also set resizebar.
							var mainright_resizebar = dhtml.getElementById("mainright_resizebar");
							if (mainright_resizebar && main) {
								mainright_resizebar.style.left = main.offsetLeft + main.clientWidth + "px";
							}
						} else {
							main.style.width = windowwidth - event.clientX - 6 + "px";
						}
					}
					break;
					
				case "mainright":
					resizeModule.push("main");
					resizeModule.push("right");
					if (event.clientX > parseInt((main.offsetLeft + 50), 10) && event.clientX < parseInt((windowwidth - 30), 10)) {
						right.style.left = event.clientX + "px";
						right.style.width = windowwidth - event.clientX - 4 + "px";
						main.style.width = event.clientX - main.offsetLeft - 4 + "px";
						
						// Also set resizebar.
						var mainright_resizebar = dhtml.getElementById("mainright_resizebar");
						if (mainright_resizebar && main) {
							mainright_resizebar.style.left = main.offsetLeft + main.clientWidth + "px";
						}
					}
					break;
					
				case "main":
					resizeModule.push("main");
					if (event.clientY < parseInt((footer.offsetTop - 80), 10) && event.clientY > parseInt((main.offsetTop + 80), 10)){
						var main_top = dhtml.getElementById("main_0");
						var previewpane = dhtml.getElementById("main_1");
						var divelement = dhtml.getElementById("divelement");
						var table = divelement.getElementsByTagName("table")[0];
						var divelementMinSize = main.offsetTop + divelement.offsetTop + 20;
						
						//calculate total height of first two items, b,couz the upper pane should show atleast two items...
						for (var i = 0; i < 2; i++){
							//If folder is empty then table will not have any rows, so checking any rows present...
							if (table.rows[i]){
								divelementMinSize += table.rows[i].clientHeight; 
							}
						}
	
						if (event.clientY > divelementMinSize) {
							main_top.style.height = event.clientY - main.offsetTop - 5 + "px";
							
							//resize divelement which contains list of items...
							divelement.style.height = main_top.clientHeight - divelement.offsetTop + "px";
						}
					}
					break;
			}
		}
		
		//resize module which are contained by the panes
		for (var position in resizeModule) {
			for(var moduleID in webclient.layoutmanager.layout[resizeModule[position]]["elements"]) {
				var module = webclient.getModule(moduleID);
				if (module){
					module.resize();
				}
			}
		}
		resizeModule = "";
		
	} else {
		//If div over entire webaccess is not there, then create it.
		
		divOverWebaccess = dhtml.addElement(document.body, "div", "divover", "divOverWebaccess");
		divOverWebaccess.style.left = "0px";
		divOverWebaccess.style.top = "0px";
		divOverWebaccess.style.width = document.documentElement.clientWidth + "px";
		divOverWebaccess.style.height = document.documentElement.clientHeight + "px";
		
		dhtml.addEvent(-1, divOverWebaccess, "mousemove", eventDivOverWebaccessMouseMove);
		dhtml.addEvent(-1, divOverWebaccess, "mouseout", eventDivOverWebaccessMouseOut);
	}
}

/**
 * Function which virtually propagates the event from IFrame to document.body,
 * because mousemove on IFrame prevents calling of function eventBodyResizePanesMouseMove(),
 * which is actually responsible for resizing the panes.
 */
function eventDivOverWebaccessMouseMove(moduleObject, element, event)
{
	/**
	 * Could use dhtml.executeEvent(),
	 * but need mouse co-ordinates of event object
	 * which is passed as arguments to eventDivOverWebaccessMouseMove() 
	 * function.
	 */
	eventBodyResizePanesMouseMove(moduleObject, element, event);
}

/**
 * Function which calls mouseup event of document body
 * whenever mouse pointer gets outside the browser window.
 */
function eventDivOverWebaccessMouseOut(moduleObject, element, event)
{
	dhtml.executeEvent(document.body, "mouseup");
}
LayoutManager.prototype.removeAllresizebars = function (position)
{
	var element = dhtml.getElementById(position);
	// remove all vertical resizebars.
	for (var i = 0; i < element.childNodes.length; i++) {
		var child = element.childNodes[i];
		if (child.getAttribute("resizebar") == true) {
			dhtml.deleteElement(child);
		}
	}
}
/**
 * Function which logout from WA.
 */
function eventLogout(moduleObject, element, event)
{
	// Abort all xml requests whoes response is not yet received
	webclient.xmlrequest.abortAll();
	window.location = "index.php?logout";
}