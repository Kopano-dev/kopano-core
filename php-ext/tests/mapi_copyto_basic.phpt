--TEST--
mapi_copyto() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_copyto(null, array(), array(), null));
--EXPECTF--
Warning: mapi_copyto() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
