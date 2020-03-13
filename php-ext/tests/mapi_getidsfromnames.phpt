--TEST--
mapi_getidsfromnames() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

define('PSETID_Appointment',                    makeguid("{00062002-0000-0000-C000-000000000046}"));

$store = getDefaultStore(getMapiSession());

var_dump(mapi_getidsfromnames($store, array(0x820d), array(PSETID_Appointment)));
--EXPECTF--
array(1) {
  [0]=>
  int(-%d)
}
