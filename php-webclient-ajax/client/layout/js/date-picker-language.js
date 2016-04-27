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

// ** I18N

// Calendar EN language
// Author: Mihai Bazon, <mihai_bazon@yahoo.com>
// Encoding: any
// Distributed under the same terms as the calendar itself.

// For translators: please use UTF-8 if possible.  We strongly believe that
// Unicode is the answer to a real internationalized world.  Also please
// include your contact information in the header, as can be seen above.

// full day names
Calendar._DN = new Array
(_("Sunday"),
 _("Monday"),
 _("Tuesday"),
 _("Wednesday"),
 _("Thursday"),
 _("Friday"),
 _("Saturday"),
 _("Sunday"));

// Please note that the following array of short day names (and the same goes
// for short month names, _SMN) isn't absolutely necessary.  We give it here
// for exemplification on how one can customize the short day names, but if
// they are simply the first N letters of the full name you can simply say:
//
//   Calendar._SDN_len = N; // short day name length
//   Calendar._SMN_len = N; // short month name length
//
// If N = 3 then this is not needed either since we assume a value of 3 if not
// present, to be compatible with translation files that were written before
// this feature.

// short day names
Calendar._SDN = new Array
(_("Sun"),
 _("Mon"),
 _("Tue"),
 _("Wed"),
 _("Thu"),
 _("Fri"),
 _("Sat"),
 _("Sun"));

// First day of the week. "0" means display Sunday first, "1" means display
// Monday first, etc.
Calendar._FD = 1;

// full month names
Calendar._MN = new Array
(_("January"),
 _("February"),
 _("March"),
 _("April"),
 _("May"),
 _("June"),
 _("July"),
 _("August"),
 _("September"),
 _("October"),
 _("November"),
 _("December"));

// short month names
Calendar._SMN = new Array
(_("Jan"),
 _("Feb"),
 _("Mar"),
 _("Apr"),
 _("May"),
 _("Jun"),
 _("Jul"),
 _("Aug"),
 _("Sep"),
 _("Oct"),
 _("Nov"),
 _("Dec"));

// tooltips
Calendar._TT = {};
Calendar._TT["INFO"] = "About the calendar";

Calendar._TT["ABOUT"] =
"DHTML Date/Time Selector\n" +
"(c) dynarch.com 2002-2005 / Author: Mihai Bazon\n" + // don't translate this this ;-)
"For latest version visit: http://www.dynarch.com/projects/calendar/\n" +
"Distributed under GNU LGPL.  See http://gnu.org/licenses/lgpl.html for details." +
"\n\n" +
"Date selection:\n" +
"- Use the \xab, \xbb buttons to select year\n" +
"- Use the " + String.fromCharCode(0x2039) + ", " + String.fromCharCode(0x203a) + " buttons to select month\n" +
"- Hold mouse button on any of the above buttons for faster selection.";
Calendar._TT["ABOUT_TIME"] = "\n\n" +
"Time selection:\n" +
"- Click on any of the time parts to increase it\n" +
"- or Shift-click to decrease it\n" +
"- or click and drag for faster selection.";

Calendar._TT["PREV_YEAR"] = _("Prev year")+" ("+_("hold for menu")+")";
Calendar._TT["PREV_MONTH"] = _("Prev month")+" ("+_("hold for menu")+")";
Calendar._TT["GO_TODAY"] = _("Go Today");
Calendar._TT["NEXT_MONTH"] = _("Next month")+" ("+_("hold for menu")+")";
Calendar._TT["NEXT_YEAR"] = _("Next year")+" ("+_("hold for menu")+")";
Calendar._TT["SEL_DATE"] = _("Select date");
Calendar._TT["DRAG_TO_MOVE"] = _("Drag to move");
Calendar._TT["PART_TODAY"] = " ("+_("today")+")";

// the following is to inform that "%s" is to be the first day of week
// %s will be replaced with the day name.
Calendar._TT["DAY_FIRST"] = _("Display %s first");

// This may be locale-dependent.  It specifies the week-end days, as an array
// of comma-separated numbers.  The numbers are from 0 to 6: 0 means Sunday, 1
// means Monday, etc.
Calendar._TT["WEEKEND"] = "0,6";

Calendar._TT["CLOSE"] = _("Close");
Calendar._TT["TODAY"] = _("Today");
Calendar._TT["TIME_PART"] = _("(Shift-)Click or drag to change value");

// date formats
Calendar._TT["DEF_DATE_FORMAT"] = "%Y-%m-%d";
Calendar._TT["TT_DATE_FORMAT"] = "%a, %b %e";

Calendar._TT["WK"] = _("wk");
Calendar._TT["TIME"] = _("Time")+":";
