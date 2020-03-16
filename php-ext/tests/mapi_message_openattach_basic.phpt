--TEST--
mapi_message_openattach() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_message_openattach(null));
--EXPECTF--
Warning: mapi_message_openattach() expects exactly 2 parameters, 1 given in %s on line %d
bool(false)
