--TEST--
mapi_msgstore_entryidfromsourcekey() basic tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_msgstore_entryidfromsourcekey(null, "store"));
--EXPECTF--
Warning: mapi_msgstore_entryidfromsourcekey() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
