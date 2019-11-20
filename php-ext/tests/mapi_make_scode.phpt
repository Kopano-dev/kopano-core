--TEST--
mapi_make_scode() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
define('MAPI_E_NOT_ENOUGH_MEMORY', (int)-2147024882);

var_dump(mapi_make_scode(null));
var_dump(mapi_make_scode(0, 0x380) != MAPI_E_NOT_ENOUGH_MEMORY);
--EXPECTF--
Warning: mapi_make_scode() expects exactly 2 parameters, 1 given in %s on line %d
NULL
bool(true)
