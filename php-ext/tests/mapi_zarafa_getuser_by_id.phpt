--TEST--
mapi_zarafa_getuser_by_id() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());
$username = getenv("KOPANO_TEST_USER");

$user = mapi_zarafa_getuser_by_name($store, $username);
$userid = $user["userid"];

var_dump(mapi_zarafa_getuser_by_id($store, "garbage"));
var_dump(count(mapi_zarafa_getuser_by_id($store, $userid)));
--EXPECTF--
Warning: mapi_zarafa_getuser_by_id(): Unable to get user: missing or invalid argument (80070057) in %s on line %d
bool(false)
int(%d)
