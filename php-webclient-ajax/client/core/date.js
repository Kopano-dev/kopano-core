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
* Prototyping the default Date object
*/

/**
 * Function which returns the number of days in a month.
 * If month is given, use this month instead of this.getMonth()
 * @return integer number of days in a month 
 */ 
Date.prototype.getDaysInMonth = function(month, year)
{
	if (typeof month == "undefined"){
		month = this.getMonth();
	}
	if (typeof year == "undefined"){
		year = this.getFullYear();
	}

	var DAYS_IN_MONTH = new Array(31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31);

	var days = 0;
	
	if(DAYS_IN_MONTH[month]) {
		days = DAYS_IN_MONTH[month];
		
		// Month is 'February', check is this year is a leap year.
		if(month == 1) {
			if(this.isLeapYear(year)) {
				days++;
			}
		}
	}
	
	return days;
}

/**
 * Function will add one or more days to current date
 * @param	integer		dayCount	number of days to add
 */
Date.prototype.addDays = function(dayCount)
{
	if (typeof dayCount == "undefined" || dayCount === null || dayCount === false) {
		dayCount = 1;
	}
	
	var oldOffset = this.getTimezoneOffset();
	this.setTime(this.getTime()+(ONE_DAY*dayCount));
	var newOffset = this.getTimezoneOffset();
	
	this.setTime(this.getTime()+((newOffset-oldOffset)*60000));
}

/**
 * Function will add one or more hours to current time (timezone/dst safe)
 * @param	integer		hourCount	number of hours to add
 */
Date.prototype.addHours = function(hourCount)
{
	if (typeof hourCount == "undefined" || hourCount === null || hourCount === false) {
		hourCount = 1;
	}

	var oldOffset = this.getTimezoneOffset();
	this.setTime(this.getTime() + (ONE_HOUR * hourCount));
	var newOffset = this.getTimezoneOffset();

	this.setTime(this.getTime() + ((newOffset - oldOffset) * 60000));
}

/**
 * Function will add timestamp to date object (timezone/dst safe)
 * @param	integer		unixTimestampToAdd	unixtimestamp in seconds
 */
Date.prototype.addUnixTimeStampToDate = function(unixTimeStampToAdd)
{
	// convert timestamp into miliseconds from seconds
	unixTimeStampToAdd = unixTimeStampToAdd * 1000;

	var oldOffset = this.getTimezoneOffset();
	this.setTime(this.getTime() + unixTimeStampToAdd);
	var newOffset = this.getTimezoneOffset();

	this.setTime(this.getTime() + ((newOffset - oldOffset) * 60000));
}

/**
 * Function will add one or more months to current date
 * @param	integer		dayCount	number of months to add
 */
Date.prototype.addMonths = function(monthCount)
{
	if (typeof monthCount == "undefined" || monthCount === null || monthCount === false) {
		monthCount = 1;
	}

	this.setMonth(this.getMonth() + monthCount);
}

/**
 * Function which verifies if the given year is a leap year.
 * @return boolean true - year is leap year, false - year is no leap year 
 */ 
Date.prototype.isLeapYear = function(year)
{
	if (typeof year == "undefined"){
		year = this.getFullYear();
	}

	isLeapYear = false;
	
	if((year % 4) == 0 || (year % 400) == 0) {
		isLeapYear = true;
	}
	
	return isLeapYear;
}

/**
 * Returns the number of the week in year, as defined in ISO 8601. 
 */
Date.prototype.getWeekNumber = function(){
	var d = new Date(this.getFullYear(), this.getMonth(), this.getDate(), 0, 0, 0);
	var DoW = d.getDay();
	d.setDate(d.getDate() - (DoW + 6) % 7 + 3); // Nearest Thu
	var ms = d.valueOf(); // GMT
	d.setMonth(0);
	d.setDate(4); // Thu in Week 1
	return Math.round((ms - d.valueOf()) / (7 * 864e5)) + 1;
}

/**
 * Get the week number of the month (1 to 5)
 */
Date.prototype.getWeekNrOfMonth = function()
{
	var week = this.getWeekNumber();
	var monthstart = this.getTime() - (this.getDate()-1) * 24 * 60 * 60 * 1000;
	var monthDate = new Date(monthstart);
	var monthstartweek = monthDate.getWeekNumber();
	return week - monthstartweek + 1; 
}

/**
 * Helper function to convert a year to a full year
 */
Date.prototype.y2k = function(year) 
{ 
	return (year < 1000) ? year + 1900 : year;
}

