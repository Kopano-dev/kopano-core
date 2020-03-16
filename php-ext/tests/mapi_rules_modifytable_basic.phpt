--TEST--
mapi_rules_modifytable() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_rules_modifytable(null, array()));
--EXPECTF--
Warning: mapi_rules_modifytable() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
