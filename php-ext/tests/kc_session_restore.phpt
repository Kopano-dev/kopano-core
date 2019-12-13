--TEST--
kc_session_restore() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$data = null;
$newsession = null;
kc_session_save($session, $data);
var_dump(kc_session_restore($data, $newsession));
var_dump($newsession);
--EXPECTF--
int(0)
resource(%d) of type (MAPI Session)
