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

ElasticTextarea.prototype = new Widget;
ElasticTextarea.prototype.constructor = ElasticTextarea;
ElasticTextarea.superclass = Widget.prototype;

/**
 * @constructor
 * This widget can be used to give auto expanding/shrinking facility to any textarea
 * this widget will create a dummy textarea for the original textarea and copy all contents to dummy textarea
 * the dummy textarea will be having height zero so everytime we check its scrollheight which will represent its
 * content height
 * 
 * @param {HTMLObject} element textarea element on which functionality will be added
 * @param {Function} callbackfunction callbackfunction to be called after resizing textarea,
 *					typically a resizing function
 * @param {String} path to the settings which will hold number of max rows to allow for expansion
 */
function ElasticTextarea(element, settingsPath, callBackFunction)
{
	this.dummyTextarea = false;
	this.textarea = false;
	this.textareaLineHeight = 14;
	this.settingsPath = settingsPath;

	// we can also pass id of element
	if(typeof element == "string") {
		this.textarea = dhtml.getElementById(element);
	} else if(typeof element == "object") {
		this.textarea = element;
	}

	this.callBackFunction = typeof callBackFunction == "function" ? callBackFunction : false;

	this.createDummyTextarea();

	// register event for actual expansion of textarea
	dhtml.addEvent(this, this.textarea, "keydown", autoExpandElasticTextarea);
	dhtml.addEvent(this, this.textarea, "keyup", autoExpandElasticTextarea);
	// we can use this event to manualy fire when we are setting data of textareas from another function
	// as changing data of textarea programatically doesn't fire any event
	dhtml.addEvent(this, this.textarea, "change", autoExpandElasticTextarea);
}

/**
 * this function will create a dummy textarea for height calculation,
 * this dummy textarea will be unique for every textarea and every widget instance
 */
ElasticTextarea.prototype.createDummyTextarea = function() {
	if(this.textarea && !this.dummyTextarea) {
		this.dummyTextarea = dhtml.addElement(document.body, "textarea", "field");
		this.dummyTextarea.style.position = "absolute";
		this.dummyTextarea.style.top = "-99px";
		this.dummyTextarea.style.left = "-9999px";
	}
}

/**
 * this function does actuall calculation of height and sets height of textarea
 * it also checks setting for maximum height of textarea, beyond this value we will not be
 * expanding the textarea
 */
ElasticTextarea.prototype.autoExpandTextarea = function() {
	if(this.textarea && this.dummyTextarea) {
		// set width of dummy textarea according to original textarea, so we will
		// get correct height everytime even if original textarea is resized after rendering
		this.dummyTextarea.style.width = (this.textarea.clientWidth - 1) + "px";

		// dump data of original textarea to dummy textarea to calculate height of content
		this.dummyTextarea.value = this.textarea.value;

		// get maximum height that is allowed for expansion
		var textareaMaxHeight = this.textareaLineHeight * parseInt(webclient.settings.get(this.settingsPath, "3"), 10);

		/**
		 * HACK ALERT
		 * The first call to the scrollHeight property is on it’s own line, and not even used.
		 * in IE when first time accessing scrollHeight property will give sometimes incorrect results
		 * so to get consistant results scrollHeight proprety is fetched twice
		 * http://soulpass.com/2006/07/24/ie-and-scrollheight/
		 */
		if(window.BROWSER_IE) this.dummyTextarea.scrollHeight;

		// IE adds padding values to scrollHeight property so remove that when using it
		var textareaTargetHeight = window.BROWSER_IE ? (this.dummyTextarea.scrollHeight - 4) : this.dummyTextarea.scrollHeight;
		
		if(textareaTargetHeight > textareaMaxHeight) {
			textareaTargetHeight = textareaMaxHeight;
		}

		this.textarea.style.height = textareaTargetHeight + "px";

		if (this.callBackFunction) {
			this.callBackFunction();
		}
	}
}

/**
 * @destrcutor
 */
ElasticTextarea.prototype.destructor = function()
{
	// delete dummy element
	dhtml.deleteElement(this.dummyTextarea);
	dhtml.removeEvent(this.textarea, "keydown");
	dhtml.removeEvent(this.textarea, "keyup");
	dhtml.removeEvent(this.textarea, "change");
	
	ElasticTextarea.superclass.destructor(this);
}

/**
 * @Global Event Function
 * this function is registered on every textarea and will be fired when data is changed in
 * every textarea
 */
function autoExpandElasticTextarea(widgetObject, element, event) {
	if(widgetObject) {
		widgetObject.autoExpandTextarea();
	}
}