--TEST--
mapi_freebusyupdate_publish() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_freebusyupdate_publish(null, array()));
--EXPECTF--
Warning: mapi_freebusyupdate_publish() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
