--TEST--
mapi_savechanges() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_savechanges(null));
--EXPECTF--
Warning: mapi_savechanges() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
