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
 * --CombinedDateTimePicker widget--   
 * 
 * This widget will create a quota bar  
 * 
 * HOWTO BUILD:
 * - make two div with unique id "quota_footer"
 * - this.quotaBar = new QuotaWidget(dhtml.getElementById("quota_footer"),"quota");
 * HOWTO USE:
 * - to update the quota: this.quotaBar.update(store_size,quota_warning,quota_soft,quota_hard);
 * DEPENDS ON:
 * |------> dhtml.js
 * |------> style.css (you need some edit the style of element in the css file)
 */

QuotaWidget.prototype = new Widget;
QuotaWidget.prototype.constructor = QuotaWidget;
QuotaWidget.superclass = Widget.prototype;
 
//PUBLIC
/**
 * Constructor
 * @param element = html element where the quotabar have to be placed in
 * @param labltext = string that will be placed before the bar  
 */ 
function QuotaWidget(element,labeltext)
{
	this.hideWhenZero = true;
	this.element = element;
	this.element.style.display = "none";
	if(!labeltext){
		labeltext = "";
	}
	this.render(labeltext);
}

/**
 * Function will update the quota bar
 * @param size = int size of store in kb
 * @param warn = int warning of store in kb  
 * @param soft = int soft quota of store in kb
 * @param hard = int hard quota of store in kb  
 */ 
QuotaWidget.prototype.update = function(size, warn, soft, hard)
{
	var currentSize = parseInt(size, 10);
	var warnSize = parseInt(warn, 10);
	var softSize = parseInt(soft, 10);
	var hardSize = parseInt(hard, 10);
	var maxLimit = 0;
	var factor = 0;
	var firstBlockSize = 0;
	var secondBlockSize = 0;
	var thirdBlockSize = 0;
	var quota = [];			// display of qouta bar will depend on ordering of array elements

	// only show qouta bar when hard or soft quota is set
	if(hardSize > softSize) {
		maxLimit = hardSize;
	} else if(softSize > hardSize) {
		maxLimit = softSize;
	}

	if(maxLimit > 0) {
		if(warnSize <= softSize) {
			quota.push({
				qoutaname: "warn",
				size: warnSize,
				element: this.warnElement
			});

			quota.push({
				qoutaname: "soft",
				size: softSize,
				element: this.softElement
			});

			quota.push({
				qoutaname: "hard",
				size: hardSize,
				element: this.hardElement
			});
		} else if(softSize < warnSize) {
			quota.push({
				qoutaname: "soft",
				size: softSize,
				element: this.softElement
			});

			quota.push({
				qoutaname: "warn",
				size: warnSize,
				element: this.warnElement
			});

			quota.push({
				qoutaname: "hard",
				size: hardSize,
				element: this.hardElement
			});
		}

		this.element.style.display = "block";
		factor = this.barElement.offsetWidth / maxLimit;		// total width of qouta bar / max qouta limit

		// Set label (current size / qouta size)
		dhtml.deleteAllChildren(this.labelElement);
		dhtml.addTextNode(this.labelElement, Math.roundDecimal(currentSize / 1024, 1) + " / " + Math.roundDecimal(maxLimit / 1024, 1) + " " + _("MB"));

		// Calc value
		// get absolute difference between qouta levels
		/**
		 * |--------|							first
		 * |-------------------|				second
		 * |------------------------------|		third
		 * 
		 * absolute difference
		 * |--------|							first
		 * 			|----------|				second
		 *					   |----------|		third
		 */
		thirdBlockSize = quota[2].size - quota[1].size;
		secondBlockSize = quota[1].size - quota[0].size;
		firstBlockSize = quota[0].size;

		/**
		 * find range of qouta level in which current store size occurs
		 * we have to only change qouta level of that range, before that range all
		 * qouta levels will be same as before and all qouta levels after that range
		 * will be zero
		 */
		/**
		 * |--------|							first
		 * |-------------------|				second
		 * |------------------------------|		third
		 * |-------------|						currentsize
		 * 
		 * after calculation
		 * |--------|							first
		 * 			|----|						second = currentsize - first
		 *										third = 0
		 */
		if(currentSize < firstBlockSize) {
			firstBlockSize = currentSize; currentSize = 0;
		} else {
			currentSize -= firstBlockSize;
		}

		if(currentSize < secondBlockSize) {
			secondBlockSize = currentSize; currentSize = 0;
		} else {
			currentSize -= secondBlockSize;
		}

		if(currentSize < thirdBlockSize) {
			thirdBlockSize = currentSize;
		}

		// Update view, set width of blocks
		quota[0].element.style.width = (firstBlockSize > 0) ? parseInt(firstBlockSize * factor, 10) + "px" : "0px";
		quota[1].element.style.width = (secondBlockSize > 0) ? parseInt(secondBlockSize * factor, 10) + "px" : "0px";
		quota[2].element.style.width = (thirdBlockSize > 0) ? parseInt(thirdBlockSize * factor, 10) + "px" : "0px";

		// set starting position of blocks
		quota[0].element.style.left = "0px";
		quota[1].element.style.left = quota[0].element.offsetWidth + "px";
		quota[2].element.style.left = quota[0].element.offsetWidth + quota[1].element.offsetWidth + "px";
	}
}

//PRIVATE
/**
 * Function will draw the quota bar
 * @param labeltext = string text will be placed before quota bar 
 */ 
QuotaWidget.prototype.render = function(labeltext)
{
	dhtml.deleteAllChildren(this.element);
	if(labeltext.length > 0){
		dhtml.addElement(this.element,"span","quota_display","",labeltext+":");
	}else{
		dhtml.addElement(this.element,"span","quota_display");
	}
	this.barElement = dhtml.addElement(this.element,"div","quota_bar");
	this.warnElement = dhtml.addElement(this.barElement,"span","quota_warning");
	this.softElement = dhtml.addElement(this.barElement,"span","quota_soft");
	this.hardElement = dhtml.addElement(this.barElement,"span","quota_hard");
	this.labelElement = dhtml.addElement(this.element,"span","quota_label");
}
