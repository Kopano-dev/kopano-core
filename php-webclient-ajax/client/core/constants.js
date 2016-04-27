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
 * This file contains only some constants used anyware in the WebApp
 */

/**
*  Possible layouts in the 'main' area:
*  - BORDERLAYOUT --> only one element in main area and uses the full width and height of main (DEFAULT).
*  - FLOWLAYOUT   --> elements will be shown next to each other, each element has the same width and height.
*  - GRIDLAYOUT   --> elements will be shown in a table. Example:
*                     -- main -------------------------------
*                     | --element-- --element-- --element-- |
*                     | |         | |         | |         | |
*                     | |         | |         | |         | |
*                     | ----------- ----------- ----------- |
*                     | --element-- --element-- --element-- |
*                     | |         | |         | |         | |
*                     | |         | |         | |         | |
*                     | ----------- ----------- ----------- |
*                     ---------------------------------------
*
*  - BOXLAYOUT     --> elements will be shown below each ohter, each elements has the same width and height.
* 
* The left and the right area will have the 'BOXLAYOUT'. This can't be changed, because of
* the small width of each, left and right, area.          
*/
var BORDER_LAYOUT = 0;
var FLOW_LAYOUT = 1;
var GRID_LAYOUT = 2;
var BOX_LAYOUT = 3;

/**
*  Possible ways to insert a new element:
*  - INSERT_ELEMENT_AT_TOP      --> element will be inserted at the top of the area.
*  - INSERT_ELEMENT_AT_BOTTOM   --> element will be inserted at the bottom of the area (DEFAULT).
*  - INSERT_ELEMENT_BETWEEN     --> element will be inserted between the current elements in the area.  
*/
var INSERT_ELEMENT_AT_TOP = 0;
var INSERT_ELEMENT_AT_BOTTOM = 1;
var INSERT_ELEMENT_BETWEEN = 2;

/**
* Delete all elements and modules in an area.
*/ 
var RESET_AREA = true;

/**
* Determines if an appointment can be resized.
*/
var APPOINTMENT_RESIZABLE = true; 
var APPOINTMENT_NOT_RESIZABLE = false;

/**
 * Used with Date
 */
var MONTHS = new Array(_("January"), _("February"), _("March"), _("April"), _("May"), _("June"), _("July"), _("August"), _("September"), _("October"), _("November"), _("December"));
var MONTHS_SHORT = new Array(_("Jan"), _("Feb"), _("Mar"), _("Apr"), _("May"), _("Jun"), _("Jul"), _("Aug"), _("Sep"), _("Oct"), _("Nov"), _("Dec"));
var DAYS = new Array(_("Sunday"), _("Monday"), _("Tuesday"), _("Wednesday"), _("Thursday"), _("Friday"), _("Saturday"));
var DAYS_SHORT = new Array(_("Sun"), _("Mon"), _("Tue"), _("Wed"), _("Thu"), _("Fri"), _("Sat"));

// Please note that using ONE_DAY isn't timezone/DST safe! use Date.addDays() for calculations
var ONE_DAY = 86400000;
// Please note that using ONE_HOUR isn't timezone/DST safe! use Date.addHours() for calculations
var ONE_HOUR = 3600000;
var HALF_HOUR = 1800000;

// this string is equivalent with the html tag &nbsp; for use with textNodes
var NBSP = "\u00a0";
// carriage return line feed, for new line character
var CRLF = "\u000D\u000A";

// used with fields/columns to specify a autosize column
var PERCENTAGE = "percentage";

// used for access rights
var ecRightsNone			=		0x00000000;
var ecRightsReadAny			=		0x00000001;
var ecRightsCreate			=		0x00000002;
var ecRightsEditOwned		=		0x00000008;
var ecRightsDeleteOwned		=		0x00000010;
var ecRightsEditAny			=		0x00000020;
var ecRightsDeleteAny		=		0x00000040;
var ecRightsCreateSubfolder	=		0x00000080;
var ecRightsFolderAccess	=		0x00000100;
//var ecRightsContact		=		0x00000200;
var ecRightsFolderVisible	=		0x00000400;

var ecRightsAll				=		0x000005FB;
var ecRightsFullControl		=		0x000004FB;
var ecRightsDefault			=		ecRightsNone | ecRightsFolderVisible;
var ecRightsDefaultPublic	=		ecRightsReadAny | ecRightsFolderVisible;
var ecRightsAdmin			=		0x00001000;
var ecRightsAllMask			=		0x000015FB;

