--TEST--
mapi_importcontentschanges_importmessagedeletion() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_importcontentschanges_importmessagedeletion(null, 0, array()));
--EXPECTF--
Warning: mapi_importcontentschanges_importmessagedeletion() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
