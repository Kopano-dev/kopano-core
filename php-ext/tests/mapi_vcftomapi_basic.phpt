--TEST--
mapi_vcftomapi() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_vcftomapi(null, null, null, ""));
--EXPECTF--
Warning: mapi_vcftomapi() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