// rights template from provider/ECProps/PropPage.cpp
var ecRightsTemplate = new Object();
ecRightsTemplate[_("Full control")] 	= ecRightsFullControl;
ecRightsTemplate[_("Owner")] 			= ecRightsAll;
ecRightsTemplate[_("Secretary")] 		= (ecRightsFullControl&~ecRightsCreateSubfolder);
ecRightsTemplate[_("Only read")] 		= ecRightsReadAny|ecRightsFolderVisible;
ecRightsTemplate[_("No rights")] 		= ecRightsNone|ecRightsFolderVisible;

// message flags
var MSGFLAG_READ		=	0x00000001;
var MSGFLAG_UNMODIFIED	=	0x00000002;
var MSGFLAG_SUBMIT		=	0x00000004;
var MSGFLAG_UNSENT		=	0x00000008;
var MSGFLAG_HASATTACH	=	0x00000010;
var MSGFLAG_FROMME		=	0x00000020;
var MSGFLAG_ASSOCIATED	=	0x00000040;
var MSGFLAG_RESEND		=	0x00000080;
var MSGFLAG_RN_PENDING	=	0x00000100;
var MSGFLAG_NRN_PENDING	=	0x00000200;

// importance
var IMPORTANCE_LOW		= 0;
var IMPORTANCE_NORMAL	= 1;
var IMPORTANCE_HIGH		= 2;

// FLAG_ICON
var olRedFlagIcon		= 6;
var olBlueFlagIcon		= 5;
var olYellowFlagIcon	= 4;
var olGreenFlagIcon		= 3;
var olOrangeFlagIcon	= 2;
var olPurpleFlagIcon	= 1;
var olNoFlagIcon		= 0;

// FLAG_STATUS
var olNoFlag			= 0;
var olFlagComplete		= 1;
var olFlagMarked		= 2;

// sensitivity
var SENSITIVITY_NONE					= 0x00000000;
var SENSITIVITY_PERSONAL				= 0x00000001;
var SENSITIVITY_PRIVATE					= 0x00000002;
var SENSITIVITY_COMPANY_CONFIDENTIAL	= 0x00000003;

// RecurrenceType
var olRecursDaily = 0
var olRecursWeekly = 1
var olRecursMonthly = 2
var olRecursMonthNth = 3
var olRecursYearly = 5
var olRecursYearNth = 6

// TaskStatus
var olTaskNotStarted = 0
var olTaskInProgress = 1
var olTaskComplete = 2
var olTaskWaiting = 3
var olTaskDeferred = 4

// Access
var MAPI_ACCESS_MODIFY				= 0x00000001;
var MAPI_ACCESS_READ				= 0x00000002;
var MAPI_ACCESS_DELETE				= 0x00000004;
var MAPI_ACCESS_CREATE_HIERARCHY	= 0x00000008;
var MAPI_ACCESS_CREATE_CONTENTS		= 0x00000010;
var MAPI_ACCESS_CREATE_ASSOCIATED	= 0x00000020;

// search 
var SEARCH_RUNNING			= 0x00000001;
var SEARCH_REBUILD			= 0x00000002;
var SEARCH_RECURSIVE		= 0x00000004;
var SEARCH_FOREGROUND		= 0x00000008;

var STORE_SEARCH_OK			= 0x00000004;

// Restrictions
var RES_AND					= 0x00000000;
var RES_OR					= 0x00000001;
var RES_NOT					= 0x00000002;
var RES_CONTENT				= 0x00000003;
var RES_PROPERTY			= 0x00000004;
var RES_COMPAREPROPS		= 0x00000005;
var RES_BITMASK				= 0x00000006;
var RES_SIZE				= 0x00000007;
var RES_EXIST				= 0x00000008;
var RES_SUBRESTRICTION		= 0x00000009;
var RES_COMMENT				= 0x0000000A;

// String fuzzylevel
var FL_FULLSTRING			= 0x00000000;
var FL_SUBSTRING			= 0x00000001;
var FL_PREFIX				= 0x00000002;
var FL_IGNORECASE			= 0x00010000;
var FL_IGNORENONSPACE		= 0x00020000;
var FL_LOOSE				= 0x00040000;

// Restriction comparison operators
var RELOP_LT				= 0x00000000;		// <
var RELOP_LE				= 0x00000001;		// <=
var RELOP_GT				= 0x00000002;		// >
var RELOP_GE				= 0x00000003;		// >=
var RELOP_EQ				= 0x00000004;		// ==
var RELOP_NE				= 0x00000005;		// !=
var RELOP_RE				= 0x00000006;		// LIKE (Regular expression)

