--TEST--
mapi_prop_id() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
define('PT_STRING8', 30);
define('PR_SUBJECT', mapi_prop_tag(PT_STRING8,     0x0037));

var_dump(mapi_prop_id(PR_SUBJECT) == PR_SUBJECT >> 16);
mapi_prop_id();
--EXPECTF--
bool(true)

Warning: mapi_prop_id() expects exactly 1 parameter, 0 given in %s on line %d
