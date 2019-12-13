--TEST--
mapi_exportchanges_config() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_exportchanges_config(null, null, 0, null, array(), array(), false, 0));
--EXPECTF--
Warning: mapi_exportchanges_config() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
