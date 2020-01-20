--TEST--
mapi_feature() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") print "skip"; ?>
--FILE--
<?php
var_dump(mapi_feature(null));
var_dump(mapi_feature("LOGONFLAGS"));
var_dump(mapi_feature("NOTIFICATIONS"));
var_dump(mapi_feature("INETMAPI_IMTOMAPI"));
--EXPECTF--
bool(false)
bool(true)
bool(true)
bool(true)
