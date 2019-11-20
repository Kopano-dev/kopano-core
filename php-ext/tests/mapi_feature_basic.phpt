--TEST--
mapi_feature() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") print "skip"; ?>
--FILE--
<?php
var_dump(mapi_feature(null));
var_dump(mapi_feature("LOGONFLAGS"));
--EXPECTF--
bool(false)
bool(true)