/**
 * Function which sets the day, month and year. It sets the hours, minutes,
 * seconds and miliseconds to 0 (zero). 
 * @param integer day the day
 * @param integer month the month
 * @param integer year the full year (2006)
 * @return integer time stamp  
 */  
Date.prototype.setTimeStamp = function(day, month, year)
{
	this.setTime(new Date(year, month-1, day).getTime());

	return this.getTime();
}

/**
 * Function will return the time as "07:05"
 * @return string 
 */
Date.prototype.toTime = function()
{
	var hours = this.getHours()
	var minutes = this.getMinutes();
	return (hours<10?"0":"")+hours+":"+(minutes<10?"0":"")+minutes; 
}
/**
 * Function will return the date as "28-02-2006"
 * @return string 
 */ 
Date.prototype.toDate = function()
{
	var date = this.getDate();
	var month = this.getMonth()+1;//month start at 0
	var year = this.getFullYear();
	return (date<10?"0":"")+date+"-"+(month<10?"0":"")+month+"-"+year;
}

Date.prototype.toDateTime = function()
{
	return this.toDate()+" "+this.toTime();
}

/**
 * Function will return the first day of a week
 * @return Date
 * 
 * TODO: week can also start on sunday, check settings
 * TODO: check DST
 */	 
Date.prototype.getStartDateOfWeek = function()
{
	var day = this.getDay();
	if (day == 0){
		day = 7;
	}
	return new Date (this-((day-1)*ONE_DAY));//Monday
	//return new Date (this-(day*ONE_DAY));//Sunday
}

/**
 * Function will check "day" == same day as this object
 * @param date
 * @return Bool   
 */ 
Date.prototype.isSameDay = function(day)
{
	return 	this.getDate() == day.getDate() && 
			this.getMonth() == day.getMonth() && 
			this.getYear() == day.getYear();
}

/**
 * Function will convert "14:13" -> "14:30" or "14:32 " -> "15:00"
 * @return int: time in milliseconds 
 */
Date.prototype.ceilHalfhour = function()
{
	return this.ceilMinutes(30).getTime();
}
/**
 * Function will convert "14:13" -> "14:00" or "14:32 " -> "14:30"
 * @return int: time in milliseconds 
 */
Date.prototype.floorHalfhour = function()
{
	return this.floorMinutes(30).getTime();
}

/**
 * Function to ceil timings according to the passed ceil minutes.
 * @param date ceilTimeValue date time which needs to be ceil (5/10/15/30/60 or so on)
 * @param Boolean checkForDSTChange When set to true it will check whether the new time is not sooner than the original time due to any DST changes
 * @return number Time number which is unixtimestamp of time.
 *
 * Example to understand what the code is actually suppose to do.
 *	9:12	5min		ceil-9:15
 *			10min		ceil-9.20
 *			15min		ceil-9.15
 *			30min		ceil-9.30
 *			1hr/60min	ceil-10.00
 *
 */ 
Date.prototype.ceilMinutes = function(ceilTimeValue, checkForDSTChange){
	var originalTime = this.getTime();

	var minutes = parseInt(this.getMinutes(),10);

	// when time is XX:00 then there is no need to ceil the time.
	if(minutes % ceilTimeValue > 0)
		this.setMinutes((minutes - (minutes % ceilTimeValue)) + ceilTimeValue);

	/* DST fix:
	 * When the item ends at the end of the day the it will be set to 23:59. When the above line
	 * of code rounds the minutes up it will set the time to 0:00 the next day. If at that time 
	 * a DST change happens (like the Brazilian DST change) the time gets set to 23:00. In that
	 * case it will set the time to before the original time. If that is the case the original time 
	 * is set again.
	 */
	if(checkForDSTChange && this.getTime() < originalTime){
		this.setTime(originalTime);
	}

	//Date and Time values is build using the current time which includes seconds and milliseconds as well
	//so to roundoff the timestamp value completely, set sec and ms to zero.
	this.setSeconds(0);
	this.setMilliseconds(0);
	return this;
}

/**
 * Function to floor timings according to the passed floor minutes.
 * @param date floorTimeValue date time which needs to be floor (5/10/15/30/60 or so on)
 * @return number Time number which is unixtimestamp of time.
 *
 * Example to understand what the code is actually suppose to do.
 *	9:12	5min		floor-9.10
 *			10min		floor-9.10
 *			15min		floor-9.00
 *			30min		floor-9.00
 *			1hr/60min	floor-9.00
 *
 */ 
