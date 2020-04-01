import pytest

from MAPI import MODRECIP_ADD, MODRECIP_MODIFY, MODRECIP_REMOVE
from MAPI.Struct import MAPIError


def test_modifyrecipients(message):
    with pytest.raises(MAPIError) as excinfo:
        message.ModifyRecipients(0, None)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)

    with pytest.raises(MAPIError) as excinfo:
        message.ModifyRecipients(MODRECIP_ADD, None)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)

    with pytest.raises(MAPIError) as excinfo:
        message.ModifyRecipients(MODRECIP_MODIFY, None)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)

    with pytest.raises(MAPIError) as excinfo:
        message.ModifyRecipients(MODRECIP_REMOVE, None)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)
