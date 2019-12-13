--TEST--
mapi_importcontentschanges_importmessagemove() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_importcontentschanges_importmessagemove(null, "", "", "", "", ""));
--EXPECTF--
Warning: mapi_importcontentschanges_importmessagemove() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