Date.prototype.floorMinutes = function(floorTimeValue){
	var minutes = parseInt(this.getMinutes(),10);
	this.setMinutes(minutes - (minutes % floorTimeValue));
	return this;
}

Date.prototype.getMonthTranslation = function()
{
	return MONTHS[this.getMonth()];
}
Date.prototype.getMonthShortTranslation = function()
{
	return MONTHS_SHORT[this.getMonth()];
}

Date.prototype.getDayTranslation = function()
{
	return DAYS[this.getDay()];
}
Date.prototype.getDayShortTranslation = function()
{
	return DAYS_SHORT[this.getDay()];
}

/**
 * Attempts to clear all time information from this Date by setting the time to midnight of the same day,
 * automatically adjusting for Daylight Saving Time (DST) where applicable.
 * (note: DST timezone information for the browser's host operating system is assumed to be up-to-date)
 * @param {Boolean} clone true to create a clone of this date, clear the time and return it (defaults to false).
 * @return {Date} this or the clone.
 */
Date.prototype.clearTime = function(clone) {
	// get current date before clearing time
	var d = this.getDate();

	// clear time
	this.setHours(0);
	this.setMinutes(0);
	this.setSeconds(0);
	this.setMilliseconds(0);

	if (this.getDate() != d) { // account for DST (i.e. day of month changed when setting hour = 0)
		// note: DST adjustments are assumed to occur in multiples of 1 hour (this is almost always the case)
		// refer to http://www.timeanddate.com/time/aboutdst.html for the (rare) exceptions to this rule

		// increment hour until cloned date == current date
		for (var hr = 1, c = this.add(Date.HOUR, hr); c.getDate() != d; hr++, c = this.add(Date.HOUR, hr));

		this.setDate(d);
		this.setHours(c.getHours());
	}

	return this;
}

/**
 * Provides a convenient method for performing basic date arithmetic. This method
 * does not modify the Date instance being called - it creates and returns
 * a new Date instance containing the resulting date value.
 *
 * Examples:
 * <pre><code>
	// Basic usage:
	var dt = new Date('10/29/2006').add(Date.DAY, 5);
	document.write(dt); //returns 'Fri Nov 03 2006 00:00:00'

	// Negative values will be subtracted:
	var dt2 = new Date('10/1/2006').add(Date.DAY, -5);
	document.write(dt2); //returns 'Tue Sep 26 2006 00:00:00'

	// You can even chain several calls together in one line:
	var dt3 = new Date('10/1/2006').add(Date.DAY, 5).add(Date.HOUR, 8).add(Date.MINUTE, -30);
	document.write(dt3); //returns 'Fri Oct 06 2006 07:30:00'
 * </code></pre>
 *
 * Furthermore, changes to {@link Date#HOUR hours}, {@link Date#MINUTE minutes},
 * {@link Date#SECOND seconds} and {@link Date#MILLI milliseconds} are treated more accurately
 * regarding DST changes then the {@link Date#DAY days}, {@link Date#MONTH months} and {@link Date#YEAR years}
 * changes. When changing the time the standard is applied, which means that if the DST kicks in at 2AM,
 * and the time becomes 3AM. Doing new Date('Mar 25 2012 01:00').add(Date.HOUR, 1) will be 'Mar 25 2012 03:00'.
 * However when changing the date, we will use the JS behavior, which means that
 * new Date('Mar 24 2012 02:00').add(Date.DAY, 1) could become 'Mar 25 2012 01:00' as JS will not correctly
 * move the time correctly passed the DST switch.
 *
 * @param {String} interval A valid date interval enum value.
 * @param {Number} value The amount to add to the current date.
 * @return {Date} The new Date instance.
 */
