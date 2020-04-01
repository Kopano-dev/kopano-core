import os

import pytest

from MAPI.Tags import PR_IPM_SUBTREE_ENTRYID, PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID


if not os.getenv('KOPANO_SOCKET'):
    pytest.skip('No kopano-server running', allow_module_level=True)


def test_ipm(publicstore):
    props = publicstore.GetProps([PR_IPM_SUBTREE_ENTRYID, PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID], 0)
    assert props[0].ulPropTag == PR_IPM_SUBTREE_ENTRYID
    assert props[1].ulPropTag == PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID
