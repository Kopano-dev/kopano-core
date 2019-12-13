--TEST--
mapi_importcontentschanges_config() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_importcontentschanges_config(null, null, 0));
--EXPECTF--
Warning: mapi_importcontentschanges_config() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
