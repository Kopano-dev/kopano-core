--TEST--
mapi_setprops() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_setprops(null, array()));
--EXPECTF--
Warning: mapi_setprops() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
