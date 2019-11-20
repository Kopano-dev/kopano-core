--TEST--
mapi_freebusysupport_close() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_freebusysupport_close(null));
--EXPECTF--
Warning: mapi_freebusysupport_close() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
