--TEST--
mapi_exportchanges_updatestate() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_exportchanges_updatestate(null, null));
--EXPECTF--
Warning: mapi_exportchanges_updatestate() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
