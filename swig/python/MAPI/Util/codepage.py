# SPDX-License-Identifier: AGPL-3.0-only
import MAPI

# same map from common/codepage.cpp
_charset_map = [
    ("DIN_66003", 20106),
    ("NS_4551-1", 20108),
    ("SEN_850200_B", 20107),
    ("big5", 950),
    ("csISO2022JP", 50221),
    ("euc-jp", 51932),
    ("euc-cn", 51936),
    ("euc-kr", 51949),
    ("euc-kr", 949),
    ("cp949", 949),
    ("ks_c_5601-1987", 949),
    ("gb18030", 936),
    ("gb2312", 936),
    ("GBK", 936),
    ("csgb2312", 52936),
    ("ibm852", 852),
    ("ibm866", 866),
    ("iso-2022-jp", 50220),
    ("iso-2022-jp", 50222),
    ("iso-2022-kr", 50225),
    ("windows-1252", 1252),
    ("iso-8859-1", 1252),
    ("iso-8859-1", 28591),
    ("iso-8859-2", 28592),
    ("iso-8859-3", 28593),
    ("iso-8859-4", 28594),
    ("iso-8859-5", 28595),
    ("iso-8859-6", 28596),
    ("iso-8859-7", 28597),
    ("iso-8859-8", 28598),
    ("iso-8859-8-i", 28598),
    ("iso-8859-9", 28599),
    ("iso-8859-13", 28603),
    ("iso-8859-15", 28605),
    ("koi8-r", 20866),
    ("koi8-u", 21866),
    ("shift-jis", 932),
    ("unicode", 1200),
    ("unicodebig", 1201),
    ("utf-7", 65000),
    ("utf-8", 65001),
    ("windows-1250", 1250),
    ("windows-1251", 1251),
    ("windows-1253", 1253),
    ("windows-1254", 1254),
    ("windows-1255", 1255),
    ("windows-1256", 1256),
    ("windows-1257", 1257),
    ("windows-1258", 1258),
    ("windows-874", 874),
    ("us-ascii", 2012)
]

def GetCharsetByCP(codepage):
    for ch, cp in _charset_map:
        if codepage == cp:
            return ch
    raise MAPI.Struct.MAPIErrorNotFound

def GetCPByCharset(charset):
    for ch, cp in _charset_map:
        if charset == ch:
            return cp
    raise MAPI.Struct.MAPIErrorNotFound