Date.prototype.add = function(interval, value)
{
	// Clone Date
	var d = new Date(this.getTime());
	if (!interval || value === 0) {
		return d;
	}

	switch(interval.toLowerCase()) {
		// Changing the time is done more accuretely then
		// changing the date. This is because we have to work
		// around DST issues (which we don't care for when
		// changing the day). In JS, we have the following
		// scenario at the following date: Mar 25 2012.
		// At 2:00:00 the DST kicks in and the time will be
		//     Mar 25 2012 03:00:00
		// However, when using setMilliseconds, setSeconds,
		// setMinutes or setHours, JS decides to wrap back
		// to:
		// 	Mar 25 2012 01:00:00
		// How can this go wrong, take the following date:
		//      a = new Date('Mar 25 2012 01:45:00')
		// add 30 minutes to it
		//      a.setMinutes(a.getMinutes() + 30)
		// we expect the time to be 03:15:00 however JS
		// decides to fall back to 01:15:00.
		// To fix this correctly, we have to work using timestamps
		// as JS is able to correctly step over the DST switch.
		case Date.HOUR:
			// Convert value to minutes
			value *= 60;
		case Date.MINUTE:
			// Convert value to seconds
			value *= 60;
		case Date.SECOND:
			// Convert value to milliseconds
			value *= 1000;
		case Date.MILLI:
			d = new Date(d.getTime() + value);
			break;
		// Changing the date is done with less accuracy,
		// basically we don't care if we come at exactly
		// the same time as before. If the JS decides to
		// perform weird tricks, then so be it.
		case Date.DAY:
			d.setDate(this.getDate() + value);
			break;
		case Date.MONTH:
			var day = this.getDate();
			if (day > 28) {
				day = Math.min(day, this.getFirstDateOfMonth().add(Date.MONTH, value).getLastDateOfMonth().getDate());
			}
			d.setDate(day);
			d.setMonth(this.getMonth() + value);
			break;
		case Date.YEAR:
			d.setFullYear(this.getFullYear() + value);
			break;
	}
	return d;
}

Date.MILLI = 'milli';
Date.SECOND = 'second';
Date.MINUTE = 'minute';
Date.HOUR = 'hour';
Date.DAY = 'day';
Date.MONTH = 'month';
Date.YEAR = 'year';

/**
 * Get the date of the first day of the month in which this date resides.
 * @return {Date}
 */
Date.prototype.getFirstDateOfMonth = function() {
	return new Date(this.getFullYear(), this.getMonth(), 1);
}

/**
 * Get the date of the last day of the month in which this date resides.
 * @return {Date}
 */
Date.prototype.getLastDateOfMonth = function() {
	return new Date(this.getFullYear(), this.getMonth(), this.getDaysInMonth());
}


/**
 * Function will set the time of timestamp on zero "12-jan-06 14:12" -> "12-jan-06 0:0"
 * @return int: unixtimestamp   
 */
function timeToZero(unixtimestamp)
{
	var tempDate = new Date(unixtimestamp*1000);
	tempDate.setHours(0);
	tempDate.setMinutes(0);
	tempDate.setSeconds(0);
	return parseInt(Math.floor(tempDate.getTime()/1000),10);
}

/**
 * Function will convert time "1:40" to seconds "6000" 
 * If time format is in AM/PM format then it converts "1:40 PM" to "49200" sedonds
 */
function timeToSeconds(value)
{
	// Get current date.
	var tempDate = new Date();
	/**
	 * For using parseDate function for parsing date in am/pm or 24hr format one should have complete Date string, 
	 * here only time values is passed to the function so create one dummy Date object,
	 * and using dummy object and time from the form parameter, use parseDate function and let it do all formating work.
	 * parseDate function will return a new Date object, using it get timeInSeconds
	 * and Using new Date object get the value
	 */
	// parseDate function works with Date value so create current datetime string
	var tempDateTimeStr = tempDate.getDate() + "-" + (tempDate.getMonth()+1) + "-" + tempDate.getFullYear() + " " + value;

	// Set the timeformat for parsing.
	var format = "%d-%m-%Y "+_("%H:%M");
	// Get the date object for specific hour and minutes
	var newDate = Date.parseDate(tempDateTimeStr, format, false);
	var timeInSeconds = ((newDate.getHours() * 60) + newDate.getMinutes()) * 60;

	return timeInSeconds;
}

/**
 * Function will convert seconds "6000" to time "1:40"
 * If time format is in AM/PM format then it converts "49200" seconds to "1:40 PM"
 * @param value = int time in seconds
 * @param fourDigit = boolean true for "01:40" or false for "1:40"  
 */
function secondsToTime(value,fourDigit)
{
	// Get current Date and time and set its time to zero
	var tempDate = new Date();
	// using dst safe function for time manipulation.
	value = addHoursToUnixTimeStamp(timeToZero(tempDate.getTime()/1000) , (value / (60*60)));

	// Add time in to today's time stamp and return it.
	result = strftime(_('%H:%M'),value).toUpperCase();
	return result
}

/**
 * Date.strftime() 
 * string format ( string format )
 * Formatting rules according to http://php.net/strftime
 *
 * Copyright (C) 2006  Dao Gottwald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Contact information:
 *   Dao Gottwald  <dao at design-noir.de>
 *
 * Changed by M. Erkens for use with Zarafa WebAccess
 *  - use translation from Zarafa WebAccess
 *
 * @version  0.7 (changed)
 * @url      http://design-noir.de/webdev/JS/Date.format/
 */
