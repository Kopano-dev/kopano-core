--TEST--
mapi_icaltomapi2() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');
$session = getMapiSession();
$store = getDefaultStore($session);
$ab = mapi_openaddressbook($session);
$root = mapi_msgstore_openentry($store, null);

var_dump(mapi_icaltomapi2($ab, $root, "LOL"));

$invalid = "BEGIN:VCALENDAR
NOPE:LOL
END:VCALENDAR";
var_dump(mapi_icaltomapi2($ab, $root, $invalid));
var_dump(mapi_numinvalidicalproperties());
var_dump(mapi_numinvalidicalcomponents());

$valid = "BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//hacksw/handcal//NONSGML v1.0//EN
BEGIN:VEVENT
UID:uid1@example.com
DTSTAMP:19970714T170000Z
ORGANIZER;CN=John Doe:MAILTO:john.doe@example.com
DTSTART:19970714T170000Z
DTEND:19970715T035959Z
SUMMARY:Bastille Day Party
GEO:48.85299;2.36885
END:VEVENT
END:VCALENDAR";
var_dump(mapi_icaltomapi2($ab, $root, $valid));
var_dump(mapi_numinvalidicalproperties());
var_dump(mapi_numinvalidicalcomponents());

--EXPECTF--
bool(false)
array(0) {
}
int(1)
int(0)
array(1) {
  [0]=>
  resource(%d) of type (MAPI Message)
}
int(0)
int(0)
