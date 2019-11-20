--TEST--
mapi_freebusyenumblock_reset() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_freebusyenumblock_reset(null));
--EXPECTF--
Warning: mapi_freebusyenumblock_reset() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
