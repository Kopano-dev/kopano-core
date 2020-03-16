--TEST--
mapi_zarafa_getuserlist() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_zarafa_getuserlist(null));
var_dump(mapi_zarafa_getuserlist(null, "test"));
--EXPECTF--
Warning: mapi_zarafa_getuserlist() expects parameter 1 to be resource, null given in %s on line %d
bool(false)

Warning: mapi_zarafa_getuserlist() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
