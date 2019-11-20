--TEST--
mapi_folder_copyfolder() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_folder_copyfolder(null, "", null, "", 0));
--EXPECTF--
Warning: mapi_folder_copyfolder() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