Date._pad = function(num, len) {
	for (var i = 1; i <= len; i++)
		if (num < Math.pow(10, i))
			return new Array(len-i+1).join(0) + num;
	return num;
}

Date.prototype.strftime = function(format) {
	if (format.indexOf('%%') > -1) { // a literal `%' character
		format = format.split('%%');
		for (var i = 0; i < format.length; i++)
			format[i] = this.format(format[i]);
		return format.join('%');
	}
	format = format.replace(/%D/g, '%m/%d/%y'); // same as %m/%d/%y
	format = format.replace(/%r/g, '%I:%M:%S %p'); // time in a.m. and p.m. notation
	format = format.replace(/%R/g, '%H:%M:%S'); // time in 24 hour notation
	format = format.replace(/%T/g, '%H:%M:%S'); // current time, equal to %H:%M:%S
	format = format.replace(/%x/g, _("%d-%m-%Y")); // preferred date representation for the current locale without the time
	format = format.replace(/%X/g, _("%H:%M")); // preferred time representation for the current locale without the date
	var dateObj = this;
	return format.replace(/%([aAbhBcCdegGHIjmMnpStuUVWwyYzZ])/g, function(match0, match1) {
		return dateObj.format_callback(match0, match1);
	});
}

Date.prototype.format_callback = function(match0, match1) {
	switch (match1) {
		case 'a': // abbreviated weekday name according to the current locale
			return DAYS_SHORT[this.getDay()];
		case 'A': // full weekday name according to the current locale
			return DAYS[this.getDay()];
		case 'b':
		case 'h': // abbreviated month name according to the current locale
			return MONTHS_SHORT[this.getMonth()];
		case 'B': // full month name according to the current locale
			return MONTHS[this.getMonth()];
		case 'c': // preferred date and time representation for the current locale
			return this.toLocaleString();
		case 'C': // century number (the year divided by 100 and truncated to an integer, range 00 to 99)
			return Math.floor(this.getFullYear() / 100);
		case 'd': // day of the month as a decimal number (range 01 to 31)
			return Date._pad(this.getDate(), 2);
		case 'e': // day of the month as a decimal number, a single digit is preceded by a space (range ' 1' to '31')
			return Date._pad(this.getDate(), 2);
		/*case 'g': // like %G, but without the century
			return ;
		case 'G': // The 4-digit year corresponding to the ISO week number (see %V). This has the same format and value as %Y, except that if the ISO week number belongs to the previous or next year, that year is used instead
			return ;*/
		case 'H': // hour as a decimal number using a 24-hour clock (range 00 to 23)
			return Date._pad(this.getHours(), 2);
		case 'I': // hour as a decimal number using a 12-hour clock (range 01 to 12).But displays 12 am as 00 am.
			var hour = Date._pad(this.getHours() % 12, 2);
			return (hour != "00")?hour: (this.getHours() == "00")?"00":"12";
		case 'j': // day of the year as a decimal number (range 001 to 366)
			return Date._pad(this.getMonth() * 30 + Math.ceil(this.getMonth() / 2) + this.getDay() - 2 * (this.getMonth() > 1) + (!(this.getFullYear() % 400) || (!(this.getFullYear() % 4) && this.getFullYear() % 100)), 3);
		case 'm': // month as a decimal number (range 01 to 12)
			return Date._pad(this.getMonth() + 1, 2);
		case 'M': // minute as a decimal number
			return Date._pad(this.getMinutes(), 2);
		case 'n': // newline character
			return '\n';
		case 'p': // either `am' or `pm' according to the given time value, or the corresponding strings for the current locale
			return this.getHours() < 12 ? 'am' : 'pm';
		case 'S': // second as a decimal number
			return Date._pad(this.getSeconds(), 2);
		case 't': // tab character
			return '\t';
		case 'u': // weekday as a decimal number [1,7], with 1 representing Monday
			return this.getDay() || 7;
		/*case 'U': // week number of the current year as a decimal number, starting with the first Sunday as the first day of the first week
			return ;
		case 'V': // The ISO 8601:1988 week number of the current year as a decimal number, range 01 to 53, where week 1 is the first week that has at least 4 days in the current year, and with Monday as the first day of the week. (Use %G or %g for the year component that corresponds to the week number for the specified timestamp.)
			return ;
		case 'W': // week number of the current year as a decimal number, starting with the first Monday as the first day of the first week
			return ;*/
		case 'w': // day of the week as a decimal, Sunday being 0
			return this.getDay();
		case 'y': // year as a decimal number without a century (range 00 to 99)
			return this.getFullYear().toString().substr(2);
		case 'Y': // year as a decimal number including the century
			return this.getFullYear();
		/*case 'z':
		case 'Z': // time zone or name or abbreviation
			return ;*/
		default:
			return match0;
	}
}

