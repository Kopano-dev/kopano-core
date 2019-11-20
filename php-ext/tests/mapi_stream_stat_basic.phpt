--TEST--
mapi_stream_stat() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
$stream = mapi_stream_create();
var_dump(mapi_stream_stat($stream));
--EXPECTF--
array(1) {
  ["cb"]=>
  int(0)
}
