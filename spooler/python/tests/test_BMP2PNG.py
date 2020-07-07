from importlib.machinery import SourceFileLoader

from MAPI import MAPI_DEFERRED_ERRORS
from MAPI.Tags import PR_ATTACH_MIME_TAG_W


def test_bmp2png(pluginpath, logger, session, ab, store, root, bmpmessage):
    plugin = SourceFileLoader('BMP2PNG', pluginpath + '/BMP2PNG.py').load_module()
    bmp2png = plugin.BMP2PNG(logger)

    bmp2png.PostConverting(session, ab, store, root, bmpmessage)

    table = bmpmessage.GetAttachmentTable(MAPI_DEFERRED_ERRORS)
    table.SetColumns([PR_ATTACH_MIME_TAG_W], 0)
    rows = table.QueryRows(1, 0)

    assert rows
    assert rows[0][0].Value == 'image/png'
