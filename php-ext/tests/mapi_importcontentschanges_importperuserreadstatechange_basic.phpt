--TEST--
mapi_importcontentschanges_importperuserreadstatechange() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_importcontentschanges_importperuserreadstatechange(null, array()));
--EXPECTF--
Warning: mapi_importcontentschanges_importperuserreadstatechange() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
