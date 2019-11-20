--TEST--
mapi_freebusy_openmsg() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());

var_dump(mapi_freebusy_openmsg($store));
--EXPECTF--
resource(%d) of type (MAPI Message)
