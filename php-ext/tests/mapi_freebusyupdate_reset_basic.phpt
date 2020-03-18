--TEST--
mapi_freebusyupdate_reset() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_freebusyupdate_reset(null));
--EXPECTF--
Warning: mapi_freebusyupdate_reset() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
