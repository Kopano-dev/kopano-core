import os

import pytest

from MAPI import (MAPI_E_NOT_FOUND, MAPI_E_NOT_ENOUGH_MEMORY,
                  MAPI_CREATE, MAPI_MODIFY, MAPI_MOVE)
from MAPI.Struct import PROP_TAG, PROP_ID, SPropValue
from MAPI.Tags import (PT_ERROR, PR_BODY, PR_RTF_COMPRESSED, PR_HTML,
                       PR_ENTRYID, IID_IStream, IID_IMessage)
from MAPI.Util import WrapCompressedRTFStream, STREAM_SEEK_SET


if not os.getenv('KOPANO_SOCKET'):
    pytest.skip('No kopano-server running', allow_module_level=True)


def test_plain(store, message):
    message.SetProps([SPropValue(PR_BODY, b'pr_body')])
    props = message.GetProps([PR_BODY, PR_RTF_COMPRESSED, PR_HTML], 0)
    assert props[0].ulPropTag == PR_BODY
    assert props[0].Value == b'pr_body'
    assert props[1].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_RTF_COMPRESSED))
    assert props[1].Value == MAPI_E_NOT_FOUND
    assert props[2].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_HTML))
    assert props[2].Value == MAPI_E_NOT_FOUND

    # Retest after reload
    message.SaveChanges(0)
    entryid = message.GetProps([PR_ENTRYID], 0)[0].Value
    message = store.OpenEntry(entryid, None, 0)
    props = message.GetProps([PR_BODY, PR_RTF_COMPRESSED, PR_HTML], 0)

    assert props[0].ulPropTag == PR_BODY
    assert props[0].Value == b'pr_body'
    assert props[1].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_RTF_COMPRESSED))
    assert props[1].Value == MAPI_E_NOT_FOUND
    assert props[2].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_HTML))
    assert props[2].Value == MAPI_E_NOT_FOUND

    # Try two properties

    # BODY, RTF
    props = message.GetProps([PR_BODY, PR_RTF_COMPRESSED], 0)
    assert props[0].ulPropTag == PR_BODY
    assert props[0].Value == b'pr_body'
    assert props[1].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_RTF_COMPRESSED))
    assert props[1].Value == MAPI_E_NOT_FOUND

    # BODY, HTML
    props = message.GetProps([PR_BODY, PR_HTML], 0)
    assert props[0].ulPropTag == PR_BODY
    assert props[0].Value == b'pr_body'
    assert props[1].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_HTML))
    assert props[1].Value == MAPI_E_NOT_FOUND

    # RTF, HTML
    props = message.GetProps([PR_RTF_COMPRESSED, PR_HTML], 0)
    assert props[0].ulPropTag == PR_RTF_COMPRESSED
    assert len(props[0].Value) > 0
    assert props[1].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_HTML))
    assert props[1].Value == MAPI_E_NOT_FOUND

    # Try one property

    # BODY
    props = message.GetProps([PR_BODY], 0)
    assert props[0].ulPropTag == PR_BODY
    assert props[0].Value == b'pr_body'

    # RTF
    props = message.GetProps([PR_RTF_COMPRESSED], 0)
    assert props[0].ulPropTag == PR_RTF_COMPRESSED
    assert len(props[0].Value) > 0

    # HTML
    props = message.GetProps([PR_HTML], 0)
    assert props[0].ulPropTag == PR_HTML
    assert len(props[0].Value) > 0