Date.prototype.strftime_gmt = function(format)
{
	var timestamp = this.getTime();
	timestamp += this.getTimezoneOffset()*60000;
	
	var gmtDate = new Date(timestamp);
	return gmtDate.strftime(format);
}

/**
* returns a string with the difference between two dates in a simple form
*
* otherDate: the second date object, to which 'this' is compared
* futureString: string to be used when otherDate is in the future compared to this
* pastString: string to be used when pastString is in the past compared to this
*
* Note for futureString and pastString: When these are set and not an empty string they are used as described.
*     when they contain '%s' this is replaced by the difference in time.
*
* Example:
*   alert(date1.simpleDiffString(date2, "", "%s overdue"));
*/
Date.prototype.simpleDiffString = function(otherDate, futureString, pastString)
{
	var result = false;

	var time1 = this.getTime();
	var time2 = otherDate.getTime();

	var diff = time1 - time2;
	
	var shellString = "%s";
	if (diff>=0 && typeof(futureString)=="string" && futureString!=""){
		shellString = futureString;
	}else if (diff<0 && typeof(pastString)=="string" && pastString!=""){
		shellString = pastString;
	}
	
	// get diff in minutes
	diff = Math.round(Math.abs(diff)/60000);
	if (diff<=1)
		result = _("now");
	if (!result && diff<59)
		result = _("%d minutes").sprintf(diff);

	// now convert diff in hours
	diff = Math.round(diff/60);
	if (!result && diff<=1)
		result = _("1 hour");
	if (!result && diff<=23)
		result = _("%d hours").sprintf(diff);

	// diff in days
	diff = Math.round(diff/24);
	if (!result && diff<=1)
		result = _("1 day");
	if (!result && diff<=13)
		result = _("%d days").sprintf(diff);
	
	// diff in weeks
	diff = Math.floor(diff/7);
	if (!result && diff<=1)
		result = _("1 week");
	if (!result)
		result = _("%d weeks").sprintf(diff);
	
	return shellString.sprintf(result);
}

/**
 * Get the next given weekday starting from this date. If the current date is this weekday,
 * then the current day will be returned.
 * @param {Number} weekday The day in the week to skip to (0: Sunday -  6: Saturday). If
 * not given, tomorrow will be returned.
 * @return {Date} this or the clone
 */
Date.prototype.getNextWeekDay = function(weekday)
{
	var currentday = this.getDay();

	if (typeof weekday === 'undefined') {
		return this.add(Date.DAY, 1);
	} else if (weekday < currentday) {
		return this.add(Date.DAY, 7 - (currentday - weekday));
	} else {
		return this.add(Date.DAY, weekday - currentday);
	}
}

/**
 * Get the previous given weekday starting from this date. If the current date is this weekday,
 * then the current day will be returned.
 * @param {Number} weekday The day in the week to skip to (0: Sunday -  6: Saturday). If
 * not given, yesterday will be returned.
 * @return {Date} this or the clone
 */
Date.prototype.getPreviousWeekDay = function(weekday)
{
	var currentday = this.getDay();

	if (typeof weekday === 'undefined') {
		return this.add(Date.DAY, -1);
	} else if (weekday <= currentday) {
		return this.add(Date.DAY, weekday - currentday);
	} else {
		return this.add(Date.DAY, -7 + (weekday - currentday));
	}
}


/**
* Returns a human readable string for duration property
* @param		duration		number		duration in minutes
* @return		result			string		human readable duration string
* 
* @TODO this function is not much accurate as compared to OL
*/
function simpleDurationString(duration) {
	var result = false;

	if(duration.constructor != Number || isNaN(duration)) {
		// invalid argument
		return "";
	}

	// now convert duration in minutes
	duration = Math.round(duration);
	if (duration <= 59)
		result = _("%d mins").sprintf(duration);

	// now convert duration in hours
	duration = duration / 60;
	if (!result && duration <= 1)
		result = _("1 hour");
	if (!result && duration <= 23)
		result = _("%.1f hours").sprintf(duration);

	// diff in days
	duration = duration / 24;
	if (!result && duration <= 1)
		result = _("1 day");
	if (!result && duration <= 13)
		result = _("%.1f days").sprintf(duration);

	// diff in weeks
	duration = duration / 7;
	if (!result && duration <= 1)
		result = _("1 week");
	if (!result)
		result = _("%.1f weeks").sprintf(duration);

	return result;
}

