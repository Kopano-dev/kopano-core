--TEST--
mapi_freebusydata_setrange() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_freebusydata_setrange(null, 0, 0));
--EXPECTF--
Warning: mapi_freebusydata_setrange() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