// Bitmask operators, for RES_BITMASK only
var BMR_EQZ					= 0x00000000;		// == 0
var BMR_NEZ					= 0x00000001;		// != 0

/**
 * array index values of restrictions -- 
 * same values are used in php-ext/main.cpp::PHPArraytoSRestriction()
 */
var VALUE					= 0;				// propval
var RELOP					= 1;				// compare method
var FUZZYLEVEL				= 2;				// string search flags
var CB						= 3;				// size restriction
var ULTYPE					= 4;				// bit mask restriction type BMR_xxx
var ULMASK					= 5;				// bitmask
var ULPROPTAG				= 6;				// property
var ULPROPTAG1				= 7;				// RES_COMPAREPROPS 1st property
var ULPROPTAG2				= 8;				// RES_COMPAREPROPS 2nd property
var PROPS					= 9;				// RES_COMMENT properties
var RESTRICTION				= 10;				// RES_COMMENT and RES_SUBRESTRICTION restriction

// Meeting Request Recipient Type
var MAPI_ORIG	= 0;
var MAPI_TO		= 1;
var MAPI_CC		= 2;
var MAPI_BCC	= 3;

// OlResponseStatus
var olResponseNone				= 0;
var olResponseOrganized			= 1;
var olResponseTentative			= 2;
var olResponseAccepted			= 3;
var olResponseDeclined			= 4;
var olResponseNotResponded		= 5;

// OlMeetingStatus
var olNonMeeting				= 0;
var olMeeting					= 1;
var olMeetingReceived			= 3;
var olMeetingCanceled			= 5;
var olMeetingReceivedAndCanceled= 7;

// Free/busy status
var fbFree						= 0;
var fbTentative					= 1;
var fbBusy						= 2;
var fbOutOfOffice				= 3;

// Recipient flags
var recipSendable				= 1;
var recipOrganizer				= 2;

/**
 * List of Key combinations handled by Keycontroller.
 */
var KEYS = new Array();
// Keys for creating new item.
KEYS["new"] = new Array();
KEYS["new"]["item"]				= 'N';			// New Item
KEYS["new"]["appointment"] 		= 'N+A';		// New Appointment
KEYS["new"]["contact"]			= 'N+C';		// New Contact
KEYS["new"]["distlist"]			= 'N+D';		// New Distributionlist
KEYS["new"]["folder"]			= 'N+F';		// New Folder
KEYS["new"]["task"]				= 'N+K';		// New Task
KEYS["new"]["taskrequest"]		= 'N+U';		// New Task Request
KEYS["new"]["mail"]				= 'N+M';		// New Mail
KEYS["new"]["meeting_request"]	= 'N+Q';		// New Meeting Request
KEYS["new"]["note"]				= 'N+S';		// New Note

// Keys for opening default folders.
KEYS["open"] = new Array();
KEYS["open"]["inbox"]			= 'G+M';		// Jump to default Inbox folder
KEYS["open"]["calendar"]		= 'G+A';		// Jump to default Calendar folder
KEYS["open"]["contact"]			= 'G+C';		// Jump to default Contacts folder
KEYS["open"]["task"]			= 'G+K';		// Jump to default Tasks folder
KEYS["open"]["note"]			= 'G+S';		// Jump to default Notes folder
KEYS["open"]["journal"]			= 'G+J';		// Jump to default Journal folder
KEYS["open"]["muc"]				= 'G+U';		// Jump to MutliUser Calendar
KEYS["open"]["shared_store"]	= 'Z+S';		// Open shared store
KEYS["open"]["shared_folder"]	= 'Z+F';		// Open shared folder

// Key to refresh a folder.
KEYS["refresh"] = new Array();
KEYS["refresh"]["folder"]	= 'F+R';		// Refresh folder

// Key for toggling Reading pane.
KEYS["readingpane"] = new Array();
KEYS["readingpane"]["toggle"]	= 'Z+R';		// Toggle Reading Pane

// Key for selecting all items.
KEYS["select"] = new Array();
KEYS["select"]["all_items"]	= 'CTRL+A';		// Select all items

// Key for search options
KEYS["search"] = new Array();
KEYS["search"]["normal"]	= 'S+N';			// Search
KEYS["search"]["advanced"]	= 'S+A';			// Advanced Search

// Key for reply/replyall/forward options for message.
KEYS["respond_mail"] = new Array();
KEYS["respond_mail"]["reply"]		= 'R';			// Reply to message
KEYS["respond_mail"]["replyall"]	= 'A';			// ReplyAll to message
KEYS["respond_mail"]["forward"]		= 'F';			// Forward message

