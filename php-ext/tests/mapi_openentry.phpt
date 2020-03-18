--TEST--
mapi_openentry() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$store = getDefaultStore($session);
$root = mapi_msgstore_openentry($store);
$props = mapi_getprops($root, array(PR_ENTRYID));
var_dump(mapi_openentry($session, $props[PR_ENTRYID]));
--EXPECTF--
resource(%d) of type (MAPI Folder)
