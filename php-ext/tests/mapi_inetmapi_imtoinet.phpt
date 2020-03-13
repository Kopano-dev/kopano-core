--TEST--
mapi_inetmapi_imtoinet() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$store = getDefaultStore($session);
$ab = mapi_openaddressbook($session);
$root = mapi_msgstore_openentry($store, null);
$msg = mapi_folder_createmessage($root);

var_dump(mapi_inetmapi_imtoinet($session, $ab, $msg, array()));
--EXPECTF--
resource(%d) of type (IStream Interface)
