--TEST--
mapi_msgstore_createentryid() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());

var_dump(mapi_msgstore_createentryid($store, getenv("KOPANO_TEST_USER")));
--EXPECTF--
string(%d) "%s"