// MUC related keys.
KEYS["muc"] = new Array();
KEYS["muc"]["add_user"]			= 'M+A';		// Add new user
KEYS["muc"]["remove_user"]		= 'M+R';		// Remove user
KEYS["muc"]["load_group"]		= 'M+G';		// Load Group
KEYS["muc"]["next_day"]			= 'M+RA';		// Next Day
KEYS["muc"]["previous_day"]		= 'M+LA';		// Previous Day
KEYS["muc"]["next_period"]		= 'M+.';		// Next Period
KEYS["muc"]["previous_period"]	= 'M+,';		// Previous Period
KEYS["muc"]["zoom_in"]			= 'M+UA';		// Zoom In
KEYS["muc"]["zoom_out"]			= 'M+DA';		// Zoom Out

// Key for editing items
KEYS["edit_item"] = new Array();
KEYS["edit_item"]["copy"]			= 'I+C';		// Copy item to folder
KEYS["edit_item"]["move"]			= 'I+M';		// Move item to folder
KEYS["edit_item"]["toggle_read"]	= 'I+R';		// Mark read/unread (toggling)
KEYS["edit_item"]["categorize"]		= 'I+G';		// Categorize Item
KEYS["edit_item"]["print"]			= 'CTRL+P';			// Print Item
KEYS["edit_item"]["toggle_flag"]	= 'I+F';		// Flag

// Key for switching views
KEYS["view"] = new Array();
KEYS["view"]["prev"]	= 'V+[';		// Previous View
KEYS["view"]["next"]	= 'V+]';		// Next View

KEYS["quick_edit"] = new Array();
KEYS["quick_edit"]["activate"]		= 'F2';
KEYS["quick_edit"]["deactivate"]	= 'ESCAPE';

KEYS["mail"] = new Array();
KEYS["mail"]["save"]	= 'CTRL+S';
KEYS["mail"]["send"]	= 'CTRL+ENTER';

KEYS["respond_meeting"] = new Array();
KEYS["respond_meeting"]["accept"]		= 'ALT+C';
KEYS["respond_meeting"]["tentative"]	= 'ALT+T';
KEYS["respond_meeting"]["decline"]		= 'ALT+D';

// For GAB display type values
var DT_MAILUSER					= 0x00000000;
var DT_DISTLIST					= 0x00000001;
var DT_FORUM					= 0x00000002;
var DT_AGENT					= 0x00000003;
var DT_ORGANIZATION				= 0x00000004;
var DT_PRIVATE_DISTLIST			= 0x00000005;
var DT_REMOTE_MAILUSER			= 0x00000006;
var DT_ROOM						= 0x00000007;
var DT_EQUIPMENT				= 0x00000008;
var DT_SEC_DISTLIST				= 0x00000009;

// PR_OBJECT_TYPE values
var MAPI_STORE			= 1;	// MAPI Store
var MAPI_ADDRBOOK		= 2;	// MAPI Address Book
var MAPI_FOLDER			= 3;	// MAPI Folder
var MAPI_ABCONT			= 4;	// MAPI Address Book Container
var MAPI_MESSAGE		= 5;	// MAPI Message
var MAPI_MAILUSER		= 6;	// MAPI Address Book MailUser
var MAPI_ATTACH			= 7;	// MAPI Attachment
var MAPI_DISTLIST		= 8;	// MAPI Address Book Distribution List
var MAPI_PROFSECT		= 9;	// MAPI Profile Section
var MAPI_STATUS			= 10	// MAPI Status
var MAPI_SESSION		= 11	// MAPI Session
var MAPI_FORMINFO		= 12;	// MAPI Form Information

// Task request constants

var tdmtNothing		= 0;
var tdmtTaskReq		= 1;
var tdmtTaskAcc		= 2;
var tdmtTaskDec		= 3;
var tdmtTaskUpd		= 4;
var tdmtTaskSELF 	= 5;

var thNone			= 0;
var thAccepted		= 1;
var thDeclined		= 2;
var thUpdated		= 3;
var thDueDateChanged = 4;
var thAssigned		=5;

var tdsNOM			= 0;
var tdsOWNNEW		= 1;
var tdsOWN			= 2;
var tdsACC			= 3;
var tdsDEC			= 4;

var olNewTask		= 0;
var olDelegatedTask	= 1;	// Task has been assigned
var olOwnTask		= 3;	// Task owned

var olTaskNotDelegated			= 0;
var olTaskDelegationUnknown		= 1;	// After sending req
var olTaskDelegationAccepted	= 2;	// After receiving accept
var olTaskDelegationDeclined	= 3;	// After receiving decline