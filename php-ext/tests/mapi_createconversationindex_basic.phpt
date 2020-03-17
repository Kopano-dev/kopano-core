--TEST--
mapi_createconversationindex() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_createconversationindex(array()));
//var_dump(mapi_createconversationindex("kopano"));
--EXPECTF--
Warning: mapi_createconversationindex() expects parameter 1 to be string, array given in %s on line %d
bool(false)
