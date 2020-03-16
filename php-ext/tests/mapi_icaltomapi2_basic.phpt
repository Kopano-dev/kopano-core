--TEST--
mapi_icaltomapi2()
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_icaltomapi2(null, null, null));
--EXPECTF--
Warning: mapi_icaltomapi2() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
