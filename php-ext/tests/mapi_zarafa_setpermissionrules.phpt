--TEST--
mapi_zarafa_setpermissionrules() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());
$root = mapi_msgstore_openentry($store, null);

var_dump(mapi_zarafa_setpermissionrules($root, array()));
--EXPECTF--
bool(false)
