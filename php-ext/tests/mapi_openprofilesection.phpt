--TEST--
mapi_openprofilesection() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$pbGlobalProfileSectionGuid = makeGuid("{C8B0DB13-05AA-1A10-9BB0-00AA002FC45A}");
$session = getMapiSession();

var_dump(mapi_openprofilesection($session, $pbGlobalProfileSectionGuid));
--EXPECTF--
resource(%d) of type (MAPI Property)
