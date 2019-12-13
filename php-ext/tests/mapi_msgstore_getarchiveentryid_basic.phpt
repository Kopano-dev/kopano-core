--TEST--
mapi_msgstore_getarchiveentryid() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_msgstore_getarchiveentryid(null, 0, 0));
--EXPECTF--
Warning: mapi_msgstore_getarchiveentryid() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
