--TEST--
mapi_msgstore_advise() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_msgstore_advise(null, "test", 0, null));
--EXPECTF--
Warning: mapi_msgstore_advise() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
