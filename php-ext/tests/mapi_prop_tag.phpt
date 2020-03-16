--TEST--
mapi_prop_tag() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
define('PT_STRING8', 30);
define('PR_SUBJECT', mapi_prop_tag(PT_STRING8,     0x0037));

var_dump(mapi_prop_tag(PT_STRING8, 0x0036) != PR_SUBJECT);
mapi_prop_tag();
--EXPECTF--
bool(true)

Warning: mapi_prop_tag() expects exactly 2 parameters, 0 given in %s on line %d
