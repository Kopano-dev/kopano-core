--TEST--
mapi_logon_zarafa() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_logon_zarafa(null));
--EXPECTF--
Warning: mapi_logon_zarafa() expects at least 2 parameters, 1 given in %s on line %d
bool(false)
