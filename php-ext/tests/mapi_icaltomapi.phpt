--TEST--
mapi_icaltomapi() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$data = "
BEGIN:VCALENDAR
PRODID:-//Mozilla.org/NONSGML Mozilla Calendar V1.1//EN
VERSION:2.0
BEGIN:VTIMEZONE
TZID:Europe/Amsterdam
X-LIC-LOCATION:Europe/Amsterdam
BEGIN:DAYLIGHT
TZOFFSETFROM:+0100
TZOFFSETTO:+0200
TZNAME:CEST
DTSTART:19700329T020000
RRULE:FREQ=YEARLY;BYDAY=-1SU;BYMONTH=3
END:DAYLIGHT
BEGIN:STANDARD
TZOFFSETFROM:+0200
TZOFFSETTO:+0100
TZNAME:CET
DTSTART:19701025T030000
RRULE:FREQ=YEARLY;BYDAY=-1SU;BYMONTH=10
END:STANDARD
END:VTIMEZONE
BEGIN:VEVENT
CREATED:20101129T075140Z
LAST-MODIFIED:20101129T075151Z
DTSTAMP:20101129T075151Z
UID:a224887f-9d76-41f9-942b-7096ed9c19f8
SUMMARY:TEst put 1
DTSTART;TZID=Europe/Amsterdam:20101217T090000
DTEND;TZID=Europe/Amsterdam:20101217T100000
DESCRIPTION:test
END:VEVENT
END:VCALENDAR
";

$session = getMapiSession();
$store = getDefaultStore($session);
$addrbook = mapi_openaddressbook($session);
$root = mapi_msgstore_openentry($store, null);
$message = mapi_folder_createmessage($root);
$ics = mapi_icaltomapi($session, $store, $addrbook, $message, $data, false);
var_dump($ics);
var_dump(mapi_getprops($message, array(PR_SUBJECT)));
--EXPECTF--
bool(true)
array(1) {
  [3604510]=>
  string(10) "TEst put 1"
}

