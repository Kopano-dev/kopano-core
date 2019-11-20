--TEST--
mapi_mapitoical() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_mapitoical(null, null, null, array()));
--EXPECTF--
Warning: mapi_mapitoical() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
