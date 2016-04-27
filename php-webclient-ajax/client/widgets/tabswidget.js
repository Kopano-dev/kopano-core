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
 * --Tabs Widget--  
 * @type	Widget
 * @classDescription	 This widget can be used to create tabs in javascript
 * 
 * HOWTO USE:
 * Structure of Object tabs:
 *		tabs	=	{
 *						id	:	{
 *									title :	"TITLE TEXT",
 *									class :	"CLASS"
 *								}
 *					};
 * 
 * var tabbar = new TabsWidget(TAB_CONTAINER_ELEMENT, TABS_DATA_OBJECT, SELECTED_TAB_ID, HIDDEN_TAB_ID, TABBAR_ID[OPTIONAL]);
 * tabbar.createTabPages();
 *
 * DEPENDS ON:
 * -----> dhtml.js
 * -----> tabbar.css
 *
 * INFORMATION:
 * all data related to this widget will be stored in this.tabs variable
 * tabid							unique id of the tab
 * this.tabs[tabid]["title"]		title to display in tab
 * this.tabs[tabid]["tabitem"]		element of tabbar which contains title
 * this.tabs[tabid]["tabpage"]		container element of corresponding tabitem
 *
 * @TODO: Its good to have following functionalities in tab widget.
 *		1. Dynamically addition / deletion of tabs.
 *		2. close button in tab.
 *		3. scrolling in tab bar to scroll horizontally among tabs same as FF.
 *		4. add widget id in every element id to make html ids unique, currently this is not possible because
 *			currently we are using older tabbar.css file which has styles on html ids
 */
TabsWidget.prototype = new Widget;
TabsWidget.prototype.constructor = TabsWidget;
TabsWidget.superclass = Widget.prototype;

/**
 * @constructor
 * initializes widget variables and calls initTabsWidget to create tabbar
 * @param		HTMLElement		contentElement		container of tabbar
 * @param		Object			tabs				info of tabs that will be created
 * @param		String			selectedTab			tab id of tab that should be selected by default
 * @param		String			hideTabs			tab id of tabs that should be hidden by default,
 *													multiple tab ids can be space seperated
 * @param		String			tabbarId			id to give to tabbar which will be created
 */
function TabsWidget(contentElement, tabs, selectedTab, hideTabs, tabbarId)
{
	this.tabs = tabs;
	this.contentElement = contentElement;

	if(typeof tabbarId == "undefined" || tabbarId === false) {
		this.tabbarId = "tabbar";
	} else {
		this.tabbarId = tabbarId;
	}

	if(typeof selectedTab != "undefined" && selectedTab) {
		this.selectedTab = selectedTab;
	} else {
		this.selectedTab = false;
	}

	if(typeof hideTabs != "undefined" && hideTabs) {
		this.hideTabs = hideTabs;
	} else {
		this.hideTabs = false;
	}

	this.initTabsWidget(this.contentElement, this.tabs, this.selectedTab, this.hideTabs, this.tabbarId);
}

/**
 * this function creates all HTML elements for the tabbar
 * @param		HTMLElement		contentElement		container of tabbar
 * @param		Object			tabs				info of tabs that will be created
 * @param		String			selectedTab			tab id of tab that should be selected by default
 * @param		String			hideTabs			tab id of tabs that should be hidden by default,
 *													multiple tab ids can be space seperated
 * @param		String			tabbarId			id to give to tabbar which will be created
 */
TabsWidget.prototype.initTabsWidget = function(contentElement, tabs, selectedTab, hideTabs, tabbarId)
{
	// create tabs container div
	var tabsContainer = dhtml.addElement(contentElement, "div", false, tabbarId);
	var tabsList = dhtml.addElement(tabsContainer, "ul", false, false);
	var tabClass, tabListitem, tabListitemTitle;

	for(var tabId in tabs) {
		tabClass = (tabs[tabId]["class"]) ? tabs[tabId]["class"] : false;
		if(selectedTab && selectedTab == tabId) {
			if(tabClass == false) {
				tabClass = "selectedtab";
			} else {
				tabClass += " selectedtab";
			}
		}

		if(hideTabs && hideTabs.indexOf(tabId) != -1) {
			if(tabClass == false) {
				tabClass = "tab_hide";
			} else {
				tabClass += " tab_hide";
			}
		}

		tabListitem = dhtml.addElement(tabsList, "li", tabClass, "tab_" + tabId);
		tabListitemTitle = dhtml.addElement(tabListitem, "span", false, false, tabs[tabId]["title"]);

		// register event handlers
		dhtml.addEvent(this, tabListitem, "click", eventTabsWidgetClick);
		dhtml.addEvent(this, tabListitem, "mouseover", eventTabsWidgetMouseOver);
		dhtml.addEvent(this, tabListitem, "mouseout", eventTabsWidgetMouseOut);

		// add tab item element to tabs object
		tabs[tabId]["tabitem"] = tabListitem;
	}

	// add extra break to remove css float values
	dhtml.addElement(contentElement, "br", "tabbar_end", false);
}

/**
 * this function creates HTML div elements for tab pages
 * @param		HTMLElement		contentElement		container of tabpages
 */
TabsWidget.prototype.createTabPages = function(contentElement)
{
	var tabClass, tabpage;

	if(typeof contentElement == "undefined" && contentElement) {
		contentElement = this.contentElement;
	}

	for(var tabId in this.tabs) {
		tabClass = "tabpage";

		if(this.selectedTab == tabId && this.hideTab != tabId) {
			tabClass += " selectedtabpage";
		}

		// add tab page element to tabs object
		this.tabs[tabId]["tabpage"] = dhtml.addElement(this.contentElement, "div", tabClass, tabId + "_tab");
	}
}

