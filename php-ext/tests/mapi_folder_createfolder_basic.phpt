--TEST--
mapi_folder_createfolder() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_folder_createfolder(null, ""));
--EXPECTF--
Warning: mapi_folder_createfolder() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
