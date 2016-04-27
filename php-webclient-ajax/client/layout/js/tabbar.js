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
 * This class can be used to output a tabbar control
 *
 * HOWTO BUILD:
 * - see tabbar.class.php
 * - tabbarControl = tabbar; //tabbarControl will be used in example 
 * HOWTO USE:
 * - see tabbar.class.php
 * - tabbarControl.addExternalHandler(handler); //handler will be executed on tab switch
 * - tabbarControl.delExternalHandler(); //will remove the current handler
 * - tabbarControl.getSelectedTab(); //will return name the selected tab 
 * DEPENDS ON:
 * |------> tabbar.php
 * |------> tabbar.css
 * |------> dhtml.js 
 */

/**
 * Constructor
 *
 * @param string tabbar_id The HTML id for the tabbar (set in PHP)
 * @param array tabpages   All tabpages are specified here
 */
function TabBar(tabbar_id, tabpages)
{
	this.element = dhtml.getElementById(tabbar_id);
	this.pages = tabpages;
	
	// add event handlers
	for(var tab_id in this.pages){
		var tabbutton = dhtml.getElementById('tab_'+tab_id);
		dhtml.addEvent(this, tabbutton, "click", eventTabBarClick);
		dhtml.addEvent(this, tabbutton, "mouseover", eventTabBarMouseOver);
		dhtml.addEvent(this, tabbutton, "mouseout", eventTabBarMouseOut);

		if (tabbutton.className == "selectedtab"){
			this.selected_tab = tab_id;
		}
	}
}

/**
 * Function to add a handler which will be called when the tabbar changes view, the handler will be called with
 * a string argument, witch contains the new active tabname and the old tabname
 */
TabBar.prototype.addExternalHandler = function(handler)
{
	this.handler = handler;
}

TabBar.prototype.delExternalHandler = function()
{
	this.handler = null;
}

/**
 * Function to change the page
 *
 * @param string newPage The name of the page we want to change to.
 */
TabBar.prototype.change = function(newPage)
{
	if (this.pages[newPage]){
		var old_tab = this.selected_tab;
		this.selected_tab = newPage;
		for(tab_id in this.pages){
			if (tab_id == this.selected_tab){
				dhtml.getElementById('tab_'+tab_id).className = "selectedtab";
				dhtml.getElementById(tab_id+'_tab').className = "tabpage selectedtabpage";
			}else{
				dhtml.removeClassName(dhtml.getElementById('tab_'+tab_id), "selectedtab");
				dhtml.getElementById(tab_id+'_tab').className = "tabpage";
			}
		}

		if (this.handler && this.handler != null){
			this.handler(this.selected_tab, old_tab);
		}
	}
	
	// Resize the body.
	// The layout brakes when body is resized when it is not visible. So
	// every time a tab is clicked the body will be resized. To make sure
	// the layout won't brake.
	resizeBody();
}

/**
 * Function to get the current selected tab
 */
TabBar.prototype.getSelectedTab = function()
{
	return this.selected_tab;
}

// Event handlers

function eventTabBarClick(tabBarObject, element, event)
{
	var buttonName = element.id.substring(4);
	tabBarObject.change(buttonName);
	return false;
}

function eventTabBarMouseOver(tabBarObject, element, event)
{
	if (element.className.indexOf("hover") == -1){
		element.className += " hover";
	}
}

function eventTabBarMouseOut(tabBarObject, element, event)
{
	if (element.className.indexOf("hover") != -1){
		element.className = element.className.substring(0,element.className.indexOf("hover")-1)+element.className.substring(element.className.indexOf("hover")+5, element.className.length);
	}
}
