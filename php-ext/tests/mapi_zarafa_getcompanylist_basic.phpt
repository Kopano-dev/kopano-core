--TEST--
mapi_zarafa_getcompanylist() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_zarafa_getcompanylist(null));
--EXPECTF--
Warning: mapi_zarafa_getcompanylist() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