def test_html(store, message):
    message.SetProps([SPropValue(PR_HTML, b"pr_html_body")])
    props = message.GetProps([PR_BODY, PR_RTF_COMPRESSED, PR_HTML], 0)

    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_BODY))
    assert props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY
    bodystream = message.OpenProperty(PR_BODY, IID_IStream, 0, 0)
    assert bodystream.Read(4096) == b'pr_html_body'
    assert props[1].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_RTF_COMPRESSED))
    assert props[1].Value == MAPI_E_NOT_ENOUGH_MEMORY
    assert props[2].ulPropTag == PR_HTML
    assert props[2].Value == b'pr_html_body'

    # Retest after reload
    message.SaveChanges(0)
    entryid = message.GetProps([PR_ENTRYID], 0)[0].Value
    message = store.OpenEntry(entryid, None, 0)
    props = message.GetProps([PR_BODY, PR_RTF_COMPRESSED, PR_HTML], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_BODY))
    assert props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY

    bodystream = message.OpenProperty(PR_BODY, IID_IStream, 0, 0)
    assert bodystream.Read(4096) == b'pr_html_body'
    assert props[1].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_RTF_COMPRESSED))
    assert props[1].Value == MAPI_E_NOT_ENOUGH_MEMORY
    assert props[2].ulPropTag == PR_HTML
    assert props[2].Value == b'pr_html_body'

    # Try two properties

    # BODY == RTF
    props = message.GetProps([PR_BODY, PR_RTF_COMPRESSED], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_BODY))
    assert props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY
    assert props[1].ulPropTag == PR_RTF_COMPRESSED
    assert len(props[1].Value)

    # BODY == HTML
    props = message.GetProps([PR_BODY, PR_HTML], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_BODY))
    assert props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY
    assert props[1].ulPropTag == PR_HTML
    assert props[1].Value == b'pr_html_body'

    # RTF == HTML
    props = message.GetProps([PR_RTF_COMPRESSED, PR_HTML], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_RTF_COMPRESSED))
    assert props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY
    assert props[1].ulPropTag == PR_HTML
    assert props[1].Value == b'pr_html_body'

    # Try one property

    # BODY
    props = message.GetProps([PR_BODY], 0)
    assert props[0].ulPropTag == PR_BODY
    assert props[0].Value == b'pr_html_body'

    # RTF
    props = message.GetProps([PR_RTF_COMPRESSED], 0)
    assert props[0].ulPropTag == PR_RTF_COMPRESSED
    assert len(props[0].Value) > 0

    # HTML
    props = message.GetProps([PR_HTML], 0)
    assert props[0].ulPropTag == PR_HTML
    assert props[0].Value == b'pr_html_body'


def test_rtf(store, message):
    rtfprop = message.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, 0, MAPI_MODIFY | MAPI_CREATE)
    rtf = WrapCompressedRTFStream(rtfprop, 0)
    rtf.Write(b'pr_rtf_compressed')
    rtf.Commit(0)
    rtfprop.Seek(STREAM_SEEK_SET, 0)
    compdata = rtfprop.Read(4096)
    props = message.GetProps([PR_BODY, PR_RTF_COMPRESSED, PR_HTML], 0)

    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_BODY))
    assert props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY

    bodystream = message.OpenProperty(PR_BODY, IID_IStream, 0, 0)
    assert bodystream.Read(4096) == b'pr_rtf_compressed\r\n'
    assert props[1].ulPropTag == PR_RTF_COMPRESSED
    assert props[1].Value == compdata
    assert props[2].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_HTML))
    assert props[2].Value == MAPI_E_NOT_FOUND

    # Retest after reload
    message.SaveChanges(0)
    entryid = message.GetProps([PR_ENTRYID], 0)[0].Value
    message = store.OpenEntry(entryid, None, 0)
    props = message.GetProps([PR_BODY, PR_RTF_COMPRESSED, PR_HTML], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_BODY))
    assert props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY

    bodystream = message.OpenProperty(PR_BODY, IID_IStream, 0, 0)
    assert bodystream.Read(4096) == b'pr_rtf_compressed\r\n'
    assert props[1].ulPropTag == PR_RTF_COMPRESSED
    assert props[1].Value == compdata
    assert props[2].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_HTML))
    assert props[2].Value == MAPI_E_NOT_FOUND

    # Try two properties

    # BODY == RTF
    props = message.GetProps([PR_BODY, PR_RTF_COMPRESSED], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_BODY))
    assert props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY
    assert props[1].ulPropTag == PR_RTF_COMPRESSED
    assert len(props[1].Value)

    # BODY == HTML
    props = message.GetProps([PR_BODY, PR_HTML], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_BODY))
    assert props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY
    assert props[1].ulPropTag == PR_HTML
    assert len(props[1].Value) > 0

    # RTF == HTML
    props = message.GetProps([PR_RTF_COMPRESSED, PR_HTML], 0)
    assert props[0].ulPropTag == PR_RTF_COMPRESSED
    assert len(props[0].Value) > 0
    assert props[1].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_HTML))
    assert props[1].Value == MAPI_E_NOT_FOUND

    # Try one property

    # BODY
    props = message.GetProps([PR_BODY], 0)
    assert props[0].ulPropTag == PR_BODY
    assert props[0].Value == b'pr_rtf_compressed\r\n'

    # RTF
    props = message.GetProps([PR_RTF_COMPRESSED], 0)
    assert props[0].ulPropTag == PR_RTF_COMPRESSED
    assert len(props[0].Value) > 0

    # HTML
    props = message.GetProps([PR_HTML], 0)
    assert props[0].ulPropTag == PR_HTML
    assert len(props[0].Value) > 0


