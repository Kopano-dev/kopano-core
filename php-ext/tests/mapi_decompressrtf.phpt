--TEST--
mapi_decompressrtf() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_decompressrtf(null));
$rtf = "\x0f\x01\x00\x00\xff\x00\x00\x00MELA\x00\x00\x00\x00{\\rtf1\\ansi\\ansicpg1252\\fromtext \\deff0{\\fonttbl\n{\\f0\\fswiss Arial;}\n{\\f1\\fmodern Courier New;}\n{\\f2\\fnil\\fcharset2 Symbol;}\n{\\f3\\fmodern\\fcharset0 Courier New;}}\n{\\colortbl\\red0\\green0\\blue0;\\red0\\green0\\blue255;}\n\\uc1\\pard\\plain\\deftab360 \\f0\\fs20 test}";
var_dump(mapi_decompressrtf($rtf));
--EXPECT--
string(0) ""
string(255) "{\rtf1\ansi\ansicpg1252\fromtext \deff0{\fonttbl
{\f0\fswiss Arial;}
{\f1\fmodern Courier New;}
{\f2\fnil\fcharset2 Symbol;}
{\f3\fmodern\fcharset0 Courier New;}}
{\colortbl\red0\green0\blue0;\red0\green0\blue255;}
\uc1\pard\plain\deftab360 \f0\fs20 test}"
