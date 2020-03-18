--TEST--
mapi_freebusysupport_open() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();

var_dump(mapi_freebusysupport_open($session));
--EXPECTF--
resource(%d) of type (Freebusy Support Interface)
