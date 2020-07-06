from importlib.machinery import SourceFileLoader

from plugintemplates import MP_STOP_SUCCESS

from MAPI.Struct import SPropValue
from MAPI.Tags import PR_SUBJECT, PR_RECEIVED_BY_EMAIL_ADDRESS, PR_MESSAGE_CLASS


def test_movetopublic(tmp_path, pluginpath, logger, session, ab, store, root, message, publicfolder):
    plugin = SourceFileLoader('movetopublic', pluginpath + '/movetopublic.py').load_module()
    plugin.MoveToPublic.configfile = pluginpath + '/movetopublic.cfg'
    movetopublic = plugin.MoveToPublic(logger)

    message.SetProps([SPropValue(PR_MESSAGE_CLASS, b'IPM.Note'),
                      SPropValue(PR_SUBJECT, b'movetopublic'),
                      SPropValue(PR_RECEIVED_BY_EMAIL_ADDRESS, b'testuser')])
    message.SaveChanges(0)

    retval = movetopublic.PreDelivery(session, ab, store, root, message)
    assert retval == (MP_STOP_SUCCESS,)

    table = publicfolder.GetContentsTable(0)
    assert table.GetRowCount(0)
