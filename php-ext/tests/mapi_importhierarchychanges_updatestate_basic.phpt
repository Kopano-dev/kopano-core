--TEST--
mapi_importhierarchychanges_updatestate() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_importhierarchychanges_updatestate(null, null));
--EXPECTF--
Warning: mapi_importhierarchychanges_updatestate() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
