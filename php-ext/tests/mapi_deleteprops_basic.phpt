--TEST--
mapi_deleteprops() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_deleteprops(null, array()));
--EXPECTF--
Warning: mapi_deleteprops() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