function strftime(format, timestamp) 
{
	var t = new Date();
	if (typeof(timestamp) != "undefined")
		t.setTime(parseInt(timestamp)*1000);

	return t.strftime(format);
}

function strftime_gmt(format,timestamp) {
	var t = new Date();
	if (typeof(timestamp) != "undefined")
		t.setTime(parseInt(timestamp)*1000);

	return t.strftime_gmt(format);
}

/**
 * global function to add number of days in unixtimestamp (dst/timezone safe)
 * @param	integer		baseUnixTimestamp	timestamp to use for adding days
 * @param	integer		dayCount			number of days to add
 * @return	integer							timestamp after adding day
 */
function addDaysToUnixTimeStamp(baseUnixTimeStamp, dayCount) {
	var dateObj = new Date(baseUnixTimeStamp * 1000);
	dateObj.addDays(dayCount);

	return (dateObj.getTime() / 1000);
}

/**
 * global function to add number of hours in unixtimestamp (dst/timezone safe)
 * @param	integer		baseUnixTimestamp	timestamp to use for adding days
 * @param	integer		hourCount			number of hours to add
 * @return	integer							timestamp after adding day
 */
function addHoursToUnixTimeStamp(baseUnixTimeStamp, hourCount) {
	var dateObj = new Date(baseUnixTimeStamp * 1000);
	dateObj.addHours(hourCount);

	return (dateObj.getTime() / 1000);
}

/**
 * global function to add unixtimestamp to unixtimestamp (dst/timezone safe)
 * @param	integer		baseUnixTimestamp	timestamp to use for adding another timestamp
 * @param	integer		unixTimeStampToAdd	timestamp to add
 * @return	integer							timestamp after adding another timestamp
 */
function addUnixTimestampToUnixTimeStamp(baseUnixTimeStamp, unixTimeStampToAdd) {
	var dateObj = new Date(baseUnixTimeStamp * 1000);
	dateObj.addUnixTimeStampToDate(unixTimeStampToAdd);

	return (dateObj.getTime() / 1000);
}

/**
 * global function to get date range of a particular week from week number
 * @param	integer		weekNo		week number of week
 * @return	Object					timestamp of start and end date of week
 * @TODO check DST
 */
function getDateRangeOfWeek(weekNo) {
	var currentDate = new Date();
	var result = new Object();

	if(typeof weekNo == "undefined" || weekNo === null || weekNo === false) {
		weekNo = currentDate.getWeekNumber();
	}

	// set current day as monday
	var numOfDaysPastSinceLastMonday = currentDate.getDay() - 1;
	currentDate.addDays(-numOfDaysPastSinceLastMonday);

	// find difference between current week number and supplied week number
	var currentWeekNo = currentDate.getWeekNumber();
	var weeksInTheFuture = weekNo - currentWeekNo;

	// add days difference to current week
	if(weeksInTheFuture !== 0) {
		currentDate.addDays(7 * weeksInTheFuture);
	}
	result["week_start_date"] = timeToZero(currentDate.getTime() / 1000);

	// add 6 days to get end of week
	currentDate.addDays(6);
	result["week_end_date"] = timeToZero(currentDate.getTime() / 1000);

	return result;
}

/**
 * global function to get date validation.
 * @param integer day of the input date 
 * @param integer month the input date. 
 * @param integer year the input date.
 * @return boolean true if the date is a valid date else false.
 */
function checkDateValidation(day, month, year)
{
	var startYearRange = 1900; // least year to consider
	var endYearRange = 2100; // most year to consider
	
	if (month < 1 || month > 12) return false;
	if (day < 1 || day > 31) return false;
	if (year < startYearRange || year > endYearRange) return false;
	//Check for months which have 30days like april,may,etc.
	if ((month == 4 || month == 6 || month == 9 || month == 11) && (day == 31)) return false;
	// Check for Feburary month, February has 29 days in any year evenly divisible by four,
    // EXCEPT for centurial years which are not also divisible by 400
	if (month == 2)
	{	
		var leapyear = parseInt(year/4,10);
		if (isNaN(leapyear)) return false;
		if (day > 29) return false;
		if (day == 29 && ((year/4) != parseInt(year/4,10))) return false;
	}
	return true;
}

