--TEST--
mapi_createoneoff() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
$oneoff = mapi_createoneoff("dîsplay", "typé", "emaïl");
var_dump(mapi_parseoneoff($oneoff));
--EXPECT--
array(3) {
  ["name"]=>
  string(8) "dîsplay"
  ["type"]=>
  string(5) "typé"
  ["address"]=>
  string(6) "emaïl"
}
