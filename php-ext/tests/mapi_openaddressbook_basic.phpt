--TEST--
mapi_openaddressbook() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_openaddressbook(null));
--EXPECTF--

Warning: mapi_openaddressbook() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
