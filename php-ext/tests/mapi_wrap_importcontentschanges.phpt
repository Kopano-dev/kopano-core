--TEST--
mapi_wrap_importcontentschanges() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
class PHPWrapper {
}

$phpwrapper = new PHPWrapper();
var_dump(mapi_wrap_importcontentschanges($phpwrapper));
--EXPECTF--
resource(%d) of type (ICS Import Contents Changes)
