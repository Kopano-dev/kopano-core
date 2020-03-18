--TEST--
mapi_importhierarchychanges_config() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_importhierarchychanges_config(null, null, null));
--EXPECTF--
Warning: mapi_importhierarchychanges_config() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
