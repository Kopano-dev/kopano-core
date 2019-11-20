--TEST--
kc_session_save() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$data = null;
var_dump(kc_session_save($session, $data));
var_dump(gettype($data));
--EXPECTF--
int(0)
string(6) "string"