/**
 * Function to change the tab
 * @param		String		newPage		The name of the page we want to change to
 */
TabsWidget.prototype.changeTab = function(newPage)
{
	if(this.tabs[newPage]["tabpage"]){
		var oldTab = this.selectedTab;
		this.selectedTab = newPage;
		for(tabId in this.tabs){
			if(tabId == this.selectedTab){
				// add selection to newly selected tab
				dhtml.addClassName(this.tabs[tabId]["tabitem"], "selectedtab");
				dhtml.addClassName(this.tabs[tabId]["tabpage"], "selectedtabpage");
			} else {
				// remove selection from old selected tab
				dhtml.removeClassName(this.tabs[tabId]["tabitem"], "selectedtab");
				dhtml.removeClassName(this.tabs[tabId]["tabpage"], "selectedtabpage");
			}
		}

		if (this.handler && this.handler != null){
			this.handler(this.selectedTab, oldTab);
		}
	}

	/**
	 * Resize the body to get proper layout after changing tab
	 * because different tabs can have different height
	 */
	resizeBody();
}

/**
 * Function to get id of the currently selected tab
 * @return		String		id of the currently selected tab
 */
TabsWidget.prototype.getSelectedTab = function()
{
	return this.selectedTab;
}

/**
 * Function to get tabpage of the currently selected tab
 * @return		HTMLElement		tabpage of the currently selected tab
 */
TabsWidget.prototype.getSelectedTabPage = function()
{
	return this.getTabPage(this.selectedTab);
}

/**
 * Function to get tabpage of a tab from tabid
 * @return		HTMLElement		tabpage of specified tabid
 */
TabsWidget.prototype.getTabPage = function(tabId)
{
	return this.tabs[tabId]["tabpage"];
}

/**
 * Function to show a particular tab
 * @param		String		tabId		tabid of tab that should be shown
 */
TabsWidget.prototype.showTab = function(tabId)
{
	dhtml.removeClassName(this.tabs[tabId]["tabitem"], "tab_hide");
}

/**
 * Function to hide a particular tab
 * @param		String		tabId		tabid of tab that should be hidden
 */
TabsWidget.prototype.hideTab = function(tabId)
{
	if(tabId == this.selectedTab) {
		// don't hide currently selected tab
		return false;
	}

	dhtml.addClassName(this.tabs[tabId]["tabitem"], "tab_hide");
}

/**
 * Function to toggle visibility of particular tab
 * @param		String		tabId		tabid of tab that should be toggled
 */
TabsWidget.prototype.toggleTabVisibility = function(tabId)
{
	if(dhtml.hasClassName(this.tabs[tabId]["tabitem"], "tab_hide")) {
		this.showTab(tabId);
	} else {
		this.hideTab(tabId);
	}
}

/**
 * Function to change title of particular tab
 * @param		String		tabId		tabid of tab
 * @param		String		newTitle	new title
 */
TabsWidget.prototype.changeTabTitle = function(tabId, newTitle)
{
	this.tabs[tabId]["tabitem"].firstChild.firstChild.nodeValue = newTitle;
}

/**
 * Function to get all info about tabs
 * @return		Object		this.tabs		all information about tabs
 */
TabsWidget.prototype.getAllTabs = function()
{
	return this.tabs;
}

/**
 * @destrcutor
 */
TabsWidget.prototype.destructor = function()
{
	var tabBarContainerElement = dhtml.getElementById(this.tabbarId, "div", this.contentElement);

	// remove external event handlers
	this.delExternalHandler();

	// remove internal events before deleting elements
	dhtml.removeEvents(tabBarContainerElement);

	// delete tabbar elements
	dhtml.deleteAllChildren(tabBarContainerElement);
	dhtml.deleteElement(tabBarContainerElement);
	
	// remove tabpages
	for(var tabId in this.tabs) {
		dhtml.deleteAllChildren(this.tabs[tabId]["tabpage"]);
		dhtml.deleteElement(this.tabs[tabId]["tabpage"]);
	}

	TabsWidget.superclass.destructor(this);
}

/***** Event handlers *******/

/**
 * Function to add a handler which will be called when the tabbar changes view
 * the handler will be called with a string argument
 * which contains the new active tabname and the old tabname
 * @param		EventFunction		handler		reference of the event function
 */
TabsWidget.prototype.addExternalHandler = function(handler)
{
	this.handler = handler;
}

/**
 * Function will remove reference of external handler
 */
TabsWidget.prototype.delExternalHandler = function()
{
	this.handler = null;
}

/**
 * Function will be called when user clicks on a tab
 * @param		Object			widgetObject		widget object
 * @param		HTMLElement		element				element on which event occured
 * @param		EventObject		event				event object
 */
function eventTabsWidgetClick(widgetObject, element, event)
{
	var tabName = element.id.substring(4);
	widgetObject.changeTab(tabName);
}

/**
 * Function is used to change css class for hover functionality
 * @param		Object			widgetObject		widget object
 * @param		HTMLElement		element				element on which event occured
 * @param		EventObject		event				event object
 */
function eventTabsWidgetMouseOver(widgetObject, element, event)
{
	dhtml.addClassName(element, "hover");
}

/**
 * Function is used to change css class for hover functionality
 * @param		Object			widgetObject		widget object
 * @param		HTMLElement		element				element on which event occured
 * @param		EventObject		event				event object
 */
function eventTabsWidgetMouseOut(widgetObject, element, event)
{
	dhtml.removeClassName(element, "hover");
}