import os
import pytest
import inetmapi

from MAPI import MAPI_BEST_ACCESS
from MAPI.Util import PpropFindProp
from MAPI.Util.codepage import GetCharsetByCP
from MAPI.Tags import (PR_DISPLAY_TO_W, PR_SENDER_NAME_W, PR_SUBJECT_W, PR_BODY_W, PR_INTERNET_CPID,
                       PR_ATTACH_NUM, IID_IAttachment, PR_ATTACH_MIME_TAG_W, PR_ATTACH_DATA_BIN)


MAILS_PATH = os.path.dirname(os.path.realpath(__file__)) + '/mails/{}'


def test_gb2312_18030(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('gb2312_18030.eml'), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)

    props = message.GetProps([PR_SENDER_NAME_W, PR_DISPLAY_TO_W, PR_SUBJECT_W], 0)
    assert props[0].Value == "梁祐晟"
    assert props[1].Value == "'林慶美'"
    assert props[2].Value == '讀取: ??排??生?a'


def test_zcp_11581(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('zcp-11581.eml'), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_BODY_W], 0)
    assert "RFC-compliant" in props[0].Value


@pytest.mark.parametrize("eml", ["zcp-11699-p.eml", "zcp-11699-utf8.eml", "zcp-11699-ub.eml"])
def test_zcp_11699_p(session, store, message, eml, dopt):
    rfc = open(MAILS_PATH.format(eml), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_SUBJECT_W, PR_BODY_W], 0)
    assert props[0].Value == "☺ dum"
    assert props[1].Value == "☺ dummy ☻"


def test_zcp_11713(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('zcp-11713.eml'), 'rb').read()
    dopt.charset_strict_rfc = False
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID, PR_BODY_W], 0)

	# ISO-2022-JP (50220, 50222) is a valid outcome of any decoder.
	# SHIFT_JIS (932) is a possible outcome of ZCP's IMToMAPI, but not
	# strictly RFC-conformant.
    charset = GetCharsetByCP(props[0].Value)
    assert charset == "shift-jis"
    assert "メッセージ" in props[1].Value


def test_zcp_12930(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('zcp-12930.eml'), 'rb').read()
    dopt.ascii_upgrade = None
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID, PR_BODY_W], 0)

    charset = GetCharsetByCP(props[0].Value)
    assert charset == "us-ascii"

    assert "simply dummy t ext" in props[1].Value


def test_zcp_13036_0d(session, store, message):
    rfc = open(MAILS_PATH.format('zcp-13036-0db504a2.eml'), 'rb').read()
    dopt = inetmapi.delivery_options()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID, PR_BODY_W], 0)
    assert GetCharsetByCP(props[0].Value) == "utf-8"
    assert "zgłoszeń" in props[1].Value


def test_zcp_13036_69(session, store, message):
    rfc = open(MAILS_PATH.format('zcp-13036-6906a338.eml'), 'rb').read()
    dopt = inetmapi.delivery_options()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID, PR_BODY_W], 0)
    assert GetCharsetByCP(props[0].Value) == "iso-8859-1"
    assert "Jänner" in props[1].Value


def test_zcp_13036_lh(session, store, message):
    rfc = open(MAILS_PATH.format('zcp-13036-lh.eml'), 'rb').read()
    dopt = inetmapi.delivery_options()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID, PR_BODY_W], 0)

    assert GetCharsetByCP(props[0].Value) == "utf-8"
    assert "können, öffnen" in props[1].Value


def test_zcp_13175(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('zcp-13175.eml'), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID, PR_BODY_W], 0)

    assert GetCharsetByCP(props[0].Value) == "utf-8"
    print(props[1].Value)
    assert "extrem überhöht" in props[1].Value


def test_zcp_13337(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('zcp-13337.eml'), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID, PR_BODY_W], 0)

    assert GetCharsetByCP(props[0].Value) == "utf-8"
    # non breaking space
    assert "\xa0" in props[1].Value


def test_zcp_13439_nl(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('zcp-13439-nl.eml'), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID, PR_BODY_W, PR_SUBJECT_W], 0)

    assert GetCharsetByCP(props[0].Value) == "utf-8"
    assert "für" in props[1].Value
    assert "Ää Öö Üü ß – Umlautetest, UMLAUTETEST 2" == props[2].Value


def test_zcp_13449_meca(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('zcp-13449-meca.eml'), 'rb').read()
    dopt.charset_strict_rfc = False
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID, PR_BODY_W], 0)

    assert GetCharsetByCP(props[0].Value) == "windows-1252"
    assert "Mécanique" in props[1].Value


def test_zcp_13449_na(session, store, message, dopt):
    # On unknown Content-Transfer-Encodings, the MIME part needs to be
    # read raw and tagged application/octet-stream (RFC 2045 §6.4 pg 17).
    rfc = open(MAILS_PATH.format('zcp-13449-na.eml'), 'rb').read()
    dopt.ascii_upgrade = None
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID, PR_BODY_W, PR_SUBJECT_W], 0)

    assert GetCharsetByCP(props[0].Value) == "us-ascii"
	# All non-ASCII is stripped, and the '!' is the leftover.
    assert "!" in props[1].Value
    # May need rework depending on how unreadable characters
    # are transformed (decoder dependent).
    assert "N??t ASCII??????" == props[2].Value


