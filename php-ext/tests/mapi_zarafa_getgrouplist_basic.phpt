--TEST--
mapi_zarafa_getgrouplist() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_zarafa_getgrouplist(null, "test"));
--EXPECTF--
Warning: mapi_zarafa_getgrouplist() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
