import pytest

from MAPI.Util import (MAPIAdviseSink, IID_IMAPIAdviseSink, MAPIError,
                       fnevObjectCreated)


class AdviseSink(MAPIAdviseSink):
    def __init__(self):
        MAPIAdviseSink.__init__(self, [IID_IMAPIAdviseSink])


def test_mismatch_advise(store):
    inboxeid = store.GetReceiveFolder(b'IPM', 0)[0]
    sink = AdviseSink()
    with pytest.raises(MAPIError) as excinfo:
        store.Advise(inboxeid, fnevObjectCreated, sink)
    assert 'MAPI_E_NO_SUPPORT' in str(excinfo)