def test_ChangeToPlainAndReopen(store, message):
    message.SetProps([SPropValue(PR_HTML, b'<html><body>pr_html</body></html>')])
    message.SaveChanges(2)
    message.SetProps([SPropValue(PR_BODY, b'pr_body')])
    message.SaveChanges(2)
    eid = message.GetProps([PR_ENTRYID], 0)[0]
    message = store.OpenEntry(eid.Value, None, 0)
    body = message.GetProps([PR_BODY], 0)[0].Value
    html = message.GetProps([PR_HTML], 0)[0].Value
    assert body == b'pr_body'
    assert b'<html><body>pr_html</body></html>' != html


def test_ChangeToHTMLAndReopen(store, message):
    message.SetProps([SPropValue(PR_BODY, b"pr_body")])
    message.SaveChanges(2)
    message.SetProps([SPropValue(PR_HTML, b"<html><body>pr_html</body></html>")])
    message.SaveChanges(2)
    eid = message.GetProps([PR_ENTRYID], 0)[0]
    message = store.OpenEntry(eid.Value, None, 0)
    body = message.GetProps([PR_BODY], 0)[0].Value
    html = message.GetProps([PR_HTML], 0)[0].Value
    assert body != b'pr_body'
    assert b'<html><body>pr_html</body></html>' == html


def testHtmlCopyToRTF(root, message):
    # this is what outlook does when you press the 'send' button on a new and unsaved html message
    message.SetProps([SPropValue(PR_HTML, b"<html><body>pr_html</body></html>")])
    msgnew = root.CreateMessage(None, 0)
    message.CopyProps([PR_RTF_COMPRESSED], 0, None, IID_IMessage, msgnew, MAPI_MOVE)
    html = msgnew.GetProps([PR_HTML], 0)[0].Value
    assert b"<html><body>pr_html</body></html>" in html


def test_HtmlCopyToPlain(root, message):
    message.SetProps([SPropValue(PR_HTML, b"<html><body>pr_html</body></html>")])
    msgnew = root.CreateMessage(None, 0)
    message.CopyProps([PR_BODY], 0, None, IID_IMessage, msgnew, MAPI_MOVE)
    html = msgnew.GetProps([PR_HTML], 0)[0].Value
    assert html == b'<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2//EN">\n<HTML>\n<HEAD>\n<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=us-ascii">\n<META NAME="Generator" CONTENT="Kopano HTML builder 1.0">\n<TITLE></TITLE>\n</HEAD>\n<BODY>\n<!-- Converted from text/plain format -->\n\n<P><FONT STYLE="font-family: courier" SIZE=2>\npr_html</FONT>\n</P>\n\n</BODY></HTML>'


def testPlainCopyToRTF(root, message):
    message.SetProps([SPropValue(PR_BODY, b"pr_body")])
    msgnew = root.CreateMessage(None, 0)
    message.CopyProps([PR_RTF_COMPRESSED], 0, None, IID_IMessage, msgnew, MAPI_MOVE)
    plain = msgnew.GetProps([PR_BODY], 0)[0].Value
    assert plain == b"pr_body\r\n"


def testPlainCopyToHTML(root, message):
    message.SetProps([SPropValue(PR_BODY, b"pr_body")])
    msgnew = root.CreateMessage(None, 0)
    message.CopyProps([PR_HTML], 0, None, IID_IMessage, msgnew, MAPI_MOVE)
    plain = msgnew.GetProps([PR_BODY], 0)[0].Value
    assert plain == b"pr_body\r\n"


