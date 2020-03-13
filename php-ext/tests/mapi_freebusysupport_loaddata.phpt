--TEST--
mapi_freebusysupport_loaddata() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();

$fb = mapi_freebusysupport_open($session);
var_dump(mapi_freebusysupport_loaddata($fb, array()));
mapi_freebusysupport_close($fb);
--EXPECT--
array(0) {
}
