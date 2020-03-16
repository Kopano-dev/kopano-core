--TEST--
mapi_openmsgstore() basic tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_openmsgstore(null, "store"));
--EXPECTF--
Warning: mapi_openmsgstore() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
