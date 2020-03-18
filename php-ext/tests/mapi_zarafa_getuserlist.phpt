--TEST--
mapi_zarafa_getuserlist() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());

var_dump(count(mapi_zarafa_getuserlist($store)));
--EXPECTF--
int(%d)
