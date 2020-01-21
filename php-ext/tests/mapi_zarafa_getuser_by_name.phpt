--TEST--
mapi_zarafa_getuser_by_name() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());
$username = getenv("KOPANO_TEST_USER");

var_dump(mapi_zarafa_getuser_by_name($store, "nopenopenope"));
$user = mapi_zarafa_getuser_by_name($store, $username);
var_dump($user["emailaddress"]);
--EXPECTF--
Warning: mapi_zarafa_getuser_by_name(): Unable to resolve user: not found (8004010f) in %s on line %d
bool(false)
string(%d) "%s"
