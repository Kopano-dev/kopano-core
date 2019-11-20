--TEST--
mapi_importhierarchychanges_importfolderdeletion() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_importhierarchychanges_importfolderdeletion(null, 0, array()));
--EXPECTF--
Warning: mapi_importhierarchychanges_importfolderdeletion() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
