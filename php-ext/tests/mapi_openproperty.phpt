--TEST--
mapi_openproperty() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());

var_dump(mapi_openproperty($store, PR_EC_WEBACCESS_SETTINGS_JSON, IID_IStream, STGM_TRANSACTED, MAPI_MODIFY | MAPI_CREATE));;
--EXPECTF--
resource(%d) of type (IStream Interface)
