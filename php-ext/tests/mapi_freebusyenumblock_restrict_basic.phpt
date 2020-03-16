--TEST--
mapi_freebusyenumblock_restrict() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_freebusyenumblock_restrict(null, 0, 0));
--EXPECTF--
Warning: mapi_freebusyenumblock_restrict() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
