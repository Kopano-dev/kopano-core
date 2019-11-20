--TEST--
mapi_wrap_importhierarchychanges() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
class PHPWrapper {
}

$phpwrapper = new PHPWrapper();
var_dump(mapi_wrap_importhierarchychanges($phpwrapper));
--EXPECTF--
resource(%d) of type (ICS Import Hierarchy Changes)
