--TEST--
mapi_folder_deletemessages() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_folder_deletemessages(null, array()));
--EXPECTF--
Warning: mapi_folder_deletemessages() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
