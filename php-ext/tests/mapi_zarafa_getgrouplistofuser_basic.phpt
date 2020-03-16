--TEST--
mapi_zarafa_getgrouplistofuser() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_zarafa_getgrouplistofuser(null, null));
--EXPECTF--
Warning: mapi_zarafa_getgrouplistofuser() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
