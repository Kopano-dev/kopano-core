import pytest

from MAPI.Tags import (PR_IPM_SUBTREE_ENTRYID, PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID,
                       PR_DISPLAY_NAME_A, PR_DISPLAY_NAME_W)
from MAPI.Util import SPropValue


def test_ipm(publicstore):
    props = publicstore.GetProps([PR_IPM_SUBTREE_ENTRYID, PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID], 0)
    assert props[0].ulPropTag == PR_IPM_SUBTREE_ENTRYID
    assert props[1].ulPropTag == PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID


@pytest.mark.parametrize("proptag,value", [(PR_DISPLAY_NAME_A, b'Public Folders'), (PR_DISPLAY_NAME_W, 'Public Folders')])
def test_generated_props(publicstore, proptag, value):
    props = publicstore.GetProps([proptag], 0)
    assert props[0].Value == value


@pytest.mark.parametrize("proptag,value", [(PR_DISPLAY_NAME_A, b'hello'), (PR_DISPLAY_NAME_W, 'フォルダ')])
def test_folder_setprops(publicfolder, proptag, value):
    publicfolder.SetProps([SPropValue(proptag, value)])
    props = publicfolder.GetProps([proptag], 0)
    assert props[0].Value == value
