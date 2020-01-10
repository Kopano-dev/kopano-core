import os

import pytest

from MAPI import MAPIAdminProfiles


kopanoserver = pytest.mark.skipif(
        not os.getenv('KOPANO_TEST_SERVER'),
        reason='No kopano-server running'
)


@pytest.fixture
def adminprof():
    return MAPIAdminProfiles(0)


@pytest.fixture
def adminservice(adminprof):
    name = b't1'
    adminprof.CreateProfile(name, None, 0, 0)

    yield adminprof.AdminServices(name, None, 0, 0)

    adminprof.DeleteProfile(name, 0)
