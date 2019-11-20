--TEST--
mapi_openproperty() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_openproperty(null, 0, 0, 0, 0));
--EXPECTF--
Warning: mapi_openproperty() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
