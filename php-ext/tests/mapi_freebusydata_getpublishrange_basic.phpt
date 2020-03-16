--TEST--
mapi_freebusydata_getpublishrange() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_freebusydata_getpublishrange(null));
--EXPECTF--
Warning: mapi_freebusydata_getpublishrange() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
