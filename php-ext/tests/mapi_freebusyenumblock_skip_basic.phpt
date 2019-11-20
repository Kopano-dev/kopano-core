--TEST--
mapi_freebusyenumblock_skip() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_freebusyenumblock_skip(null, 0));
--EXPECTF--
Warning: mapi_freebusyenumblock_skip() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
