import os

import pytest


from MAPI.Tags import IID_IECTestProtocol


if not os.getenv('KOPANO_SOCKET'):
    pytest.skip('No kopano-server running', allow_module_level=True)


@pytest.mark.skip(reason='requires an admin user')
def test_ping(store):
    testproto = store.QueryInterface(IID_IECTestProtocol)
    assert testproto.TestGet('ping') == 'pong'
