from importlib.machinery import SourceFileLoader

from MAPI import KEEP_OPEN_READWRITE
from MAPI.Struct import SPropValue
from MAPI.Tags import PR_SUBJECT, PR_BODY, PR_HTML, PR_MESSAGE_CLASS


DISCLAIMER_TXT = b'this is a disclaimer'
DISCLAIMER_HTML = b'<p>HTML Disclaimer</p>'


def test_disclaimer(tmp_path, pluginpath, logger, session, ab, store, root, message):
    plugin = SourceFileLoader('Disclaimer', pluginpath + '/disclaimer.py').load_module()
    disclaimer = plugin.Disclaimer(logger)
    disclaimer.disclaimerdir = tmp_path

    textfile = tmp_path / 'default.txt'
    textfile.write_bytes(DISCLAIMER_TXT)
    htmlfile = tmp_path / 'default.html'
    htmlfile.write_bytes(DISCLAIMER_HTML)

    message.SetProps([SPropValue(PR_MESSAGE_CLASS, b'IPM.Note'),
                      SPropValue(PR_SUBJECT, b'disclaimer'),
                      SPropValue(PR_BODY, b'test')])
    message.SaveChanges(KEEP_OPEN_READWRITE)

    disclaimer.PreSending(session, ab, store, root, message)

    props = message.GetProps([PR_BODY], 0)
    assert DISCLAIMER_TXT in props[0].Value

    message.DeleteProps([PR_BODY])
    message.SetProps([SPropValue(PR_HTML, b'<p>test</p>')])
    message.SaveChanges(0)

    disclaimer.PreSending(session, ab, store, root, message)

    props = message.GetProps([PR_HTML], 0)
    assert DISCLAIMER_HTML in props[0].Value