// global function to getTimezone and all dst props
// This is a hard one. To create a recurring appointment, we need to save
// the start and end time of the appointment in local time. So if I'm in 
// GMT+8, and I want the appointment at 9:00, I will simply save 9*60 = 540
// in the startDate. To make this usable for other users in other timezones,
// we have to tell the server in which timezone this is. The timezone is normally
// defined as a startdate and enddate for DST, the offset in minutes (so GMT+2 is 120)
// plus the extra DST offset when DST is in effect. 
//
// We can't retrieve this directly from the browser, so we assume that the DST change
// will occure on a Sunday at 2:00 or 3:00 AM, and simply scan all the sundays in a
// year, looking for changes. We then have to guess which bit is DST and which is 'normal'
// by assuming that the DST offset will be less than the normal offset. From this we
// calculate the start and end dates of DST and the actuall offset in minutes.
//
// Unfortunately we can't detect the difference between 'the last week of october' and
// 'the fourth week of october'. This can cause subtle problems, so we assume 'last week'
// because this is most prevalent.
// 
// Note that this doesn't work for many strange DST changes, see 
// http://webexhibits.org/daylightsaving/g.html

function getTimeZone()
{
    var tzswitch = new Array();
    var n = 0;
    var lastoffset;
    
    var t = new Date();
    
    var tz = new Object();
    
    t.setDate(1);
    t.setMonth(0);
    // Look up the first sunday of this year
    t = t.getNextWeekDay(0);

    t.setHours(5);
    // Use 5:00 am because any change should have happened by then
    t.setMinutes(0);
    t.setSeconds(0);
    t.setMilliseconds(0);
    
    lastoffset = t.getTimezoneOffset();
    
    for(i=0;i<52;i++) {
        if(t.getTimezoneOffset() != lastoffset) {
            // Found a switch
            tzswitch[n] = new Object;
            tzswitch[n].switchweek = t.getWeekNrOfMonth();
            tzswitch[n].switchmonth = t.getMonth();
            tzswitch[n].offset = t.getTimezoneOffset();
            n++;
            
            // We assume DST is only set or removed once per year
            if(n == 2)
                break;
                
            lastoffset = t.getTimezoneOffset();
        }
        
        // advance one week
        t.setTime(t.getTime() + 7 * 24 * 60 * 60 * 1000);
    }
    
    if(n == 0) {
        // No DST in this timezone
        tz.timezone = t.getTimezoneOffset();
        tz.timezonedst = 0;
        tz.dststartday = 0;
        tz.dststartweek = 0;
        tz.dststartmonth = 0;
        tz.dststarthour = 0;
        tz.dstendday = 0;
        tz.dstendweek = 0;
        tz.dstendmonth = 0;
        tz.dstendhour = 0;
        return tz;
    } else if(n == 1) {
        // This should be impossible unless DST started somewhere in the year 2000
        // and ended more than a year later. This is an error.
        return;
    } else if(n == 2) {
        if(tzswitch[0].offset < tzswitch[1].offset) {
            // Northern hemisphere
            tz.timezone = tzswitch[1].offset;
            tz.timezonedst = tzswitch[0].offset - tzswitch[1].offset;
            tz.dststartday = 0; // assume sunday
            tz.dststartweek = tzswitch[0].switchweek == 4 ? 5 : tzswitch[0].switchweek; // assume 'last' week if week = 4
            tz.dststartmonth = tzswitch[0].switchmonth + 1;
            tz.dststarthour = 2; // Start at 02:00 AM
            tz.dstendday = 0;
            tz.dstendweek = tzswitch[1].switchweek == 4 ? 5 : tzswitch[1].switchweek;
            tz.dstendmonth = tzswitch[1].switchmonth + 1;
            tz.dstendhour = 3;
            return tz;
            
        } else {
            // Southern hemisphere
            tz.timezone = tzswitch[0].offset;
            tz.timezonedst = tzswitch[1].offset - tzswitch[0].offset;
            tz.dststartday = 0; // assume sunday
            tz.dststartweek = tzswitch[1].switchweek == 4 ? 5 : tzswitch[1].switchweek; // assume 'last' week if week = 4
            tz.dststartmonth = tzswitch[1].switchmonth + 1;
            tz.dststarthour = 2; // Start at 02:00 AM
            tz.dstendday = 0;
            tz.dstendweek = tzswitch[0].switchweek == 4 ? 5 : tzswitch[0].switchweek;
            tz.dstendmonth = tzswitch[0].switchmonth + 1;
            tz.dstendhour = 3;
            return tz;
        }
    } else {
        // Multi-DST timezone ? This is also an error.
        return;
    }
}