--TEST--
mapi_getprops() basic tests
--SKIPIF--
<?php if (!extension_loaded("mapi") print "skip"; ?>
--FILE--
<?php
var_dump(mapi_getprops(null, "store"));
--EXPECTF--
Warning: mapi_getprops() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
