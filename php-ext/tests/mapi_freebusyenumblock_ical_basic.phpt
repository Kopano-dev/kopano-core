--TEST--
mapi_freebusyenumblock_ical() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_freebusyenumblock_ical(null, null, 0, 0, 0, "", "", ""));
--EXPECTF--
Warning: mapi_freebusyenumblock_ical() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
