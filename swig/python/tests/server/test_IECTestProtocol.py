import pytest


from MAPI.Tags import IID_IECTestProtocol


@pytest.mark.skip(reason='requires admin access')
def test_ping(adminstore):
    testproto = adminstore.QueryInterface(IID_IECTestProtocol)
    assert testproto.TestGet('ping') == 'pong'
