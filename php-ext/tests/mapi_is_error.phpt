--TEST--
mapi_is_error() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
define('MAPI_E_NOT_ENOUGH_MEMORY', (int)-2147024882);
define('MAPI_W_PARTIAL_COMPLETION', mapi_make_scode(0, 0x680));

var_dump(mapi_is_error(null));
var_dump(mapi_is_error(MAPI_W_PARTIAL_COMPLETION));
var_dump(mapi_is_error(MAPI_E_NOT_ENOUGH_MEMORY));
mapi_is_error();
--EXPECTF--
bool(false)
bool(false)
bool(true)

Warning: mapi_is_error() expects exactly 1 parameter, 0 given in %s on line %d
