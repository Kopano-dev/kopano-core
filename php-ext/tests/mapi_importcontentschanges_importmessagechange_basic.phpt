--TEST--
mapi_importcontentschanges_importmessagechange() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
$mapimessage = null;
var_dump(mapi_importcontentschanges_importmessagechange(null, array(), 0, $mapimessage));
--EXPECTF--
Warning: mapi_importcontentschanges_importmessagechange() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
