--TEST--
mapi_freebusydata_enumblocks() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_freebusydata_enumblocks(null, 0, 0));
--EXPECTF--
Warning: mapi_freebusydata_enumblocks() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
