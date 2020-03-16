--TEST--
mapi_stream_read() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
$stream = mapi_stream_create();
var_dump(mapi_stream_read($stream, 20));
--EXPECT--
string(0) ""
