--TEST--
mapi_folder_setreadflags() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_folder_setreadflags(null, array()));
--EXPECTF--
Warning: mapi_folder_setreadflags() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