def test_zcp_13473(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('zcp-13473.eml'), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID], 0)

    assert GetCharsetByCP(props[0].Value) == "utf-8"


def test_kc_138_1(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('kc-138-1.eml'), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_BODY_W], 0)

    # Ensure later bodies do not override iCal object description
    assert "part1" in props[0].Value


def test_kc_138_2(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('kc-138-2.eml'), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_BODY_W], 0)

    # Ensure iCal object description does not override earlier bodies
    assert "part1" in props[0].Value or "part2" in props[0].Value


def test_big5(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('big5.eml'), 'rb').read()
    dopt.ascii_upgrade = "big5"
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_SUBJECT_W], 0)

    assert props[0].Value == "?i???h?u?W?q??"


def test_html_charset_01(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('html-charset-01.eml'), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    attachtable = message.GetAttachmentTable(0)
    props = attachtable.QueryRows(-1, 0)
    prop = PpropFindProp(props[0], PR_ATTACH_NUM)
    attachment = message.OpenAttach(0, IID_IAttachment, 0)
    props = attachment.GetProps([PR_ATTACH_MIME_TAG_W, PR_ATTACH_DATA_BIN], 0)
    assert props[0].Value == "text/html"
    assert props[1].Value == b"body\xd0\xa7\n"


def test_html_charset_02(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('html-charset-02.eml'), 'rb').read()
    dopt.charset_strict_rfc = False
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID], 0)
    charset = GetCharsetByCP(props[0].Value)
    assert charset == "iso-8859-1"


@pytest.mark.parametrize("eml", ["iconvonly01.eml", "iconvonly02.eml"])
def test_iconvonly(session, store, message, dopt, eml):
	# iconvonly01 has a charset for which no Win32 CPID exists (at
	# least in our codepage.cpp), and so gets reconverted by inetmapi
	# to UTF-8 so Windows has something to display.
    rfc = open(MAILS_PATH.format(eml), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID], 0)
    charset = GetCharsetByCP(props[0].Value)
    assert charset == "utf-8"


@pytest.mark.parametrize("eml", ["no-content-type.eml", "no-content-type-alt.eml", "no-charset-01.eml", "no-charset-02.eml", "no-charset-03.eml", "no-charset-07.eml"])
def test_charset_upgrade(session, store, message, dopt, eml):
    rfc = open(MAILS_PATH.format(eml), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID], 0)
    charset = GetCharsetByCP(props[0].Value)
    assert charset == "utf-8"


def test_rfc2045_sec6_4(session, store, message, dopt):
	# On unknown Content-Transfer-Encodings, the MIME part needs to be
	# read raw and tagged application/octet-stream (RFC 2045 §6.4 pg 17).
    rfc = open(MAILS_PATH.format('unknown-transfer-enc.eml'), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    attachtable = message.GetAttachmentTable(0)
    props = attachtable.QueryRows(-1, 0)
    prop = PpropFindProp(props[0], PR_ATTACH_NUM)
    attachment = message.OpenAttach(0, IID_IAttachment, 0)
    props = attachment.GetProps([PR_ATTACH_MIME_TAG_W, PR_ATTACH_DATA_BIN], 0)

    assert props[0].Value == "application/octet-stream"
    assert props[1].Value == b"=E2=98=BA"


def test_unknown_text_charset(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('unknown-text-charset.eml'), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    attachtable = message.GetAttachmentTable(0)
    props = attachtable.QueryRows(-1, 0)
    prop = PpropFindProp(props[0], PR_ATTACH_NUM)
    attachment = message.OpenAttach(0, IID_IAttachment, 0)
    props = attachment.GetProps([PR_ATTACH_MIME_TAG_W, PR_ATTACH_DATA_BIN], 0)

    assert props[0].Value == "text/plain"
    assert props[1].Value == b'\xe2\x98\xba'


def test_encword_split(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('encoded-word-split.eml'), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_SUBJECT_W], 0)
    assert props[0].Value == "☺ vų. ?뮘"


@pytest.mark.parametrize("eml", ["mime_charset_01.eml", "mime_charset_02.eml"])
def test_mime_charset_01(session, store, message, dopt, eml):
    rfc = open(MAILS_PATH.format(eml), 'rb').read()
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID, PR_BODY_W], 0)
    charset = GetCharsetByCP(props[0].Value)
    assert charset == "utf-8"
    assert props[1].Value == "tæst"


def test_mime_charset_03(session, store, message, dopt):
    rfc = open(MAILS_PATH.format('mime_charset_03.eml'), 'rb').read()
    dopt.ascii_upgrade = None
    inetmapi.IMToMAPI(session, store, None, message, rfc, dopt)
    props = message.GetProps([PR_INTERNET_CPID], 0)
    charset = GetCharsetByCP(props[0].Value)
    assert charset == "us-ascii"
