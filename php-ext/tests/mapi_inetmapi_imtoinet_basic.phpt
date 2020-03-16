--TEST--
mapi_inetmapi_imtoinet() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_inetmapi_imtoinet(null, null, null, array()));
--EXPECTF--
Warning: mapi_inetmapi_imtoinet() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
