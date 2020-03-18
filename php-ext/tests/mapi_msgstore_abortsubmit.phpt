--TEST--
mapi_msgstore_abortsubmit() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());

var_dump(mapi_msgstore_abortsubmit($store, "garbage"));
--EXPECTF--
Warning: mapi_msgstore_abortsubmit(): Unable to abort submit: not found (8004010f) in %s on line %d
bool(false)