def testRTFCopyToPlain(root, message):
    rtfprop = message.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, 0, MAPI_MODIFY | MAPI_CREATE)
    rtf = WrapCompressedRTFStream(rtfprop, 0)
    rtf.Write(b'pr_rtf_compressed')
    rtf.Commit(0)
    msgnew = root.CreateMessage(None, 0)
    message.CopyProps([PR_BODY], 0, None, IID_IMessage, msgnew, MAPI_MOVE)
    rtfprop = msgnew.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, 0, 0)
    rtf = WrapCompressedRTFStream(rtfprop, 0)
    compdata2 = rtf.Read(4096)
    assert b'{\\rtf1\\ansi\\ansicpg1252\\fromtext \\deff0{\\fonttbl\n{\\f0\\fswiss Arial;}\n{\\f1\\fmodern Courier New;}\n{\\f2\\fnil\\fcharset2 Symbol;}\n{\\f3\\fmodern\\fcharset0 Courier New;}}\n{\\colortbl\\red0\\green0\\blue0;\\red0\\green0\\blue255;}\n\\uc1\\pard\\plain\\deftab360 \\f0\\fs20 pr_rtf_compressed\\line\n}' == compdata2


def testRTFCopyToHTML(root, message):
    rtfprop = message.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, 0, MAPI_MODIFY | MAPI_CREATE)
    rtf = WrapCompressedRTFStream(rtfprop, 0)
    rtf.Write(b'pr_rtf_compressed')
    rtf.Commit(0)
    msgnew = root.CreateMessage(None, 0)
    message.CopyProps([PR_HTML], 0, None, IID_IMessage, msgnew, MAPI_MOVE)
    rtfprop = msgnew.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, 0, 0)
    rtf = WrapCompressedRTFStream(rtfprop, 0)
    compdata2 = rtf.Read(4096)
    b'{\\rtf1\\ansi\\ansicpg1252\\fromhtml1 \\deff0{\\fonttbl\r\n{\\f0\\fswiss\\fcharset0 Arial;}\r\n{\\f1\\fmodern Courier New;}\r\n{\\f2\\fnil\\fcharset2 Symbol;}\r\n{\\f3\\fmodern\\fcharset0 Courier New;}\r\n{\\f4\\fswiss\\fcharset0 Arial;}\r\n{\\f5\\fswiss Tahoma;}\r\n{\\f6\\fswiss\\fcharset0 Times New Roman;}}\r\n{\\colortbl\\red0\\green0\\blue0;\\red0\\green0\\blue255;\\red0\\green0\\blue255;}\r\n\\uc1\\pard\\plain\\deftab360 \\f0\\fs24 \r\n{\\*\\htmltag243 <!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2//EN">}{\\*\\htmltag64}\r\n{\\*\\htmltag3 \\par }\r\n{\\*\\htmltag19 <HTML>}{\\*\\htmltag64}\r\n{\\*\\htmltag2 \\par }\r\n{\\*\\htmltag34 <HEAD>}{\\*\\htmltag64}\r\n{\\*\\htmltag1 \\par }\r\n{\\*\\htmltag161 <META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=us-ascii">}{\\*\\htmltag64}\r\n{\\*\\htmltag1 \\par }\r\n{\\*\\htmltag161 <META NAME="Generator" CONTENT="Kopano HrExtractHTMLFromRealRTF">}{\\*\\htmltag64}\r\n{\\*\\htmltag1 \\par }\r\n{\\*\\htmltag177 <TITLE>}{\\*\\htmltag64}\r\n{\\*\\htmltag185 </TITLE>}{\\*\\htmltag64}\r\n{\\*\\htmltag1 \\par }\r\n{\\*\\htmltag42 </HEAD>}{\\*\\htmltag64}\r\n{\\*\\htmltag2 \\par }\r\n{\\*\\htmltag50 <BODY>}{\\*\\htmltag64}\r\n{\\*\\htmltag0 \\par }\r\n{\\*\\htmltag240 <!-- Converted from text/rtf format -->}{\\*\\htmltag64}\r\n{\\*\\htmltag0 \\par }\r\n{\\*\\htmltag0 \\par }\r\n{\\*\\htmltag64 <p>}{\\*\\htmltag64}\\htmlrtf \\line \\htmlrtf0 pr_rtf_compressed\r\n{\\*\\htmltag72 </p>}{\\*\\htmltag64}\\htmlrtf \\line \\htmlrtf0 \r\n{\\*\\htmltag0 \\par }\r\n{\\*\\htmltag0 \\par }\r\n{\\*\\htmltag58 </BODY>}{\\*\\htmltag64}\r\n{\\*\\htmltag2 \\par }\r\n{\\*\\htmltag27 </HTML>}{\\*\\htmltag64}\r\n{\\*\\htmltag3 \\par }}\r\n' == compdata2
