--TEST--
mapi_zarafa_getuser_by_id() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_zarafa_getuser_by_id(null, "test"));
--EXPECTF--
Warning: mapi_zarafa_getuser_by_id() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
