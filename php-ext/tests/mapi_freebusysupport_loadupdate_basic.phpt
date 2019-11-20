--TEST--
mapi_freebusysupport_loadupdate() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_freebusysupport_loadupdate(null, array()));
--EXPECTF--
Warning: mapi_freebusysupport_loadupdate() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
