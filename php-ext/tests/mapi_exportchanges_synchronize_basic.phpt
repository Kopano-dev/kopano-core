--TEST--
mapi_exportchanges_synchronize() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_exportchanges_synchronize(null));
--EXPECTF--
Warning: mapi_exportchanges_synchronize() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
